/*************************************************************************
* \file:         NetSyncedChildren.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.1
* \brief
*                A network object's synced
*                children - finding them by name path, and replicating
*                their local transforms ("Children/Rotation",
*                "Children/Full").
*
* \details
*                Only a network object's ROOT has an identity on the wire.
*                A child is named instead, by the path of EntityNames from
*                the root down ("Mech Visual/Torso"), listed in the
*                object's NetworkObjectAuthoring::syncedChildren. Both
*                machines instantiate the same prefab (or load the same
*                scene), so the same path finds the same child on each.
*
*                Resolving a path, one segment per level: the segment must
*                match the EntityName of EXACTLY ONE direct child. None is
*                Missing; two or more is Ambiguous - and an unresolved path
*                is never guessed: that child is simply not synchronized,
*                and the problem is reported (at authoring time by the
*                Inspector and the session start's checks, at run time
*                once per object and slot). Never matched or entered:
*                client query bodies and other network objects (an entity
*                with NetworkIdentity below the root is a network object
*                attached at run time).
*
*                The serializers (on the root's NetChildTransforms, added
*                by the network layer):
*                  Children/Rotation  u16 mask of the slots carried, then
*                                     each slot's local rotation (smallest
*                                     three, 4 bytes): 2 to 66 bytes.
*                  Children/Full      u16 mask, then each slot's local
*                                     position (3 x 24.8 fixed) and
*                                     rotation: 2 to 258 bytes.
*                The host writes the children's LOCAL transforms (relative
*                to their own parent inside the object). A slot whose path
*                does not resolve on the host is left out of the mask; a
*                mask bit past the object's list is malformed. A client
*                keeps a short history and draws each child at the root's
*                render tick (ApplyChildTransforms, from
*                NetInterpolationSystem): same delay as the root, so the
*                parts of an object move together.
*
*                Positions more than kTeleportMetres apart between two
*                samples are not blended (the child snaps), and
*                RestartChildHistory drops all but the newest sample when
*                the object's parent space changes (an attachment, task
*                4): a child is never interpolated across two spaces.
*
* \history
*    -v1.0       paths, resolution (World and prefab file),
*                Children/Rotation and Children/Full, client binding and drawing
*    -v1.1       Children/OnOff and ApplyChildOnOff
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ECS/Core/ECSCore.h"
#include "EngineExport.h"
#include "Math/Math.h"

namespace Crucible
{
	class World;
	class Prefab;
	struct NetPrefabDesc;
	struct NetChildTransforms;
	struct ReplicatedComponent;

	/// What resolving one path found.
	enum class ChildPathResult : std::uint8_t
	{
		Found,       // exactly one entity
		Missing,     // a segment matched no child
		Ambiguous,   // a segment matched two or more children
	};

	/// The longest path accepted (characters).
	inline constexpr std::size_t kMaxChildPathLength = 256;

	/// A well-formed path: 1 to kMaxChildPathLength characters, segments separated by '/', none
	/// empty (no leading, trailing or doubled '/'). False with the reason in *why.
	ENGINE_API bool IsValidChildPath(std::string_view path, std::string* why = nullptr);

	/// A syncedChildren list: every path well-formed, at most kMaxSyncedChildren, none twice.
	ENGINE_API bool CheckSyncedChildList(const std::vector<std::string>& paths, std::string& error);

	/// Resolves `path` below `root` in a live World (see the file notes). `out` is the entity when
	/// Found, kNullEntity otherwise.
	ENGINE_API ChildPathResult ResolveChildPath(const World& world, Entity root, std::string_view path, Entity& out);

	/// The same over a prefab file's entries (each entry's saved EntityName), below entry
	/// `rootIndex`. `out` is the entry index when Found.
	ENGINE_API ChildPathResult ResolveChildPath(const Prefab& prefab, std::size_t rootIndex, std::string_view path, std::size_t& out);

	/// The name path of `entity` below `root`: "" for root itself. False if `entity` is not below
	/// `root`, or an entity on the way has no EntityName (an empty segment cannot be resolved).
	ENGINE_API bool ChildPathOf(const World& world, Entity root, Entity entity, std::string& out);

	/// FNV-1a (32 bits) of a path; never 0 for a non-empty path, 0 for "" (the root itself).
	ENGINE_API std::uint32_t ChildPathHash(std::string_view path) noexcept;

	/// The two child-transform serializers (ReplicationRegistry::RegisterBuiltIns registers them).
	ReplicatedComponent ChildRotationsSerializer();
	ReplicatedComponent ChildTransformsSerializer();

	/// "Children/OnOff": for each synced child the host resolves, which of
	/// childEnabledReplicated it has and which of those are on: u16 mask of the children carried,
	/// then 2 bytes each. A bit past the lists is malformed.
	ReplicatedComponent ChildOnOffSerializer();

	/// Client, after a record: every carried child's listed components switched as the host has
	/// them. A host-only Collider or CharacterController switches the child's query body instead
	/// (as EnabledMask does on the root); a child or component missing here is skipped. Returns
	/// how many switches it made.
	ENGINE_API unsigned ApplyChildOnOff(World& world, Entity root, const NetPrefabDesc& desc);

	/// Client, after a record was applied to `root`: resolves its synced children on this machine
	/// into its NetChildTransforms (a slot that does not resolve is skipped and reported once).
	ENGINE_API void BindSyncedChildren(World& world, Entity root, const NetPrefabDesc& desc);

	/// Slot `slot`'s local pose at `renderTick` from the samples that carried it (the file notes:
	/// between two, the oldest when all are ahead, the newest when none is; no blend across a
	/// teleport). False if no sample carried it.
	ENGINE_API bool EvaluateChild(const NetChildTransforms& transforms, std::uint32_t slot, double renderTick,
		Math::vec3& position, Math::quat& rotation) noexcept;

	/// Client, every frame (NetInterpolationSystem): every network object's bound children drawn
	/// at `renderTick`. Writes a Transform only when it changes.
	ENGINE_API void ApplyChildTransforms(World& world, double renderTick);

	/// Keeps only the newest sample: the object's parent space changed (an attachment).
	ENGINE_API void RestartChildHistory(NetChildTransforms& transforms) noexcept;
}
