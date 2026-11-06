/*************************************************************************
* \file:         NetComponents.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.2
* \brief
*                The networking ECS components: NetworkIdentity,
*                NetInterpolation and PlayerInput (ComponentKind 33-35),
*                and NetworkObjectAuthoring (39).
*
* \details
*                The first three are session data: they are added while
*                the game runs and mean nothing once it stops. So they are
*                RuntimeOnly (never in a scene, prefab, Play snapshot, undo
*                step or copy/paste) and NotToggleable (the ECS never
*                switches them off; see ComponentKind.h).
*
*                NetworkObjectAuthoring is the opposite: authored data,
*                saved with scenes and prefabs like any component, and the
*                one place an object's networking settings live. It holds
*                no runtime identity (no NetworkId, no owner): those exist
*                only in NetworkIdentity, while a session runs.
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
*    -v1.2       NetworkObjectAuthoring (Phase 14.1)
*
**************************************************************************/
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

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

	/*************************************************************************
	 \brief
		An object's networking settings, authored in the editor and saved
		with its scene or prefab (Networking Phase 14).
	 \details
		On the ROOT of a networked object - a prefab's root (a network
		prefab, which gameplay can spawn) or an entity placed in a scene.
		The networking backend reads it to build the object's replication
		profile (NetPrefabDesc); it never writes it, and nothing here is a
		runtime identity: NetworkIds and owners exist only in
		NetworkIdentity, while a session runs.

		Everything is stored by NAME, so engine numbering changes cannot
		break a saved file:
		  replicated         serializer names (ReplicationRegistry), in
		                     record order: what each spawn and snapshot
		                     carries ("Transform/YawOnly")
		  serverOnly         component names switched off on clients, over
		                     the object's whole hierarchy (AI, scripts,
		                     physics)
		  ownerLocalOnly     component names switched off except on the
		                     owning machine (camera rig, HUD)
		  enabledReplicated  root component names whose on/off state is
		                     replicated (EnabledMask)
		A component in none of the lists is Local: it keeps the value its
		scene or prefab loaded on every machine and runs everywhere.

		Which names and combinations are valid is checked when the profile
		is built (NetPrefabTable's rules); this struct only stores them.
	 *************************************************************************/
	struct NetworkObjectAuthoring
	{
		std::vector<std::string> replicated;
		std::vector<std::string> serverOnly;
		std::vector<std::string> ownerLocalOnly;
		std::vector<std::string> enabledReplicated;
		float sendPriority = 1.f;          // snapshot scheduling weight, (0, 1000] (host only, not hashed)
		bool  critical = false;            // every snapshot, ahead of the budget (host only, not hashed)
		bool  allowScenePlacement = true;  // instances placed in scenes are networked scene objects
		bool  spawnPerPlayer = false;      // the host spawns one for itself and one per joining client

		bool operator==(const NetworkObjectAuthoring&) const = default;
	};

	// Bind each one to its ComponentKind (see ComponentKind.h).
	CRUCIBLE_COMPONENT(NetworkIdentity, NetworkIdentity);
	CRUCIBLE_COMPONENT(NetInterpolation, NetInterpolation);
	CRUCIBLE_COMPONENT(PlayerInput, PlayerInput);
	CRUCIBLE_COMPONENT(NetworkObjectAuthoring, NetworkObjectAuthoring);

	// Session data: never saved, never switched off.
	template <> struct RuntimeOnly<NetworkIdentity>  : std::true_type {};
	template <> struct RuntimeOnly<NetInterpolation> : std::true_type {};
	template <> struct RuntimeOnly<PlayerInput>      : std::true_type {};

	template <> struct NotToggleable<NetworkIdentity>  : std::true_type {};
	template <> struct NotToggleable<NetInterpolation> : std::true_type {};
	template <> struct NotToggleable<PlayerInput>      : std::true_type {};

	// Authored settings: saved (not RuntimeOnly), but never switched off - "networking off for
	// now" is not a state an object can be in; remove the component instead.
	template <> struct NotToggleable<NetworkObjectAuthoring> : std::true_type {};

	// Plain values with no heap inside, so they are cheap to copy and to move between archetypes.
	static_assert(std::is_trivially_copyable_v<NetworkIdentity>);
	static_assert(std::is_trivially_copyable_v<NetInterpolation>);
	static_assert(std::is_trivially_copyable_v<PlayerInput>);
	static_assert(sizeof(NetInterpolation::Sample) == 32, "Sample layout changed: update the size note above");
}
