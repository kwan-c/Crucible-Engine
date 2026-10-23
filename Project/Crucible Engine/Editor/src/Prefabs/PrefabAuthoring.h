#pragma once
/*!
* @file			PrefabAuthoring.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			23 October 2026
*
* @brief		Editor-side prefab creation and placement, shared by the Hierarchy, the Asset
*				Browser and the Scene view.
*
* Creating - both end in CreatePrefabs():
*	Hierarchy       right-click an entity > Create Prefab   -> saved in Assets/Prefabs
*	Asset Browser   drag an entity from the Hierarchy onto the file grid (saved in the folder
*	                being viewed) or onto a folder tile (saved in that folder)
*
* Placing - both end in PlacePrefab():
*	Scene view      drag a .cprefab from the Asset Browser into the view: it lands where the
*	                cursor points (on the surface under it, else on the y = 0 ground plane, else
*	                10 units in front of the camera)
*	Hierarchy       drop it on a row: placed as that entity's child, sitting exactly on it;
*	                drop it on the empty space below the tree: placed as a root at the origin
*
* Each top-level entity becomes its own .cprefab, holding it and all its children (see
* Engine/src/Scene/Prefab.h for the format). The file is named after the entity
* ("Crate.cprefab"), or "Crate (1).cprefab" etc. if that name is taken - an existing prefab is
* never overwritten. The Asset Browser is refreshed afterwards, so the new file gets its .meta
* (and guid) straight away.
*
* Creating is not allowed during Play Mode (CanCreate()), the same rule as the Asset Browser's
* Refresh: the prefab would capture runtime state, and the asset index must not change
* mid-session. Placing is allowed (like Paste); Stop undoes it with everything else.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ECS/Core/ECSCore.h"   // Entity
#include "Math/Math.h"          // Math::vec3
#include <optional>
#include <string>
#include <vector>

namespace Crucible {
	class World;
}

namespace Crucible::PrefabAuthoring {

	// Where Hierarchy > Create Prefab saves (virtual path, relative to the Assets folder).
	inline constexpr char const* kDefaultFolder = "Prefabs";

	// ---- Creating ----

	// Whether a prefab may be created right now (false in Play Mode, or before Core exists).
	bool CanCreate();

	// The entities a Hierarchy drag stands for: the whole Inspector selection when the dragged
	// row is part of it (the Hierarchy drags the whole selection), otherwise just that row.
	std::vector<Entity> DraggedEntities(World& world, Entity dragged);

	// Saves each entity, with its children, as its own .cprefab in `folder` (virtual path, "" =
	// the Assets root; created if missing). Entities that are dead, or whose ancestor is also in
	// the list, are skipped - they are already inside that ancestor's prefab.
	// Returns the virtual paths written, e.g. "Prefabs/Crate.cprefab". Logs each one.
	// Must not be called while the World is being iterated (no structural change is made, but
	// callers are UI code that defers anyway; keep it that way).
	std::vector<std::string> CreatePrefabs(World& world, std::vector<Entity> const& entities, std::string const& folder);

	// ---- Placing ----

	// True if the virtual path names a .cprefab (extension compared case-insensitively).
	bool IsPrefabFile(std::string const& virtualPath);

	// True while an Asset Browser drag of a .cprefab is in progress. Lets a drop target accept
	// only prefabs (and so only light up for them) without accepting other assets.
	bool IsDraggingPrefab();

	// Adds one copy of the prefab at `virtualPath` to the scene, selects it, and records it as
	// one undo step ("Place Crate"). The scene is marked dirty.
	//	parent    entity to attach the copy's root to; kNullEntity (or a dead entity) = a root.
	//	position  the root's LOCAL position - its world position when it is a root. nullopt
	//	          keeps the saved one, which is zero (Prefab::SaveFrom zeroes it), so a copy
	//	          with a parent sits exactly on that parent.
	// Returns the new root, or kNullEntity if the file could not be loaded (logged).
	// Makes structural changes: never call it while the World is being iterated.
	Entity PlacePrefab(World& world, std::string const& virtualPath, Entity parent = kNullEntity,
	                   std::optional<Math::vec3> position = std::nullopt);
}
