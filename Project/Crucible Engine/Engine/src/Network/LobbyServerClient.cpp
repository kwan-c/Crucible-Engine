/*************************************************************************
* \file:         LobbyServerClient.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          21 November 2026
* \version       1.1
* \brief
*                LobbyServerClient (see the header).
*
* \history
*    -v1.0       LAN connection server client
*    -v1.1       monitoring (StatusRequest / StatusReply); browser Unreachable after kListAttempts
*                unanswered list requests
*
**************************************************************************/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#include <bcrypt.h>

#include "Network/LobbyServerClient.h"

#include <algorithm>
#include <array>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "bcrypt.lib")

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace Crucible
{
	using namespace LobbyServer;

	const char* ToString(LobbyServerStatus status) noexcept
	{
		switch (status)
		{
		case LobbyServerStatus::Connecting:  return "connecting";
		case LobbyServerStatus::Online:      return "online";
		case LobbyServerStatus::Unreachable: return "unreachable";
		default:                             return "off";
		}
	}

	bool LobbyServerClient::SecureRandom64(std::uint64_t& out) noexcept
	{
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			std::uint64_t value = 0;
			if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return false;
			if (value != 0) { out = value; return true; }
		}
		return false;
	}

	std::vector<HostAddress> LobbyServerClient::LocalNetworks()
	{
		struct Found { HostAddress address; bool physical; };
		std::vector<Found> found;
		ULONG size = 16 * 1024;
		std::vector<unsigned char> buffer(size);
		const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
		ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
		if (result == ERROR_BUFFER_OVERFLOW)
		{
			buffer.resize(size);
			result = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
		}
		if (result != NO_ERROR) return {};
		for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a != nullptr; a = a->Next)
		{
			if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
			const bool physical = a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211;
			for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
			{
				const auto* in = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
				const std::uint32_t ip = ntohl(in->sin_addr.s_addr);
				if (IsLoopback(ip) || (ip >> 16) == 0xA9FE || ip == 0) continue;   // loopback, 169.254 (no network)
				found.push_back({ { ip, static_cast<std::uint8_t>(std::min<ULONG>(u->OnLinkPrefixLength, 32)) }, physical });
			}
		}
		// Deterministic: wired / Wi-Fi first (a virtual switch or VPN is less likely what the
		// other players share), then by address.
		std::ranges::sort(found, [](const Found& a, const Found& b) {
			if (a.physical != b.physical) return a.physical;
			return a.address.ipv4 < b.address.ipv4;
		});
		std::vector<HostAddress> out;
		for (const Found& f : found)
			if (std::ranges::find(out, f.address) == out.end()) out.push_back(f.address);
		return out;
	}

	LobbyServerClient::~LobbyServerClient()
	{
		StopHosting();
		CloseSocket();
		if (m_winsock) WSACleanup();
	}

	void LobbyServerClient::SetServer(const std::string& address, std::uint16_t port)
	{
		std::uint32_t ip = 0;
		if (!address.empty() && port != 0)
		{
			in_addr parsed{};
			if (inet_pton(AF_INET, address.c_str(), &parsed) == 1) ip = ntohl(parsed.s_addr);
		}
		if (ip == m_serverIp && port == m_serverPort) return;
		m_serverIp = ip;
		m_serverPort = port;
		// A different server knows nothing yet: register again, ask again.
		m_registered = false;
		m_nextRegister = {};
		m_hostWaitingSince = {};
		m_outstanding = 0;
		m_nextList = {};
		m_browseWaitingSince = {};
		m_browseAnswered = false;
		m_failedLists = 0;
		m_lobbies.clear();
		m_total = 0;
		m_truncated = false;
		m_statusOutstanding = 0;
		m_nextStatus = {};
		m_monitorWaitingSince = {};
		m_monitorAnswered = false;
		m_hasStatus = false;
	}

	bool LobbyServerClient::OpenSocket()
	{
		if (m_socket != ~std::uintptr_t{ 0 }) return true;
		if (!m_winsock)
		{
			WSADATA data{};
			if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
			m_winsock = true;
		}
		const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (s == INVALID_SOCKET) return false;
		u_long nonBlocking = 1;
		ioctlsocket(s, FIONBIO, &nonBlocking);
		// A server that is down makes Windows report "connection reset" on the next read: that is
		// only "no answer" here.
		BOOL off = FALSE;
		DWORD unused = 0;
		WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &unused, nullptr, nullptr);
		sockaddr_in local{};
		local.sin_family = AF_INET;
		local.sin_addr.s_addr = htonl(INADDR_ANY);
		local.sin_port = 0;
		if (bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
		{
			closesocket(s);
			return false;
		}
		m_socket = static_cast<std::uintptr_t>(s);
		return true;
	}

	void LobbyServerClient::CloseSocket() noexcept
	{
		if (m_socket == ~std::uintptr_t{ 0 }) return;
		closesocket(static_cast<SOCKET>(m_socket));
		m_socket = ~std::uintptr_t{ 0 };
	}

	void LobbyServerClient::Send(std::span<const std::byte> bytes) noexcept
	{
		if (m_serverIp == 0 || !OpenSocket()) return;
		sockaddr_in to{};
		to.sin_family = AF_INET;
		to.sin_addr.s_addr = htonl(m_serverIp);
		to.sin_port = htons(m_serverPort);
		sendto(static_cast<SOCKET>(m_socket), reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0,
			reinterpret_cast<const sockaddr*>(&to), sizeof(to));   // fire and forget
	}

	// ---- host -----------------------------------------------------------------------------------

	void LobbyServerClient::Host(const RegisterMsg& registration)
	{
		const bool newLobby = !m_hosting || registration.lobby != m_registration.lobby || registration.ownerToken != m_registration.ownerToken;
		m_registration = registration;
		if (!newLobby) return;
		m_hosting = true;
		m_registered = false;
		m_idConflict = false;
		m_nextRegister = {};   // at once
		m_hostWaitingSince = {};
	}

	void LobbyServerClient::StopHosting(bool unregister) noexcept
	{
		if (!m_hosting) return;
		if (unregister && m_serverIp != 0)
		{
			std::array<std::byte, 64> buffer;
			ByteWriter w(buffer);
			if (Encode(w, UnregisterMsg{ .lobby = m_registration.lobby, .ownerToken = m_registration.ownerToken }))
			{
				Send(w.Written());
				Send(w.Written());   // twice: one may be lost; a duplicate is ignored
			}
		}
		m_hosting = false;
		m_registered = false;
		m_hostWaitingSince = {};
		if (Idle()) CloseSocket();
	}

	bool LobbyServerClient::TakeIdConflict() noexcept
	{
		const bool conflict = m_idConflict;
		m_idConflict = false;
		return conflict;
	}

	// ---- browser --------------------------------------------------------------------------------

	void LobbyServerClient::StartBrowsing(std::uint32_t protocolVersion)
	{
		if (m_browsing && protocolVersion == m_protocolVersion) return;
		m_browsing = true;
		m_protocolVersion = protocolVersion;
		m_outstanding = 0;
		m_nextList = {};
		m_browseWaitingSince = {};
		m_browseAnswered = false;
		m_failedLists = 0;
		m_networksAt = {};
	}

	void LobbyServerClient::StopBrowsing() noexcept
	{
		m_browsing = false;
		m_outstanding = 0;
		m_lobbies.clear();
		m_total = 0;
		m_truncated = false;
		if (Idle()) CloseSocket();
	}

	// ---- monitor --------------------------------------------------------------------------------

	void LobbyServerClient::StartMonitoring()
	{
		if (m_monitoring) return;
		m_monitoring = true;
		m_statusOutstanding = 0;
		m_nextStatus = {};
		m_monitorWaitingSince = {};
		m_monitorAnswered = false;
		m_hasStatus = false;
	}

	void LobbyServerClient::StopMonitoring() noexcept
	{
		m_monitoring = false;
		m_statusOutstanding = 0;
		m_monitorAnswered = false;
		m_hasStatus = false;
		if (Idle()) CloseSocket();
	}

	// ---- both -----------------------------------------------------------------------------------

	void LobbyServerClient::Pump(Clock::time_point now)
	{
		if (m_serverIp == 0 || Idle()) return;

		if (m_monitoring)
		{
			if (m_monitorWaitingSince == Clock::time_point{}) m_monitorWaitingSince = now;
			if (m_statusOutstanding != 0 && now - m_statusSince >= kListTimeout) m_statusOutstanding = 0;   // lost: ask again
			if (m_statusOutstanding == 0 && now >= m_nextStatus)
			{
				const StatusRequestMsg request{ .request = m_nextRequest++ };
				std::array<std::byte, 32> buffer;
				ByteWriter w(buffer);
				if (Encode(w, request))
				{
					Send(w.Written());
					m_statusOutstanding = request.request;
					m_statusSince = now;
				}
			}
		}

		if (m_hosting && now >= m_nextRegister)
		{
			m_registration.request = m_nextRequest++;
			std::array<std::byte, kMaxPacket> buffer;
			ByteWriter w(buffer);
			if (Encode(w, m_registration)) Send(w.Written());
			if (m_hostWaitingSince == Clock::time_point{}) m_hostWaitingSince = now;
			m_nextRegister = now + (m_registered ? kHeartbeat : kRegisterRetry);
		}

		if (m_browsing)
		{
			if (m_browseWaitingSince == Clock::time_point{}) m_browseWaitingSince = now;
			if (m_outstanding != 0 && now - m_outstandingSince >= kListTimeout)
			{
				m_outstanding = 0;   // lost: ask again
				if (m_failedLists < kListAttempts) ++m_failedLists;
			}
			if (m_outstanding == 0 && now >= m_nextList)
			{
				const ListRequestMsg request{ .request = m_nextRequest++, .protocolVersion = m_protocolVersion };
				std::array<std::byte, 64> buffer;
				ByteWriter w(buffer);
				if (Encode(w, request))
				{
					Send(w.Written());
					m_outstanding = request.request;
					m_outstandingSince = now;
				}
			}
			if (now - m_networksAt > std::chrono::seconds(5))
			{
				m_networks = LocalNetworks();
				m_networksAt = now;
			}
		}

		ReadReplies(now);

		if (m_monitoring && now - m_monitorWaitingSince >= kBrowseUnreachable) m_hasStatus = false;   // no longer true

		if (m_browsing && m_failedLists >= kListAttempts && !m_lobbies.empty())
		{
			m_lobbies.clear();   // the server stopped answering: its list is no longer true
			m_total = 0;
			m_truncated = false;
		}
	}

	void LobbyServerClient::ReadReplies(Clock::time_point now)
	{
		if (m_socket == ~std::uintptr_t{ 0 }) return;
		for (int i = 0; i < 32; ++i)
		{
			std::array<std::byte, kMaxPacket + 64> buffer;
			sockaddr_in from{};
			int fromLength = sizeof(from);
			const int n = recvfrom(static_cast<SOCKET>(m_socket), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
				reinterpret_cast<sockaddr*>(&from), &fromLength);
			if (n <= 0) break;
			if (ntohl(from.sin_addr.s_addr) != m_serverIp || ntohs(from.sin_port) != m_serverPort) continue;   // only our server
			ByteReader r(std::span<const std::byte>(buffer.data(), static_cast<std::size_t>(n)));
			Kind kind{};
			if (!ReadHeader(r, kind)) continue;
			if (kind == Kind::Registered)
			{
				RegisteredMsg reply;
				if (!Decode(r, reply) || !m_hosting || reply.lobby != m_registration.lobby) continue;
				m_lastHostReply = now;
				m_hostWaitingSince = {};
				if (reply.result == RegisterResult::Ok)
				{
					if (!m_registered) m_nextRegister = now + kHeartbeat;
					m_registered = true;
				}
				else
				{
					m_registered = false;
					if (reply.result == RegisterResult::IdInUse) m_idConflict = true;
					m_nextRegister = now + kHeartbeat;   // full / refused: try again later, quietly
				}
			}
			else if (kind == Kind::StatusReply)
			{
				StatusReplyMsg reply;
				if (!Decode(r, reply) || !m_monitoring || reply.request != m_statusOutstanding) continue;
				m_statusOutstanding = 0;
				m_nextStatus = now + kListInterval;
				m_monitorWaitingSince = now;
				m_monitorAnswered = true;
				m_hasStatus = true;
				m_status = reply;
			}
			else if (kind == Kind::ListReply)
			{
				ListReplyMsg reply;
				if (!Decode(r, reply) || !m_browsing || reply.request != m_outstanding) continue;   // late, duplicate or stale
				OnListReply(reply, now);
			}
		}
	}

	void LobbyServerClient::OnListReply(const ListReplyMsg& reply, Clock::time_point now)
	{
		m_outstanding = 0;
		m_nextList = now + kListInterval;
		m_browseWaitingSince = now;
		m_browseAnswered = true;
		m_failedLists = 0;
		m_total = reply.total;
		m_truncated = reply.truncated;
		if (m_networks.empty()) m_networks = LocalNetworks();
		m_lobbies.clear();
		for (const ListEntry& e : reply.entries)
		{
			LobbyInfo info;
			info.advert.instance = e.lobby;
			info.advert.protocolVersion = m_protocolVersion;   // the server listed only this version
			info.advert.gamePort = e.gamePort;
			info.advert.players = e.players;
			info.advert.maxPlayers = e.maxPlayers;
			info.advert.open = e.open;
			info.advert.name = e.name;
			info.advert.status = e.status;
			info.address = AddressText(SelectHostAddress(e, m_networks, m_serverIp));
			info.lastHeard = now - std::chrono::milliseconds(e.ageMs);
			info.sources = kLobbyFromServer;
			m_lobbies.push_back(std::move(info));
		}
	}

	LobbyServerStatus LobbyServerClient::Status(Clock::time_point now) const noexcept
	{
		if (m_serverIp == 0 || Idle()) return LobbyServerStatus::Off;
		bool waiting = false, answered = false, lost = false;
		if (m_monitoring)
		{
			answered = answered || m_monitorAnswered;
			if (m_monitorWaitingSince != Clock::time_point{} && now - m_monitorWaitingSince >= kBrowseUnreachable) lost = true;
		}
		if (m_browsing)
		{
			answered = answered || m_browseAnswered;
			if (m_failedLists >= kListAttempts) lost = true;
		}
		if (m_hosting)
		{
			answered = answered || m_lastHostReply != Clock::time_point{};
			if (m_hostWaitingSince != Clock::time_point{})
			{
				waiting = true;
				if (now - m_hostWaitingSince >= kHostUnreachable) lost = true;
			}
		}
		if (lost) return LobbyServerStatus::Unreachable;
		if (!answered || (waiting && !m_registered && !m_browsing && !m_monitoring)) return LobbyServerStatus::Connecting;
		return LobbyServerStatus::Online;
	}
}
