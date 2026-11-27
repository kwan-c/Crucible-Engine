/*************************************************************************
* \file:         LobbyRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          21 November 2026
* \version       1.0
* \brief
*                The connection server's lobby list: who registered, with
*                what, and when it was last heard from. No sockets: main.cpp
*                feeds it messages, so it can be tested on its own.
*
* \details
*                - Register is "insert or update": the first one adds the
*                  lobby, every later one (the heartbeat) refreshes it. The
*                  same lobby id with a different owner token is refused
*                  (IdInUse) and changes nothing.
*                - Unregister removes a lobby only with its owner token.
*                - Expire removes lobbies not heard from for the lifetime.
*                - List answers with one self-contained snapshot that fits
*                  one packet: open lobbies first, then in the order they
*                  registered (oldest first), then by id. Heartbeats never
*                  reorder it, so a browser's rows stay put and a new lobby
*                  is added at the end; total and truncated say how many
*                  matched and whether some were left out.
*                - Limits: kMaxLobbies in all, kMaxPerAddress per source
*                  address.
*
* \history
*    -v1.0       LAN connection server
*
**************************************************************************/
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "Network/LobbyServerProtocol.h"

namespace Crucible::LobbyServer
{
	class LobbyRegistry
	{
	public:
		using Clock = std::chrono::steady_clock;

		static constexpr std::size_t kMaxLobbies = 256;
		static constexpr std::size_t kMaxPerAddress = 8;

		explicit LobbyRegistry(std::chrono::milliseconds lifetime = kDefaultLifetime) : m_lifetime(lifetime) {}

		/// A Register (or heartbeat) from `observed`. `added` says whether it was new.
		RegisteredMsg Register(const RegisterMsg& message, std::uint32_t observed, Clock::time_point now, bool& added);

		/// True if the lobby existed and the token matched (it is gone now).
		bool Unregister(const UnregisterMsg& message);

		/// Removes lobbies not heard from within the lifetime; how many went.
		std::size_t Expire(Clock::time_point now);

		/// The snapshot for one ListRequest (fits kMaxPacket when encoded).
		ListReplyMsg List(const ListRequestMsg& request, Clock::time_point now) const;

		std::size_t Count() const noexcept { return m_lobbies.size(); }
		std::chrono::milliseconds Lifetime() const noexcept { return m_lifetime; }

		struct Lobby
		{
			RegisterMsg   registration;
			std::uint32_t observed = 0;
			Clock::time_point lastHeard{};
			Clock::time_point registered{};   // its first Register: the list order (heartbeats leave it)
		};
		const std::unordered_map<std::uint64_t, Lobby>& Lobbies() const noexcept { return m_lobbies; }

	private:
		std::chrono::milliseconds m_lifetime;
		std::unordered_map<std::uint64_t, Lobby> m_lobbies;   // by lobby id
	};
}
