/*************************************************************************
* \file:         main.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          21 November 2026
* \version       1.1
* \brief
*                CrucibleConnectionServer: the LAN connection server. Hosts
*                register their lobbies here; clients ask it for the list,
*                then join the host directly. It never takes part in a game.
*
* \details
*                CrucibleConnectionServer.exe [--port 27018] [--lifetime 7000]
*                  --port      UDP port to listen on (default 27018)
*                  --lifetime  ms without a heartbeat before a lobby is
*                              removed (default 7000; 3000..60000)
*
*                Prints the addresses it can be reached at (put one in the
*                game's network "lobbyServerAddress"), then a line for every
*                lobby added, removed or expired. Ctrl+C stops it; nothing is
*                saved, and hosts register again on their next heartbeat
*                after a restart. Answers StatusRequest (uptime, lobbies,
*                process id) for the editor's Connection Server tab, which
*                can also start and stop it.
*
* \history
*    -v1.0       LAN connection server
*    -v1.1       answers StatusRequest (editor monitoring)
*
**************************************************************************/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)   // mstcpip.h hides it on some SDK settings
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "LobbyRegistry.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

using namespace Crucible;
using namespace Crucible::LobbyServer;

namespace
{
	std::atomic<bool> g_stop{ false };

	BOOL WINAPI OnConsoleEvent(DWORD)
	{
		g_stop = true;
		return TRUE;
	}

	void Log(const char* format, auto... args)
	{
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		localtime_s(&local, &now);
		char stamp[16];
		std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
		std::printf("[%s] ", stamp);
		std::printf(format, args...);
		std::printf("\n");
		std::fflush(stdout);
	}

	// Every IPv4 address of an adapter that is up (not loopback): where clients can reach us.
	void PrintAddresses(std::uint16_t port)
	{
		ULONG size = 16 * 1024;
		std::vector<unsigned char> buffer(size);
		auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
		const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
		if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) == ERROR_BUFFER_OVERFLOW)
		{
			buffer.resize(size);
			list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
		}
		if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) != NO_ERROR) return;
		std::printf("Reachable at (put one in EngineConfig.json \"lobbyServerAddress\" or the Network tab):\n");
		for (auto* a = list; a != nullptr; a = a->Next)
		{
			if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
			for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
			{
				const auto* in = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
				const std::uint32_t ip = ntohl(in->sin_addr.s_addr);
				if ((ip >> 16) == 0xA9FE) continue;   // 169.254: no network
				std::printf("    %s:%u   (%ls)\n", AddressText(ip).c_str(), port, a->FriendlyName);
			}
		}
		std::printf("    127.0.0.1:%u   (this PC only)\n", port);
	}

	void Send(SOCKET s, const sockaddr_in& to, const ByteWriter& w)
	{
		sendto(s, reinterpret_cast<const char*>(w.Written().data()), static_cast<int>(w.Size()), 0,
			reinterpret_cast<const sockaddr*>(&to), sizeof(to));
	}

	const char* ResultName(RegisterResult r)
	{
		switch (r)
		{
		case RegisterResult::Ok: return "ok";
		case RegisterResult::IdInUse: return "id in use (different owner token)";
		case RegisterResult::ServerFull: return "server full";
		case RegisterResult::TooManyFromAddress: return "too many lobbies from this address";
		default: return "bad request";
		}
	}
}

