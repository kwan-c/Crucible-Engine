/*************************************************************************
* \file:         NetworkSettings.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       2.2
* \brief
*                NetworkSettings: mode names, conversion from EngineConfig,
*                validation (see the header).
*
* \history
*    -v1.0       Validate, ApplyArguments, process arguments, CRUCIBLE_NET
*    -v2.0       internal configuration only: FromEngineConfig, ParseNetMode,
*                IP address validation; external parsing removed
*    -v2.1       Validate checks the network simulation's ranges
*    -v2.2       lobby discovery mode and connection server address/port: config, validation
*
**************************************************************************/
#include "Network/NetworkSettings.h"

#include "Configuration/EngineConfig.h"
#include "Network/GnsTransport.h"   // IsIpAddress
#include "Network/Protocol.h"       // IsValidPlayerName

#include <algorithm>
#include <cctype>

namespace Crucible
{
	const char* ToString(NetMode mode) noexcept
	{
		switch (mode)
		{
		case NetMode::Host:   return "host";
		case NetMode::Client: return "client";
		default:              return "offline";
		}
	}

	const char* ToString(LobbyDiscovery mode) noexcept
	{
		switch (mode)
		{
		case LobbyDiscovery::Server: return "server";
		case LobbyDiscovery::Lan:    return "lan";
		default:                     return "auto";
		}
	}

	bool ParseLobbyDiscovery(std::string_view text, LobbyDiscovery& out) noexcept
	{
		const auto equals = [text](std::string_view name) {
			return std::ranges::equal(text, name, [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == static_cast<unsigned char>(b);
			});
		};
		if (equals("auto"))   { out = LobbyDiscovery::Auto;   return true; }
		if (equals("server")) { out = LobbyDiscovery::Server; return true; }
		if (equals("lan"))    { out = LobbyDiscovery::Lan;    return true; }
		return false;
	}

	bool IsIpv4Address(std::string_view text) noexcept
	{
		int parts = 0, value = -1, digits = 0;
		for (const char c : text)
		{
			if (c == '.')
			{
				if (value < 0 || ++parts > 3) return false;
				value = -1;
				digits = 0;
				continue;
			}
			if (c < '0' || c > '9' || ++digits > 3) return false;
			value = (value < 0 ? 0 : value * 10) + (c - '0');
			if (value > 255) return false;
		}
		return parts == 3 && value >= 0;
	}

	bool ParseNetMode(std::string_view text, NetMode& out) noexcept
	{
		const auto equals = [text](std::string_view name) {
			return std::ranges::equal(text, name, [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == static_cast<unsigned char>(b);
			});
		};
		if (equals("offline")) { out = NetMode::Offline; return true; }
		if (equals("host"))    { out = NetMode::Host;    return true; }
		if (equals("client"))  { out = NetMode::Client;  return true; }
		return false;
	}

	NetworkSettings FromEngineConfig(const NetworkConfig& config, std::string& warning)
	{
		warning.clear();
		NetworkSettings settings;
		if (!ParseNetMode(config.mode, settings.mode))
		{
			settings.mode = NetMode::Offline;
			warning = "network.mode '" + config.mode + "' is not offline, host or client; using offline";
		}
		settings.listenAddress = config.listenAddress;
		settings.hostPort = config.hostPort;
		settings.connectAddress = config.connectAddress;
		settings.connectPort = config.connectPort;
		settings.maxClients = config.maxClients;
		settings.lobbyServerAddress = config.lobbyServerAddress;
		settings.lobbyServerPort = config.lobbyServerPort;
		settings.discoveryPort = config.discoveryPort;
		if (!ParseLobbyDiscovery(config.lobbyDiscovery, settings.lobbyDiscovery))
		{
			settings.lobbyDiscovery = LobbyDiscovery::Auto;
			warning += (warning.empty() ? "" : "; ") + std::string("network.lobbyDiscovery '") + config.lobbyDiscovery +
				"' is not auto, server or lan; using auto";
		}
		if (!settings.lobbyServerAddress.empty() && !IsIpv4Address(settings.lobbyServerAddress))
		{
			warning += (warning.empty() ? "" : "; ") + std::string("network.lobbyServerAddress '") + settings.lobbyServerAddress +
				"' is not an IPv4 address; no connection server";
			settings.lobbyServerAddress.clear();
		}
		return settings;
	}

	bool Validate(const NetworkSettings& settings, std::string& error)
	{
		if (settings.mode == NetMode::Offline) return true;
		if (settings.maxClients < 1 || settings.maxClients > Net::kMaxClients) { error = "Max clients must be 1 to " + std::to_string(Net::kMaxClients) + "."; return false; }
		if (!IsValidPlayerName(settings.playerName)) { error = "The player name must be at most 32 bytes, without control characters."; return false; }
		if (!settings.lobbyServerAddress.empty() && !IsIpv4Address(settings.lobbyServerAddress))
		{
			error = "Connection server address '" + settings.lobbyServerAddress + "' is not an IPv4 address (for example 192.168.0.5).";
			return false;
		}
		if (!settings.lobbyServerAddress.empty() && settings.lobbyServerPort == 0) { error = "Connection server port must be 1 to 65535."; return false; }
		if (settings.joinTimeoutMs == 0) { error = "The join timeout must be above 0."; return false; }
		if (settings.simulation.extraRoundTripMs > NetworkSimulation::kMaxExtraRoundTripMs)
		{
			error = "Simulated latency must be 0 to " + std::to_string(NetworkSimulation::kMaxExtraRoundTripMs) + " ms.";
			return false;
		}
		// Written as a negated range test so NaN is refused too.
		if (!(settings.simulation.packetLossPercent >= 0.f && settings.simulation.packetLossPercent <= NetworkSimulation::kMaxPacketLossPercent))
		{
			error = "Simulated packet loss must be 0 to " + std::to_string(static_cast<int>(NetworkSimulation::kMaxPacketLossPercent)) + " %.";
			return false;
		}
		if (settings.mode == NetMode::Host)
		{
			if (settings.hostPort == 0) { error = "Host port must be 1 to 65535."; return false; }
			if (!settings.listenAddress.empty() && !GnsTransport::IsIpAddress(settings.listenAddress))
			{
				error = "Listen address '" + settings.listenAddress + "' is not an IP address (leave it empty for all interfaces).";
				return false;
			}
		}
		else
		{
			if (settings.connectAddress.empty()) { error = "Enter the host's IP address (127.0.0.1 for this computer)."; return false; }
			if (!GnsTransport::IsIpAddress(settings.connectAddress))
			{
				error = "Host address '" + settings.connectAddress + "' is not an IP address (for example 192.168.1.20).";
				return false;
			}
			if (settings.connectPort == 0) { error = "Host port must be 1 to 65535."; return false; }
		}
		return true;
	}

	ListenSpec ToListenSpec(const NetworkSettings& settings)
	{
		return ListenSpec{ .how = IpEndpoint{ .address = settings.listenAddress, .port = settings.hostPort }, .timeouts = settings.timeouts };
	}

	ConnectSpec ToConnectSpec(const NetworkSettings& settings)
	{
		return ConnectSpec{ .to = IpEndpoint{ .address = settings.connectAddress, .port = settings.connectPort }, .timeouts = settings.timeouts };
	}
}
