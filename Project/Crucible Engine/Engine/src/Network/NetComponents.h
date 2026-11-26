/*************************************************************************
* \file:         NetComponents.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.6
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
*    -v1.1       PlayerInput::hasYaw (the controller faces yawDegrees only
*                when something set it)
*    -v1.2       NetworkObjectAuthoring (Phase 14.1)
*    -v1.3       NetVariables: replicated script values (Networking C# Phase 4)
*    -v1.4       NetAnimator: replicated Animator parameters (Networking C# Phase 7)
*    -v1.5       NetworkObjectAuthoring::syncedChildren; NetChildTransforms,
*                NetAttachment, NetRenderers; kMaxSyncedChildren
*    -v1.6       NetworkObjectAuthoring::childEnabledReplicated, NetChildEnabled
*
**************************************************************************/
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "ECS/Components/ComponentKind.h"
#include "ECS/Core/ECSCore.h"   // Entity (a client's resolved synced children)
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
		  syncedChildren     name paths of children below the root
		                     ("Mech Visual/Torso"), one EntityName per
		                     level: the children whose transforms
		                     ("Children/..." serializers) and renderers
		                     ("Renderers") are replicated. At most
		                     kMaxSyncedChildren;
		                     each must name exactly one entity.
		  childEnabledReplicated  component names whose on/off state is
		                     replicated on every synced child that has
		                     them ("Children/OnOff")
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
		std::vector<std::string> syncedChildren;   // child name paths below the root
		std::vector<std::string> childEnabledReplicated;   // on/off synced on the synced children
		float sendPriority = 1.f;          // snapshot scheduling weight, (0, 1000] (host only, not hashed)
		bool  critical = false;            // every snapshot, ahead of the budget (host only, not hashed)
		bool  allowScenePlacement = true;  // instances placed in scenes are networked scene objects
		bool  spawnPerPlayer = false;      // the host spawns one for itself and one per joining client

		bool operator==(const NetworkObjectAuthoring&) const = default;
	};

	/*************************************************************************
	 \brief
		A network object's replicated script values (Networking C# Phase 4):
		16 numbered 32-bit slots the host writes and every client receives.
	 \details
		The engine gives the slots no meaning: gameplay (C# NetVars) decides
		that slot 0 is health and stores an int, a float's bits, a bool or a
		NetworkId in it. An object replicates them when its Network Object
		settings list the "NetVariables" serializer; the network layer then
		adds this component itself on every machine (never saved: all slots
		start at 0, the host's script sets the first values).

		`changed` is local bookkeeping, never sent: the slots whose value
		changed since the scripts were last told (OnNetVarChanged).
	 *************************************************************************/
	struct NetVariables
	{
		static constexpr std::uint32_t kSlots = 16;

		std::array<std::uint32_t, kSlots> slots{};
		std::uint16_t changed = 0;   // bit i: slot i changed since the scripts last heard
	};

	/*************************************************************************
	 \brief
		A network object's replicated Animator parameters (Networking C#
		Phase 7): the values the host's scripts set, for every animated
		model in the object (root and children).
	 \details
		Slot i is the i-th parameter of the object's animated models, taken
		breadth first through the hierarchy, each model's parameters in its
		controller's order (Network/NetAnimator.h builds that layout the
		same way on every machine). A slot holds a Bool (0/1), an Integer,
		a Float's bits, or for a Trigger how many times the host fired it.
		`written` marks the slots the host has set: the others keep the
		controller's own initial value on clients.

		An object replicates them when its Network Object settings list the
		"Animator" serializer; the network layer then adds this component
		itself on every machine (never saved). The host fills it from its
		scripts' Animator calls; each client gives the values to its own
		animator every frame, so its state machine follows the host's.

		firedSeen, received and primed are a client's own bookkeeping, never
		sent: which trigger counts it has already played, and whether the
		counts of its first record were taken as the starting point (a
		joining client does not replay old triggers).
	 *************************************************************************/
	struct NetAnimator
	{
		static constexpr std::uint32_t kSlots = 32;

		std::array<std::uint32_t, kSlots> slots{};
		std::uint32_t written = 0;   // bit i: the host has set slot i

		std::array<std::uint32_t, kSlots> firedSeen{};   // client: trigger counts already played
		bool received = false;   // client: a record has been applied
		bool primed = false;     // client: firedSeen holds the first record's counts
	};

	/// The most children one network object can sync (NetworkObjectAuthoring::syncedChildren).
	inline constexpr std::uint32_t kMaxSyncedChildren = 16;

	/*************************************************************************
	 \brief
		The local transforms of a network object's
		synced children, as the host sent them.
	 \details
		On the network ROOT, added by the network layer on every machine
		when the object replicates "Children/Rotation" or "Children/Full"
		(never saved). Slot i is syncedChildren[i]. Each record adds a
		sample (its server tick, which slots it carried, and their local
		rotations - and positions for Children/Full); a client keeps the
		last kHistory and NetInterpolationSystem draws each child between
		two of them at the same render tick as the root, so torso and legs
		stay together. A slot the host could not resolve is not carried,
		and the client leaves that child alone.

		children, bound and warned are a client's own bookkeeping, never
		sent: NetReceiveSystem resolves the paths on this machine after
		every record (an unresolvable path - missing, or naming two
		entities - is never guessed: that slot is skipped and reported
		once).
	 *************************************************************************/
	struct NetChildTransforms
	{
		static constexpr std::uint32_t kSlots = kMaxSyncedChildren;
		static constexpr std::uint32_t kHistory = 8;

		struct Sample
		{
			Net::NetTick  tick = 0;
			std::uint16_t present = 0;   // bit i: slot i was carried
			std::array<Math::vec3, kSlots> positions{};   // "Children/Full" only
			std::array<Math::quat, kSlots> rotations{};
		};

		bool                positions = false;   // the samples carry positions (Children/Full)
		std::vector<Sample> samples;             // client: oldest first, at most kHistory

		std::array<Entity, kSlots> children{};   // client: slot i's entity here (bound slots only)
		std::uint16_t bound = 0;                 // client: slots resolved on this machine
		std::uint16_t warned = 0;                // client: slots already reported as unresolvable
	};

	/*************************************************************************
	 \brief
		Which network object this one hangs under, and
		where in it (a mech part on a mount of another player).
	 \details
		On the network ROOT, added by the network layer when the object
		replicates "Attachment". parent is the NetworkId of the nearest
		network object above it on the host (none: it is a root); mount
		identifies the entity it is parented to inside that object by the
		FNV-1a hash of its name path below the object's root (0: the
		object's root itself). The host's Transform of an attached object
		is local to the mount, and so is what its Transform serializer
		sends: a client parents its copy the same way before using it.

		waiting and warned are a client's own bookkeeping, never sent.
	 *************************************************************************/
	struct NetAttachment
	{
		Net::NetworkId parent = Net::kInvalidNetworkId;   // none: not attached
		std::uint32_t  mount = 0;                         // name-path hash of the mount; 0 = the parent object's root

		bool waiting = false;   // client: the parent object or its mount is not here (yet)
		bool warned = false;    // client: the problem was reported
	};

	/*************************************************************************
	 \brief
		A network object's renderers as the host has
		them - model, material, mesh, material slots, colour, visibility,
		and MaterialParams (emissive scale, tint).
	 \details
		On the network ROOT, added by the network layer when the object
		replicates "Renderers". Slot 0 is the root, slot i is
		syncedChildren[i - 1] (the first kSlots - 1 of them). A client
		applies every received slot to that entity's MeshRenderer (and
		MaterialParams) as soon as the record is applied: values arrive as
		the host set them, at any time. A shader graph's own parameters
		belong to its material asset, so they arrive with the material.
	 *************************************************************************/
	struct NetRenderers
	{
		static constexpr std::uint32_t kSlots = 8;            // the root and 7 synced children
		static constexpr std::uint32_t kMaterialSlots = 4;    // model material slots carried

		struct Slot
		{
			bool          present = false;   // the entity has a MeshRenderer on the host
			bool          visible = true;
			Math::vec4    colour{ 0.f };
			std::uint64_t model = 0;
			std::uint32_t modelRoot = 0xFFFFFFFFu;   // InvalidModelIndex: the whole model
			std::uint64_t material = 0;
			std::uint64_t mesh = 0;
			std::uint8_t  materialCount = 0;   // entries of `materials` in use
			std::array<std::uint64_t, kMaterialSlots> materials{};
			bool          hasParams = false;   // the host's entity has MaterialParams
			float         emissiveScale = 1.f;
			Math::vec3    custom{ 0.f };

			bool operator==(const Slot&) const = default;
		};

		std::vector<Slot> slots;   // kSlots entries once a record arrived
	};

	/*************************************************************************
	 \brief
		The on/off states of components on a network
		object's synced children, as the host sent them.
	 \details
		On the network ROOT, added by the network layer when the object
		replicates "Children/OnOff". Slot i is syncedChildren[i]; bit j is
		childEnabledReplicated[j] (at most 8). `has` says which of those the
		child has on the host, `on` which of them are switched on. A client
		applies them right after the record (NetSyncedChildren.h).
	 *************************************************************************/
	struct NetChildEnabled
	{
		std::uint16_t present = 0;                    // bit i: slot i was carried
		std::array<std::uint8_t, kMaxSyncedChildren> has{};
		std::array<std::uint8_t, kMaxSyncedChildren> on{};
	};

	// Bind each one to its ComponentKind (see ComponentKind.h).
	CRUCIBLE_COMPONENT(NetChildEnabled, NetChildEnabled);
	CRUCIBLE_COMPONENT(NetChildTransforms, NetChildTransforms);
	CRUCIBLE_COMPONENT(NetAttachment, NetAttachment);
	CRUCIBLE_COMPONENT(NetRenderers, NetRenderers);
	CRUCIBLE_COMPONENT(NetAnimator, NetAnimator);
	CRUCIBLE_COMPONENT(NetVariables, NetVariables);
	CRUCIBLE_COMPONENT(NetworkIdentity, NetworkIdentity);
	CRUCIBLE_COMPONENT(NetInterpolation, NetInterpolation);
	CRUCIBLE_COMPONENT(PlayerInput, PlayerInput);
	CRUCIBLE_COMPONENT(NetworkObjectAuthoring, NetworkObjectAuthoring);

	// Session data: never saved, never switched off.
	template <> struct RuntimeOnly<NetworkIdentity>  : std::true_type {};
	template <> struct RuntimeOnly<NetInterpolation> : std::true_type {};
	template <> struct RuntimeOnly<PlayerInput>      : std::true_type {};
	template <> struct RuntimeOnly<NetVariables>     : std::true_type {};
	template <> struct RuntimeOnly<NetAnimator>      : std::true_type {};
	template <> struct RuntimeOnly<NetChildTransforms> : std::true_type {};
	template <> struct RuntimeOnly<NetAttachment>    : std::true_type {};
	template <> struct RuntimeOnly<NetRenderers>     : std::true_type {};
	template <> struct RuntimeOnly<NetChildEnabled>  : std::true_type {};

	template <> struct NotToggleable<NetworkIdentity>  : std::true_type {};
	template <> struct NotToggleable<NetInterpolation> : std::true_type {};
	template <> struct NotToggleable<PlayerInput>      : std::true_type {};
	template <> struct NotToggleable<NetVariables>     : std::true_type {};
	template <> struct NotToggleable<NetAnimator>      : std::true_type {};
	template <> struct NotToggleable<NetChildTransforms> : std::true_type {};
	template <> struct NotToggleable<NetAttachment>    : std::true_type {};
	template <> struct NotToggleable<NetRenderers>     : std::true_type {};
	template <> struct NotToggleable<NetChildEnabled>  : std::true_type {};

	// Authored settings: saved (not RuntimeOnly), but never switched off - "networking off for
	// now" is not a state an object can be in; remove the component instead.
	template <> struct NotToggleable<NetworkObjectAuthoring> : std::true_type {};

	// Plain values with no heap inside, so they are cheap to copy and to move between archetypes.
	static_assert(std::is_trivially_copyable_v<NetworkIdentity>);
	static_assert(std::is_trivially_copyable_v<NetInterpolation>);
	static_assert(std::is_trivially_copyable_v<PlayerInput>);
	static_assert(std::is_trivially_copyable_v<NetVariables>);
	static_assert(sizeof(NetInterpolation::Sample) == 32, "Sample layout changed: update the size note above");
}
