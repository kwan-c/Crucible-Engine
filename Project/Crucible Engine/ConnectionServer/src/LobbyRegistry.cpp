/*************************************************************************
* \file:         LobbyRegistry.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          21 November 2026
* \version       1.0
* \brief
*                LobbyRegistry (see the header).
*
* \history
*    -v1.0       LAN connection server
*
**************************************************************************/
#include "LobbyRegistry.h"

#include <algorithm>
#include <vector>

namespace Crucible::LobbyServer
{
	RegisteredMsg LobbyRegistry::Register(const RegisterMsg& message, std::uint32_t observed, Clock::time_point now, bool& added)
	{
		added = false;
		RegisteredMsg reply;
		reply.request = message.request;
		reply.lobby = message.lobby;
		reply.heartbeatMs = static_cast<std::uint16_t>(kHeartbeat.count());
		reply.lifetimeMs = static_cast<std::uint16_t>(std::min<long long>(m_lifetime.count(), 65535));

		if (message.lobby == 0 || message.ownerToken == 0 || message.gamePort == 0 || message.maxPlayers == 0 ||
			message.players > message.maxPlayers)
		{
			reply.result = RegisterResult::BadRequest;
			return reply;
		}

		if (const auto found = m_lobbies.find(message.lobby); found != m_lobbies.end())
		{
			if (found->second.registration.ownerToken != message.ownerToken)
			{
				reply.result = RegisterResult::IdInUse;   // someone else's: nothing changes
				return reply;
			}
			found->second.registration = message;   // the heartbeat: new numbers, maybe a new address
			found->second.observed = observed;
			found->second.lastHeard = now;
			reply.result = RegisterResult::Ok;
			return reply;
		}

		if (m_lobbies.size() >= kMaxLobbies)
		{
			reply.result = RegisterResult::ServerFull;
			return reply;
		}
		const auto fromAddress = std::ranges::count_if(m_lobbies, [&](const auto& l) { return l.second.observed == observed; });
		if (static_cast<std::size_t>(fromAddress) >= kMaxPerAddress)
		{
			reply.result = RegisterResult::TooManyFromAddress;
			return reply;
		}
		m_lobbies.emplace(message.lobby, Lobby{ .registration = message, .observed = observed, .lastHeard = now, .registered = now });
		added = true;
		reply.result = RegisterResult::Ok;
		return reply;
	}

	bool LobbyRegistry::Unregister(const UnregisterMsg& message)
	{
		const auto found = m_lobbies.find(message.lobby);
		if (found == m_lobbies.end() || found->second.registration.ownerToken != message.ownerToken) return false;
		m_lobbies.erase(found);
		return true;
	}

	std::size_t LobbyRegistry::Expire(Clock::time_point now)
	{
		return std::erase_if(m_lobbies, [&](const auto& l) { return now - l.second.lastHeard > m_lifetime; });
	}

	ListReplyMsg LobbyRegistry::List(const ListRequestMsg& request, Clock::time_point now) const
	{
		std::vector<const Lobby*> matching;
		for (const auto& [id, lobby] : m_lobbies)
			if (lobby.registration.protocolVersion == request.protocolVersion) matching.push_back(&lobby);
		std::ranges::sort(matching, [](const Lobby* a, const Lobby* b) {
			if (a->registration.open != b->registration.open) return a->registration.open;
			// Not by lastHeard: every heartbeat would move that lobby, and the rows would swap.
			if (a->registered != b->registered) return a->registered < b->registered;
			return a->registration.lobby < b->registration.lobby;
		});

		ListReplyMsg reply;
		reply.request = request.request;
		reply.total = static_cast<std::uint16_t>(std::min<std::size_t>(matching.size(), 65535));
		std::size_t bytes = kListReplyHeaderBytes;
		for (const Lobby* lobby : matching)
		{
			const RegisterMsg& r = lobby->registration;
			ListEntry entry;
			entry.lobby = r.lobby;
			entry.observed = lobby->observed;
			entry.observedLoopback = IsLoopback(lobby->observed);
			entry.open = r.open;
			entry.gamePort = r.gamePort;
			entry.players = r.players;
			entry.maxPlayers = r.maxPlayers;
			const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - lobby->lastHeard).count();
			entry.ageMs = static_cast<std::uint16_t>(std::clamp<long long>(age, 0, 65535));
			entry.name = r.name;
			entry.status = r.status;
			entry.addresses = r.addresses;
			const std::size_t size = EntryBytes(entry);
			if (bytes + size > kMaxPacket || reply.entries.size() == 255) break;   // the rest does not fit: truncated
			bytes += size;
			reply.entries.push_back(std::move(entry));
		}
		reply.truncated = reply.entries.size() < matching.size();
		return reply;
	}
}
