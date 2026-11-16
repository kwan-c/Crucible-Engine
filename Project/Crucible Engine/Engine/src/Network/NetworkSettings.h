/*************************************************************************
* \file:         NetworkSettings.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       2.1
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
*                Where the values come from (the only two sources):
*                  1. EngineConfig.json, "network" section: the team's shared
*                     defaults, loaded at startup (EngineConfig::network,
*                     turned into NetworkSettings by FromEngineConfig).
*                  2. The editor's Network panel: this editor instance's own
*                     choice, kept in memory (NetworkSession::SetSettings) and
*                     never written back to EngineConfig.json. Two editors
*                     started from the same project can therefore be Host and
*                     Client at the same time.
*                No environment variable or command-line argument configures
*                networking.
*
* \history
*    -v1.0       NetMode, NetworkSettings, Validate, argument parsing
*    -v1.1       joinTimeoutMs also bounds the client's wait for JoinAccepted
*    -v2.0       configuration is internal only: environment and command-
*                line parsing removed; FromEngineConfig, ParseNetMode;
*                Validate checks IP addresses
*    -v2.1       simulation: test latency and packet loss, set only from the
*                editor's Network panel (Networking Phase 0)
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "EngineExport.h"
#include "Network/ConnectionSpec.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	struct NetworkConfig;   // Configuration/EngineConfig.h

	/// What this instance does when Play starts. (NetRole says what a RUNNING session is;
	/// NetMode is what the user asked for.)
	enum class NetMode : std::uint8_t { Offline, Host, Client };

	/// "offline", "host" or "client".
	ENGINE_API const char* ToString(NetMode mode) noexcept;

	/// Reads "offline" / "host" / "client" (any letter case). False for anything else.
	ENGINE_API bool ParseNetMode(std::string_view text, NetMode& out) noexcept;

	/// The defaults match EngineConfig.h's NetworkConfig; at runtime the values always come
	/// from there (FromEngineConfig) or from the editor's Network panel.
	struct NetworkSettings
	{
		NetMode       mode = NetMode::Offline;

		// Host
		std::string   listenAddress;                  // empty = every local interface
		std::uint16_t hostPort = kDefaultNetPort;     // 27020
		std::uint32_t maxClients = Net::kMaxClients;  // 1..3 (the hosting player is the 4th)

		// Client
		std::string   connectAddress = "127.0.0.1";   // the host's IP address
		std::uint16_t connectPort = kDefaultNetPort;

		// Both
		std::uint32_t      joinTimeoutMs = 10000;     // the join handshake's deadline, both sides: a host waits this
		                                              // long for JoinRequest, a client for JoinAccepted
		std::string        playerName = "Player";     // sent in JoinRequest (at most 32 bytes)
		ConnectionTimeouts timeouts;                  // 0 = GNS defaults (10 s)

		// Testing only: bad network conditions for this whole instance while its session runs
		// (cleared when it stops). Never read from EngineConfig.json: only the editor's Network
		// panel sets it, so a shipped game always plays on the real network.
		NetworkSimulation  simulation;
	};

	/// The shared defaults from EngineConfig.json. An unknown mode text becomes Offline and is
	/// reported in `warning` (empty when everything was understood).
	ENGINE_API NetworkSettings FromEngineConfig(const NetworkConfig& config, std::string& warning);

	/// False with a reason a person can act on if these settings cannot start a session in
	/// their mode (Offline is always valid). Addresses must be IP literals, not host names.
	ENGINE_API bool Validate(const NetworkSettings& settings, std::string& error);

	ENGINE_API ListenSpec  ToListenSpec(const NetworkSettings& settings);
	ENGINE_API ConnectSpec ToConnectSpec(const NetworkSettings& settings);
}
