/*************************************************************************
* \file:         NetComponents.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.1
* \brief
*                The networking ECS components: NetworkIdentity,
*                NetInterpolation and PlayerInput, bound to their
*                ComponentKind values (33-35).
*
* \details
*                All three are session data: they are added while the
*                game runs and mean nothing once it stops. So all three
*                are RuntimeOnly (never in a scene, prefab, Play snapshot,
*                undo step or copy/paste) and NotToggleable (the ECS never
*                switches them off; see ComponentKind.h).
*
*                Plain data only: the systems that write them come in
*                later phases (identity map, spawning, input,
*                interpolation). Components.h includes this file, so
*                CRUCIBLE_COMPONENT_LIST sees the bindings.
*
* \history
*    -v1.0       NetworkIdentity, NetInterpolation, PlayerInput
*                (Networking Guide 4.2)
*    -v1.1       PlayerInput::hasYaw (the controller faces yawDegrees only
*                when something set it) (PR 10)
*
**************************************************************************/
#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "ECS/Components/ComponentKind.h"
#include "Math/Math.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	/*************************************************************************
	 \brief
		Who a networked entity is, the same on every peer.
	 \details
		On the root entity of a networked object, on the server and on
		every client. Written once when the entity is spawned and never
		edited afterwards; the session keeps its NetworkId -> Entity map
		in step with it (later phase).
	 *************************************************************************/
	struct NetworkIdentity
	{
		Net::NetworkId   id     = Net::kInvalidNetworkId;
		Net::NetPrefabId prefab = Net::kInvalidNetPrefab;
		Net::PeerId      owner  = Net::kServerPeer;   // whose input drives it
	};

	/*************************************************************************
	 \brief
		Client only: the latest authoritative samples of a remote entity,
		which the interpolation system blends between.
	 \details
		Fixed size, no heap: 8 samples of 32 bytes (tick, position,
		rotation), oldest first.
	 *************************************************************************/
	struct NetInterpolation
	{
		struct Sample
		{
			Net::NetTick tick = 0;
			Math::vec3   position{ 0.f };
			Math::quat   rotation{ 1.f, 0.f, 0.f, 0.f };   // identity (w, x, y, z)
		};

		static constexpr std::uint32_t kCapacity = 8;

		std::array<Sample, kCapacity> samples{};
		std::uint32_t count       = 0;   // valid samples, oldest first
		Net::NetTick  lastApplied = 0;   // newest tick accepted for this entity (drops stale ones)
	};

	/*************************************************************************
	 \brief
		One player's intent for the current tick.
	 \details
		Written by local sampling (offline, and the hosting player on the
		server) or, on the server, from a client's input commands; read by
		movement. sampleSerial changes every time the sampler writes, so the
		sender can tell a fresh input from one nobody has updated.
	 *************************************************************************/
	struct PlayerInput
	{
		Math::vec3    moveDirection{ 0.f };   // world-space XZ, length <= 1
		float         yawDegrees   = 0.f;     // where the player faces (world yaw, degrees)
		bool          hasYaw       = false;   // yawDegrees is meaningful: a camera orbit (or a command) set it
		bool          jump         = false;   // edge: consumed once
		Net::NetTick  tick         = 0;       // the tick this input belongs to
		std::uint32_t sampleSerial = 0;       // bumped by the sampler each time it writes
	};

	// Bind each one to its ComponentKind (see ComponentKind.h).
	CRUCIBLE_COMPONENT(NetworkIdentity, NetworkIdentity);
	CRUCIBLE_COMPONENT(NetInterpolation, NetInterpolation);
	CRUCIBLE_COMPONENT(PlayerInput, PlayerInput);

	// Session data: never saved, never switched off.
	template <> struct RuntimeOnly<NetworkIdentity>  : std::true_type {};
	template <> struct RuntimeOnly<NetInterpolation> : std::true_type {};
	template <> struct RuntimeOnly<PlayerInput>      : std::true_type {};

	template <> struct NotToggleable<NetworkIdentity>  : std::true_type {};
	template <> struct NotToggleable<NetInterpolation> : std::true_type {};
	template <> struct NotToggleable<PlayerInput>      : std::true_type {};

	// Plain values with no heap inside, so they are cheap to copy and to move between archetypes.
	static_assert(std::is_trivially_copyable_v<NetworkIdentity>);
	static_assert(std::is_trivially_copyable_v<NetInterpolation>);
	static_assert(std::is_trivially_copyable_v<PlayerInput>);
	static_assert(sizeof(NetInterpolation::Sample) == 32, "Sample layout changed: update the size note above");
}
