/*************************************************************************
* \file:         NetworkSettings.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Validation and parsing for NetworkSettings (see the header
*                for the flags and where values come from).
*
* \history
*    -v1.0       Validate, ApplyArguments, process arguments, CRUCIBLE_NET
*
**************************************************************************/
#include "Network/NetworkSettings.h"

#include "Network/Protocol.h"   // IsValidPlayerName

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
	#include <shellapi.h>
#endif

namespace Crucible
{
	namespace
	{
		bool ParsePort(std::string_view text, std::uint16_t& out)
		{
			unsigned value = 0;
			const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
			if (ec != std::errc{} || end != text.data() + text.size() || value == 0 || value > 65535) return false;
			out = static_cast<std::uint16_t>(value);
			return true;
		}

		// "192.168.1.20", "192.168.1.20:27020", "::1", "[::1]:27020". A port in the text
		// replaces `port`; without one, `port` is kept.
		bool ParseEndpoint(std::string_view text, std::string& address, std::uint16_t& port)
		{
			if (text.empty()) return false;
			if (text.front() == '[')
			{
				const std::size_t close = text.find(']');
				if (close == std::string_view::npos || close == 1) return false;
				address = std::string(text.substr(1, close - 1));
				const std::string_view rest = text.substr(close + 1);
				if (rest.empty()) return true;
				return rest.front() == ':' && ParsePort(rest.substr(1), port);
			}
			const std::size_t colon = text.find(':');
			if (colon == std::string_view::npos || text.find(':', colon + 1) != std::string_view::npos)
			{
				address = std::string(text);   // no port, or a bare IPv6 address
				return true;
			}
			if (colon == 0) return false;
			address = std::string(text.substr(0, colon));
			return ParsePort(text.substr(colon + 1), port);
		}

		std::string Lower(std::string_view text)
		{
			std::string out(text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

#ifdef _WIN32
		std::string ToUtf8(const wchar_t* text)
		{
			const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (bytes <= 1) return {};
			std::string out(static_cast<std::size_t>(bytes - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
			return out;
		}
#endif

		std::string EnvironmentValue(const char* name)
		{
#ifdef _WIN32
			std::wstring wide(name, name + std::char_traits<char>::length(name));
			const DWORD length = GetEnvironmentVariableW(wide.c_str(), nullptr, 0);
			if (length == 0) return {};
			std::wstring value(length, L'\0');
			GetEnvironmentVariableW(wide.c_str(), value.data(), length);
			return ToUtf8(value.c_str());
#else
			const char* value = std::getenv(name);
			return value != nullptr ? value : "";
#endif
		}
	}

	const char* ToString(NetMode mode) noexcept
	{
		switch (mode)
		{
		case NetMode::Host:   return "host";
		case NetMode::Client: return "client";
		default:              return "offline";
		}
	}

	bool Validate(const NetworkSettings& settings, std::string& error)
	{
		if (settings.mode == NetMode::Offline) return true;
		if (settings.maxClients < 1 || settings.maxClients > Net::kMaxClients) { error = "max clients must be 1 to " + std::to_string(Net::kMaxClients); return false; }
		if (!IsValidPlayerName(settings.playerName)) { error = "player name must be at most 32 bytes, without control characters"; return false; }
		if (settings.joinTimeoutMs == 0) { error = "join timeout must be above 0"; return false; }
		if (settings.mode == NetMode::Host && settings.hostPort == 0) { error = "a host needs a non-zero port"; return false; }
		if (settings.mode == NetMode::Client)
		{
			if (settings.connectAddress.empty()) { error = "a client needs the host's address (--net-connect IP[:PORT])"; return false; }
			if (settings.connectPort == 0) { error = "a client needs a non-zero port"; return false; }
		}
		return true;
	}

	bool ApplyArguments(std::span<const std::string> args, NetworkSettings& settings, std::string& error)
	{
		NetworkSettings result = settings;   // applied only if every flag is valid
		for (std::size_t i = 0; i < args.size(); ++i)
		{
			const std::string_view flag = args[i];
			if (!flag.starts_with("--net-")) continue;   // someone else's argument
			if (i + 1 >= args.size()) { error = std::string(flag) + " needs a value"; return false; }
			const std::string_view value = args[++i];

			if (flag == "--net-mode")
			{
				const std::string mode = Lower(value);
				if (mode == "offline")     result.mode = NetMode::Offline;
				else if (mode == "host")   result.mode = NetMode::Host;
				else if (mode == "client") result.mode = NetMode::Client;
				else { error = "--net-mode must be offline, host or client, not '" + std::string(value) + "'"; return false; }
			}
			else if (flag == "--net-port")
			{
				if (!ParsePort(value, result.hostPort)) { error = "--net-port must be 1 to 65535"; return false; }
			}
			else if (flag == "--net-listen")
			{
				result.listenAddress = std::string(value);
			}
			else if (flag == "--net-connect")
			{
				if (!ParseEndpoint(value, result.connectAddress, result.connectPort)) { error = "--net-connect expects IP or IP:PORT, not '" + std::string(value) + "'"; return false; }
			}
			else if (flag == "--net-name")
			{
				result.playerName = std::string(value);
			}
			else if (flag == "--net-max-clients")
			{
				unsigned clients = 0;
				const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), clients);
				if (ec != std::errc{} || end != value.data() + value.size()) { error = "--net-max-clients must be a number"; return false; }
				result.maxClients = clients;
			}
			else
			{
				error = "unknown flag " + std::string(flag);
				return false;
			}
		}
		settings = std::move(result);
		return true;
	}

	std::vector<std::string> SplitArguments(std::string_view text)
	{
		std::vector<std::string> words;
		std::size_t i = 0;
		while (i < text.size())
		{
			while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
			const std::size_t start = i;
			while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
			if (i > start) words.emplace_back(text.substr(start, i - start));
		}
		return words;
	}

	std::vector<std::string> ProcessArguments()
	{
		std::vector<std::string> args;
#ifdef _WIN32
		int count = 0;
		wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count);
		if (argv == nullptr) return args;
		for (int i = 1; i < count; ++i) args.push_back(ToUtf8(argv[i]));   // [0] is the program
		LocalFree(argv);
#endif
		return args;
	}

	NetworkSettings ResolveStartupSettings(std::string& error)
	{
		NetworkSettings settings;
		std::string problem;
		const std::vector<std::string> environment = SplitArguments(EnvironmentValue("CRUCIBLE_NET"));
		if (!ApplyArguments(environment, settings, problem)) error = "CRUCIBLE_NET: " + problem;
		const std::vector<std::string> commandLine = ProcessArguments();
		if (!ApplyArguments(commandLine, settings, problem)) error += (error.empty() ? "" : "; ") + std::string("command line: ") + problem;
		return settings;
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
