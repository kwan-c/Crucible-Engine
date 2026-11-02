/*************************************************************************
* \file:         NetworkSettings.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.1
* \brief
*                What one engine instance should do about networking when
*                Play starts: stay Offline, Host a session, or join one as
*                a Client, and with which addresses and ports.
*
* \details
*                A plain value type. Nothing in the networking code holds
*                an address or port of its own: everything comes from here,
*                so each running instance can be configured differently.
*
*                Where the values come from (lowest priority first):
*                  1. The defaults below (Offline, port 27020, every
*                     interface).
*                  2. The CRUCIBLE_NET environment variable, e.g.
*                     CRUCIBLE_NET=--net-mode host --net-port 27020
*                     (per process: two editors started from two shells
*                     can differ, and it needs no command-line support
*                     from the executable).
*                  3. The command line, e.g.
*                     Mekanomachia.exe --net-mode client --net-connect 192.168.1.20:27020
*                Later (not in Phase 4): saved per-app settings and a small
*                editor panel write the same struct through
*                NetworkSession::SetSettings, so pressing Play is enough.
*
*                Flags (everything not starting with --net- is ignored):
*                  --net-mode offline|host|client
*                  --net-port N          the port a host listens on
*                  --net-listen IP       a host's local address (default: all)
*                  --net-connect IP[:N]  the host a client joins ([::1]:N for IPv6)
*                  --net-name NAME       the player name sent when joining
*                  --net-max-clients N   1..3 clients a host accepts
*
* \history
*    -v1.0       NetMode, NetworkSettings, Validate, argument parsing
*    -v1.1       joinTimeoutMs also bounds the client's wait for JoinAccepted
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "EngineExport.h"
#include "Network/ConnectionSpec.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	/// What this instance does when Play starts. (NetRole says what a RUNNING session is;
	/// NetMode is what the user asked for.)
	enum class NetMode : std::uint8_t { Offline, Host, Client };

	ENGINE_API const char* ToString(NetMode mode) noexcept;

	struct NetworkSettings
	{
		NetMode       mode = NetMode::Offline;

		// Host
		std::string   listenAddress;                  // empty = every local interface
		std::uint16_t hostPort = kDefaultNetPort;     // 27020
		std::uint32_t maxClients = Net::kMaxClients;  // 1..3 (the hosting player is the 4th)
		std::uint32_t joinTimeoutMs = 10000;          // the join handshake's deadline, both sides: a host waits this
		                                              // long for JoinRequest, a client for JoinAccepted

		// Client
		std::string   connectAddress;                 // the host's IP address
		std::uint16_t connectPort = kDefaultNetPort;

		// Both
		std::string        playerName = "Player";     // sent in JoinRequest (at most 32 bytes)
		ConnectionTimeouts timeouts;                  // 0 = GNS defaults (10 s)
	};

	/// False with a reason if these settings cannot start a session in their mode
	/// (Offline is always valid).
	ENGINE_API bool Validate(const NetworkSettings& settings, std::string& error);

	/// Applies every --net-* flag in `args` to `settings`; other arguments are ignored.
	/// On a bad flag or value returns false with a reason and leaves `settings` unchanged.
	ENGINE_API bool ApplyArguments(std::span<const std::string> args, NetworkSettings& settings, std::string& error);

	/// Splits "a b  c" into words (for the CRUCIBLE_NET environment variable). No quoting:
	/// addresses, ports and names in these flags never contain spaces.
	ENGINE_API std::vector<std::string> SplitArguments(std::string_view text);

	/// This process's command-line arguments, without the program name, as UTF-8. Read from
	/// the operating system, so it works whatever the executable's main() does with them.
	ENGINE_API std::vector<std::string> ProcessArguments();

	/// The settings an instance starts with: defaults, then CRUCIBLE_NET, then the command line.
	/// A bad override is reported in `error` and skipped (the earlier layers stay in force).
	ENGINE_API NetworkSettings ResolveStartupSettings(std::string& error);

	ENGINE_API ListenSpec  ToListenSpec(const NetworkSettings& settings);
	ENGINE_API ConnectSpec ToConnectSpec(const NetworkSettings& settings);
}