int main(int argc, char** argv)
{
	std::uint16_t port = kDefaultPort;
	long long lifetimeMs = kDefaultLifetime.count();
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--port" && i + 1 < argc)
		{
			const long value = std::strtol(argv[++i], nullptr, 10);
			if (value < 1 || value > 65535) { std::fprintf(stderr, "--port must be 1..65535\n"); return 2; }
			port = static_cast<std::uint16_t>(value);
		}
		else if (arg == "--lifetime" && i + 1 < argc)
		{
			lifetimeMs = std::strtoll(argv[++i], nullptr, 10);
			if (lifetimeMs < 3000 || lifetimeMs > 60000) { std::fprintf(stderr, "--lifetime must be 3000..60000 (ms)\n"); return 2; }
		}
		else
		{
			std::printf("CrucibleConnectionServer [--port %u] [--lifetime %lld]\n", kDefaultPort, static_cast<long long>(kDefaultLifetime.count()));
			return arg == "--help" || arg == "-h" ? 0 : 2;
		}
	}

	WSADATA data{};
	if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { std::fprintf(stderr, "Windows sockets did not start\n"); return 1; }
	const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) { std::fprintf(stderr, "no UDP socket\n"); return 1; }
	// A client that went away makes Windows report "connection reset" on later reads: not an error here.
	BOOL off = FALSE;
	DWORD unused = 0;
	WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &unused, nullptr, nullptr);
	sockaddr_in local{};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(port);
	if (bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
	{
		std::fprintf(stderr, "UDP port %u is in use (is another connection server running?)\n", port);
		closesocket(s);
		WSACleanup();
		return 1;
	}
	SetConsoleCtrlHandler(OnConsoleEvent, TRUE);

	LobbyRegistry registry{ std::chrono::milliseconds(lifetimeMs) };
	const auto started = LobbyRegistry::Clock::now();
	std::printf("Crucible connection server (LAN) - protocol format %u, UDP port %u, lobbies expire after %lld ms\n",
		kFormat, port, lifetimeMs);
	PrintAddresses(port);
	std::printf("Ctrl+C to stop.\n");
	std::fflush(stdout);

	std::array<std::byte, kMaxPacket + 64> in;
	std::array<std::byte, kMaxPacket> out;
	while (!g_stop)
	{
		fd_set readable;
		FD_ZERO(&readable);
		FD_SET(s, &readable);
		timeval wait{ 0, 250000 };   // wake up 4 times a second to expire lobbies
		select(0, &readable, nullptr, nullptr, &wait);

		for (int i = 0; i < 256; ++i)   // a burst is handled in one go; a flood cannot starve expiry
		{
			u_long waiting = 0;
			if (ioctlsocket(s, FIONREAD, &waiting) != 0 || waiting == 0) break;
			sockaddr_in from{};
			int fromLength = sizeof(from);
			const int n = recvfrom(s, reinterpret_cast<char*>(in.data()), static_cast<int>(in.size()), 0,
				reinterpret_cast<sockaddr*>(&from), &fromLength);
			if (n <= 0) continue;
			const std::uint32_t source = ntohl(from.sin_addr.s_addr);
			const auto now = LobbyRegistry::Clock::now();
			ByteReader r(std::span<const std::byte>(in.data(), static_cast<std::size_t>(n)));
			Kind kind{};
			if (!ReadHeader(r, kind)) continue;   // not ours, or another format version
			ByteWriter w(out);
			switch (kind)
			{
			case Kind::Register:
			{
				RegisterMsg message;
				if (!Decode(r, message)) break;
				bool added = false;
				const RegisteredMsg reply = registry.Register(message, source, now, added);
				if (added)
					Log("+ lobby %016llX '%s' from %s, game port %u, %u/%u players (%zu listed)",
						static_cast<unsigned long long>(message.lobby), message.name.c_str(), AddressText(source).c_str(),
						message.gamePort, message.players, message.maxPlayers, registry.Count());
				else if (reply.result != RegisterResult::Ok)
					Log("! register %016llX from %s refused: %s", static_cast<unsigned long long>(message.lobby),
						AddressText(source).c_str(), ResultName(reply.result));
				if (Encode(w, reply)) Send(s, from, w);
				break;
			}
			case Kind::Unregister:
			{
				UnregisterMsg message;
				if (!Decode(r, message)) break;
				if (registry.Unregister(message))
					Log("- lobby %016llX removed by its host (%zu listed)", static_cast<unsigned long long>(message.lobby), registry.Count());
				break;
			}
			case Kind::ListRequest:
			{
				ListRequestMsg message;
				if (!Decode(r, message)) break;
				if (Encode(w, registry.List(message, now))) Send(s, from, w);
				break;
			}
			case Kind::StatusRequest:
			{
				StatusRequestMsg message;
				if (!Decode(r, message)) break;
				StatusReplyMsg reply;
				reply.request = message.request;
				reply.uptimeSeconds = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(now - started).count());
				reply.lobbies = static_cast<std::uint16_t>(registry.Count());
				reply.maxLobbies = static_cast<std::uint16_t>(LobbyRegistry::kMaxLobbies);
				reply.lifetimeMs = static_cast<std::uint32_t>(lifetimeMs);
				reply.processId = GetCurrentProcessId();
				if (Encode(w, reply)) Send(s, from, w);
				break;
			}
			default:
				break;   // Registered / ListReply / StatusReply are not for us; reserved kinds are not implemented
			}
		}

		const std::size_t expired = registry.Expire(LobbyRegistry::Clock::now());
		if (expired > 0) Log("- %zu lobby(s) expired: no heartbeat for %lld ms (%zu listed)", expired, lifetimeMs, registry.Count());
	}

	std::printf("Stopping.\n");
	closesocket(s);
	WSACleanup();
	return 0;
}
