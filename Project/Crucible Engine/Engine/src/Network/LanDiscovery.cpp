/*************************************************************************
* \file:         LanDiscovery.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          21 November 2026
* \version       1.0
* \brief
*                LanDiscovery: UDP broadcast queries and replies (see the
*                header).
*
* \history
*    -v1.0       hosts answer, browsers list
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

#include "Network/LanDiscovery.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "Network/ByteStream.h"

#pragma comment(lib, "ws2_32.lib")

namespace Crucible
{
	namespace
	{
		constexpr std::array<std::byte, 4> kMagic{ std::byte{ 'C' }, std::byte{ 'R' }, std::byte{ 'L' }, std::byte{ 'D' } };
		constexpr std::uint8_t kQuery = 1;
		constexpr std::uint8_t kReply = 2;
		constexpr std::uint8_t kFormat = 1;
		constexpr std::size_t  kMaxName = 32;
		constexpr std::size_t  kMaxStatus = 64;
		constexpr std::size_t  kMaxPacket = 4 + 1 + 1 + 8 + 4 + 2 + 1 + 1 + 1 + 1 + kMaxName + 1 + kMaxStatus;   // 123

		bool StartWinsock(bool& started, std::string& error)
		{
			if (started) return true;
			WSADATA data{};
			if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { error = "Windows sockets did not start"; return false; }
			started = true;
			return true;
		}

		void CloseSocket(std::uintptr_t& socket) noexcept
		{
			if (socket == ~std::uintptr_t{ 0 }) return;
			closesocket(static_cast<SOCKET>(socket));
			socket = ~std::uintptr_t{ 0 };
		}

		// A non-blocking UDP socket bound to `port` on every interface (0 = any free port).
		std::uintptr_t OpenUdp(std::uint16_t port, bool broadcast, std::string& error)
		{
			const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (s == INVALID_SOCKET) { error = "no UDP socket"; return ~std::uintptr_t{ 0 }; }
			const BOOL yes = TRUE;
			// Several hosts on one PC may listen on the discovery port; each gets the broadcasts.
			if (port != 0) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
			if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
			u_long nonBlocking = 1;
			ioctlsocket(s, FIONBIO, &nonBlocking);
			sockaddr_in local{};
			local.sin_family = AF_INET;
			local.sin_addr.s_addr = htonl(INADDR_ANY);
			local.sin_port = htons(port);
			if (bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
			{
				error = "the discovery port " + std::to_string(port) + " could not be opened";
				closesocket(s);
				return ~std::uintptr_t{ 0 };
			}
			return static_cast<std::uintptr_t>(s);
		}

		void WriteHeader(ByteWriter& w, std::uint8_t kind)
		{
			w.Bytes(kMagic);
			w.U8(kind);
			w.U8(kFormat);
		}

		// Magic, kind and format; false for anything that is not ours.
		bool ReadHeader(ByteReader& r, std::uint8_t& kind)
		{
			const std::span<const std::byte> magic = r.Sub(4);
			kind = r.U8();
			const std::uint8_t format = r.U8();
			return r.Ok() && std::equal(magic.begin(), magic.end(), kMagic.begin()) && format == kFormat;
		}

		void WriteText(ByteWriter& w, const std::string& text, std::size_t limit)
		{
			const std::size_t n = std::min(text.size(), limit);
			w.U8(static_cast<std::uint8_t>(n));
			w.Bytes(std::as_bytes(std::span(text.data(), n)));
		}

		bool ReadText(ByteReader& r, std::string& out, std::size_t limit)
		{
			const std::uint8_t n = r.U8();
			if (!r.Ok() || n > limit) return false;
			const std::span<const std::byte> bytes = r.Sub(n);
			if (!r.Ok()) return false;
			out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			// Shown on screen: no control characters.
			for (char& c : out)
				if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = '?';
			return true;
		}

		bool IsLoopback(const std::string& address) { return address.rfind("127.", 0) == 0; }
	}

	LanDiscovery::~LanDiscovery()
	{
		StopAdvertising();
		StopBrowsing();
		if (m_winsock) WSACleanup();
	}

	bool LanDiscovery::Advertise(std::uint16_t port, const LobbyAdvert& advert, std::string& error)
	{
		if (port == 0) { StopAdvertising(); return true; }
		m_advert = advert;
		if (m_hostSocket != kNoSocket && m_hostPort == port) return true;
		StopAdvertising();
		if (!StartWinsock(m_winsock, error)) return false;
		m_hostSocket = OpenUdp(port, false, error);
		if (m_hostSocket == kNoSocket) return false;
		m_hostPort = port;
		return true;
	}

	void LanDiscovery::StopAdvertising() noexcept
	{
		CloseSocket(m_hostSocket);
		m_hostPort = 0;
	}

	bool LanDiscovery::StartBrowsing(std::uint16_t port, std::string& error)
	{
		if (port == 0) { error = "LAN discovery is off (discovery port 0)"; return false; }
		if (m_browseSocket != kNoSocket && m_browsePort == port) return true;
		StopBrowsing();
		if (!StartWinsock(m_winsock, error)) return false;
		m_browseSocket = OpenUdp(0, true, error);
		if (m_browseSocket == kNoSocket) return false;
		m_browsePort = port;
		m_nextQuery = {};   // at once
		return true;
	}

	void LanDiscovery::StopBrowsing() noexcept
	{
		CloseSocket(m_browseSocket);
		m_browsePort = 0;
		m_lobbies.clear();
	}

	void LanDiscovery::Pump(std::chrono::steady_clock::time_point now)
	{
		if (m_hostSocket != kNoSocket) AnswerQueries();
		if (m_browseSocket == kNoSocket) return;

		if (now >= m_nextQuery)
		{
			m_nextQuery = now + kQueryInterval;
			std::array<std::byte, 8> buffer;
			ByteWriter w(buffer);
			WriteHeader(w, kQuery);
			sockaddr_in to{};
			to.sin_family = AF_INET;
			to.sin_port = htons(m_browsePort);
			// The whole local network, and this PC itself (a broadcast may not come back to it).
			for (const ULONG address : { htonl(INADDR_BROADCAST), htonl(INADDR_LOOPBACK) })
			{
				to.sin_addr.s_addr = address;
				sendto(static_cast<SOCKET>(m_browseSocket), reinterpret_cast<const char*>(w.Written().data()),
					static_cast<int>(w.Size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
			}
		}
		ReadReplies(now);
		std::erase_if(m_lobbies, [&](const LobbyInfo& lobby) { return now - lobby.lastHeard > kLobbyExpiry; });
	}

	void LanDiscovery::AnswerQueries()
	{
		std::array<std::byte, kMaxPacket> reply;
		ByteWriter w(reply);
		WriteHeader(w, kReply);
		w.U64(m_advert.instance);
		w.U32(m_advert.protocolVersion);
		w.U16(m_advert.gamePort);
		w.U8(m_advert.players);
		w.U8(m_advert.maxPlayers);
		w.U8(m_advert.open ? 1 : 0);
		WriteText(w, m_advert.name, kMaxName);
		WriteText(w, m_advert.status, kMaxStatus);
		if (!w.Ok()) return;

		// At most a few dozen a frame: a flood cannot keep the host here.
		for (int i = 0; i < 32; ++i)
		{
			std::array<std::byte, 64> buffer;
			sockaddr_in from{};
			int fromLength = sizeof(from);
			const int n = recvfrom(static_cast<SOCKET>(m_hostSocket), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()),
				0, reinterpret_cast<sockaddr*>(&from), &fromLength);
			if (n <= 0) break;   // nothing waiting (or an error a UDP socket shrugs off)
			ByteReader r(std::span<const std::byte>(buffer.data(), static_cast<std::size_t>(n)));
			std::uint8_t kind = 0;
			if (!ReadHeader(r, kind) || kind != kQuery) continue;
			sendto(static_cast<SOCKET>(m_hostSocket), reinterpret_cast<const char*>(w.Written().data()), static_cast<int>(w.Size()), 0,
				reinterpret_cast<const sockaddr*>(&from), fromLength);
		}
	}

	void LanDiscovery::ReadReplies(std::chrono::steady_clock::time_point now)
	{
		for (int i = 0; i < 64; ++i)
		{
			std::array<std::byte, kMaxPacket + 16> buffer;
			sockaddr_in from{};
			int fromLength = sizeof(from);
			const int n = recvfrom(static_cast<SOCKET>(m_browseSocket), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()),
				0, reinterpret_cast<sockaddr*>(&from), &fromLength);
			if (n <= 0) break;
			ByteReader r(std::span<const std::byte>(buffer.data(), static_cast<std::size_t>(n)));
			std::uint8_t kind = 0;
			if (!ReadHeader(r, kind) || kind != kReply) continue;
			LobbyAdvert advert;
			advert.instance = r.U64();
			advert.protocolVersion = r.U32();
			advert.gamePort = r.U16();
			advert.players = r.U8();
			advert.maxPlayers = r.U8();
			advert.open = (r.U8() & 1u) != 0;
			if (!r.Ok() || !ReadText(r, advert.name, kMaxName) || !ReadText(r, advert.status, kMaxStatus) || !r.AtEnd()) continue;
			if (advert.instance == 0 || advert.gamePort == 0) continue;

			char text[INET_ADDRSTRLEN] = {};
			if (inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text)) == nullptr) continue;
			const std::string address = text;

			const auto known = std::ranges::find_if(m_lobbies, [&](const LobbyInfo& l) { return l.advert.instance == advert.instance; });
			if (known == m_lobbies.end())
			{
				if (m_lobbies.size() >= kMaxLobbies) continue;
				m_lobbies.push_back(LobbyInfo{ .advert = std::move(advert), .address = address, .lastHeard = now });
				continue;
			}
			known->advert = std::move(advert);
			known->lastHeard = now;
			// Reached as this PC and over the network: join it locally (no firewall in the way).
			if (IsLoopback(address)) known->address = address;
		}
	}
}
