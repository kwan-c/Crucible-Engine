/*************************************************************************
* \file:         NetTypes.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The small value types every networking file shares:
*                network ids, prefab ids, peer ids, ticks, and the
*                session role.
*
* \details
*                No dependencies beyond the standard library, so the
*                byte layer, the protocol and its tests can include it
*                without pulling in the engine.
*
*                These ids are NOT ECS ids. A Crucible Entity (index +
*                generation) only means something inside one process;
*                a NetworkId means the same object on every machine in
*                a session. Never put an Entity or a ComponentKind
*                value on the wire.
*
* \history
*    -v1.0       ids, constants and NetRole (Networking Guide 4.2)
*
**************************************************************************/
#pragma once

#include <cstdint>

namespace Crucible::Net
{
	// Server-assigned name of a networked entity, the same on every peer. Never reused
	// within a session, so a late message for a dead entity cannot touch a new one.
	using NetworkId = std::uint32_t;

	// Explicit, stable id of a network prefab (written in source, never derived from
	// registration order), identical on every peer.
	using NetPrefabId = std::uint16_t;

	// 0 = the server (the hosting player, and everything no player owns), 1..3 = clients.
	using PeerId = std::uint8_t;

	// The server's simulation step number (60 Hz, the physics fixed step).
	using NetTick = std::uint32_t;

	inline constexpr NetworkId     kInvalidNetworkId = 0;
	inline constexpr NetPrefabId   kInvalidNetPrefab = 0xFFFF;
	inline constexpr PeerId        kServerPeer       = 0;
	inline constexpr PeerId        kInvalidPeer      = 0xFF;
	inline constexpr std::uint32_t kMaxClients       = 3;   // 4 players = the hosting player + 3 clients

	// Server: the authoritative World (a listen server: the hosting player's own game).
	// Client: proxies only. Offline: single player, nothing networked.
	enum class NetRole : std::uint8_t { Offline, Server, Client };
}
