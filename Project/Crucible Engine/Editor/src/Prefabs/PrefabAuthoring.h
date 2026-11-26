#pragma once
/*!
* @file			PrefabAuthoring.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			23 October 2026
*
* @brief		Editor-side prefabs: creating, placing, and the link between an instance and
*				its .cprefab. Shared by the Hierarchy, the Inspector, the Asset Browser and the
*				Scene view.
*
* Creating - both end in CreatePrefabs():
*	Hierarchy       right-click an entity > Create Prefab   -> saved in Assets/Prefabs
*	Asset Browser   drag an entity from the Hierarchy onto the file grid (saved in the folder
*	                being viewed) or onto a folder tile (saved in that folder)
*	Like Unity, the entities a prefab is made from become its first instance.
*
* Placing - both end in PlacePrefab():
*	Scene view      drag a .cprefab from the Asset Browser into the view: it lands where the
*	                cursor points (on the surface under it, else on the y = 0 ground plane, else
*	                10 units in front of the camera)
*	Hierarchy       drop it on a row: placed as that entity's child, sitting exactly on it;
*	                drop it on the empty space below the tree: placed as a root at the origin
*
* Instances (the link): every entity of an instance carries a PrefabInstance component (see
* ECS/Components/Components.h) naming the .cprefab and the entry it came from. It is saved with
* the scene and goes through undo, Play/Stop and copy/paste like any other component.
*	- Hierarchy rows of an instance are drawn in the prefab colour (red if the file is gone).
*	- The Inspector shows "Prefab: Crate" with Select (and Unpack on the root).
*	- Unpack (Hierarchy right-click on the root, or the Inspector) makes it ordinary entities.
*	- Overrides (Phase B): values an instance has that its prefab doesn't are marked in the
*	  Inspector, listed in the root's "Overrides" dropdown, and can be reverted (one property,
*	  one component, or everything), each as an undo step.
*	- Apply (Phase C1): the same overrides can be applied - written into the .cprefab - one at a
*	  time or all at once; every other instance in the open scene then follows the prefab while
*	  keeping its own overrides. Undoable, file included.
*	- Updates (Phase C2): instances in a scene that is opened, or refreshed after a .cprefab
*	  changed on disk, catch up with their prefab the same way, overrides kept.
*	- Apply to All Scenes: the root bar's button applies, then updates and saves every other
*	  scene file that uses the prefab (after a confirm box listing them).
*	- Parts of an instance below its root are "locked" in place: they can't be deleted or moved
*	  to another parent without unpacking (their components and values can still be edited, and
*	  they can still be reordered among their siblings). Entities added under an instance are
*	  ordinary entities and are not locked.
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
#include <imgui.h>              // ImVec4 (RowColour)
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Crucible {
	class World;
	class SceneManager;
}

namespace Crucible::PrefabAuthoring {

	// Where Hierarchy > Create Prefab saves (virtual path, relative to the Assets folder).
	inline constexpr char const* kDefaultFolder = "Prefabs";

	// ---- Creating ----

	// Whether a prefab may be created right now (false in Play Mode, or before Core exists).
	bool CanCreate();

	// Whether instances can be applied, updated or opened: like CanCreate, but also true in
	// Prefab Mode, where a nested prefab is an ordinary instance of its inner prefab.
	bool CanApply();

	// The entities a Hierarchy drag stands for: the whole Inspector selection when the dragged
	// row is part of it (the Hierarchy drags the whole selection), otherwise just that row.
	std::vector<Entity> DraggedEntities(World& world, Entity dragged);

	// Saves each entity, with its children, as its own .cprefab in `folder` (virtual path, "" =
	// the Assets root; created if missing), then links those entities to it as its first
	// instance (one undo step; the files themselves stay). Skipped: dead entities, entities
	// whose ancestor is also in the list (they are already inside that ancestor's prefab), and
	// locked parts of another instance (unpack first).
	// Returns the virtual paths written, e.g. "Prefabs/Crate.cprefab". Logs each one.
	// Must not be called while the World is being iterated.
	std::vector<std::string> CreatePrefabs(World& world, std::vector<Entity> const& entities, std::string const& folder);

	// ---- Placing ----

	// True if the virtual path names a .cprefab (extension compared case-insensitively).
	bool IsPrefabFile(std::string const& virtualPath);

	// True while an Asset Browser drag of a .cprefab is in progress. Lets a drop target accept
	// only prefabs (and so only light up for them) without accepting other assets.
	bool IsDraggingPrefab();

	// Adds one copy of the prefab at `virtualPath` to the scene, linked to it, selects it, and
	// records it as one undo step ("Place Crate"). The scene is marked dirty.
	//	parent    entity to attach the copy's root to; kNullEntity (or a dead entity) = a root.
	//	position  the root's LOCAL position - its world position when it is a root. nullopt
	//	          keeps the saved one, which is zero (Prefab::SaveFrom zeroes it), so a copy
	//	          with a parent sits exactly on that parent.
	// Returns the new root, or kNullEntity if the file could not be loaded (logged).
	// Makes structural changes: never call it while the World is being iterated.
	Entity PlacePrefab(World& world, std::string const& virtualPath, Entity parent = kNullEntity,
	                   std::optional<Math::vec3> position = std::nullopt);

	// ---- Instances ----

	// The virtual path of the .cprefab with this asset guid ("" if it is no longer in the project).
	std::string PrefabPathOf(std::uint64_t prefabAsset);

	// The root of an instance (PrefabInstance::isRoot).
	bool IsInstanceRoot(World const& world, Entity entity);

	// A part of an instance below its root. Its place in the tree belongs to the prefab.
	bool IsLockedPart(World const& world, Entity entity);

	// `entities` without the locked parts a Delete (newParent = nullopt) or a move to newParent
	// would take out of their place. A part that stays under its current parent (a reorder) is
	// kept. Logs one warning naming what was skipped. `verb` is for that message ("delete").
	std::vector<Entity> WithoutLockedParts(World const& world, std::vector<Entity> const& entities,
	                                       char const* verb, std::optional<Entity> newParent = std::nullopt);

	// After Paste / Duplicate: a copied PIECE of an instance (its top was a locked part) is not
	// an instance, so its links are removed. A copied whole instance keeps them. Call before the
	// creation is recorded for undo, so redo recreates it the same way.
	void DetachCopiedPart(World& world, Entity copiedTop);

	// Turns an instance back into ordinary entities: removes the link from its root and from
	// every part of the same instance below it (nested instances keep theirs). One undo step.
	// Returns false (and does nothing) if `root` is not an instance root.
	bool Unpack(World& world, Entity root);

	// The Inspector's prefab bar: "Prefab: Crate  [Select] [Unpack]", or "Part of prefab: Crate
	// [Select]". Draws nothing for an entity that is not part of an instance.
	void DrawInspectorBar(World& world, Entity entity);

	// The Hierarchy text colour for an instance's rows: the prefab colour, or red when its
	// .cprefab is gone. False (colour untouched) for an entity that isn't part of an instance.
	bool RowColour(World const& world, Entity entity, ImVec4& colour);

	// ---- Overrides (Phase B) ----
	//
	// An override is anything an instance has that its prefab entry does not: a property value
	// ("Collider/m_size"), a component switched on/off ("Collider/$enabled"), a component added
	// to the instance ("+Light") or one removed from it ("-Collider"). Found by comparing each
	// instance entity with its entry in the .cprefab, property by property, through the same
	// JSON the files hold. The root's position, rotation and name always belong to the instance
	// and are never overrides (Unity's rule). Entities added under an instance are listed too:
	// reverting one does not delete it, it detaches it to become a root (world position kept).

	struct Override {
		enum class Kind { Property, AddedComponent, RemovedComponent, AddedEntity };
		Kind        kind = Kind::Property;
		Entity      entity{};      // the instance entity it is on (the added entity itself, for AddedEntity)
		std::string component;     // "Collider" ("" for AddedEntity)
		std::string property;      // "m_size", "$enabled" for the on/off state; "" for a whole component
	};

	// This entity's own overrides (no AddedEntity entries). Empty when it isn't part of an
	// instance, or its prefab / entry can't be found.
	std::vector<Override> EntityOverrides(World& world, Entity entity);

	// Every override of the instance `root` heads: all its parts' overrides, plus the entities
	// added under it. Empty if `root` is not an instance root.
	std::vector<Override> InstanceOverrides(World& world, Entity root);

	// "Collider.m_size", "Light (added)", "Collider (removed)", "child 'Lamp' (added)".
	std::string Describe(World const& world, Override const& item);

	// Puts the prefab's values back for each item (whole component when property is ""), as one
	// undo step. Added components are removed, removed ones re-added, and added entities are
	// moved out of the instance to become roots (world position kept, children with them).
	void Revert(World& world, std::vector<Override> const& items, std::string const& label);

	// Writes every instance's current overrides into PrefabInstance::overrides - the record
	// propagation (Phase C) needs of what each instance changed. Run before every scene save
	// (InstallSceneHooks); not an undo step (the list always follows the values).
	void RefreshStoredOverrides(World& world);

	// Hooks scene saving up to RefreshStoredOverrides, and scene opening up to
	// UpdateChangedInstances. Safe to call more than once.
	void InstallSceneHooks(World& world, SceneManager& scenes);

	// ---- Inspector hooks: override marks ----
	// Around Inspector::DrawInstance for one of an entity's components:
	//		BeginDrawComponent(world, entity, "Collider"); DrawInstance(...); EndDrawComponent();
	// and inside DrawInstance, after each top-level property's widget, MarkProperty(name): an
	// overridden property gets a coloured bar at the left edge and a right-click "Revert".
	void BeginDrawComponent(World& world, Entity entity, std::string_view component);
	void EndDrawComponent();
	void MarkProperty(std::string const& property);
	// After a component's header: a bar when the component has overrides or was added.
	void MarkComponentHeader(World& world, Entity entity, std::string_view component);
	// Inside a component header's right-click menu: "Apply / Revert to Prefab" when it has overrides.
	void ComponentMenuItems(World& world, Entity entity, std::string_view component);

	// ---- Apply and propagation (Phase C1) ----

	// The root of the instance an entity belongs to (itself, for a root); kNullEntity if none.
	Entity InstanceRootOf(World const& world, Entity entity);

	// Writes `items` (overrides of the instance `root`; all of them for Apply All) into its
	// .cprefab, then updates every other instance of that prefab in the open scene: whatever they
	// overrode themselves is kept, everything else follows the prefab - new parts are created,
	// parts the prefab dropped are deleted (children added under them are detached, not lost).
	// Added children become parts of the prefab and are linked. The root's position, rotation and
	// name are never applied. ONE undo step: Ctrl+Z restores the file and every instance.
	// Not in Play Mode. Instances in other scenes update when those scenes are opened (Phase C2).
	bool ApplyToPrefab(World& world, Entity root, std::vector<Override> const& items, std::string const& label);

	// ---- Keeping instances up to date (Phase C2) ----
	//
	// Every instance root records the content hash of the .cprefab version it last matched
	// (PrefabInstance::prefabHash - set when it is placed, created, applied or updated). When the
	// file's hash differs, the prefab changed somewhere else (another scene's Apply, a git pull),
	// and the instance is brought up to date the same way Apply updates the others: the prefab's
	// values, keeping its own overrides. Those overrides are worked out against the old version
	// when it is still in memory (loaded earlier this session), otherwise taken from the list
	// saved with the scene (PrefabInstance::overrides). Instances with no hash yet (placed before
	// this existed) are not touched, only marked as matching the file from then on.

	// Updates every instance in the world whose prefab changed since it was last updated. One
	// undo step ("Update Prefab Instances"); marks the scene dirty if anything changed. Does
	// nothing in Play Mode. Returns how many instances were updated. Runs by itself when a scene
	// is opened (InstallSceneHooks), on Asset Browser Refresh, and once at startup.
	std::size_t UpdateChangedInstances(World& world);

	// The startup scene was loaded before the hooks existed: call every frame (the Hierarchy
	// does); runs UpdateChangedInstances once, as soon as the asset index is up.
	void RunStartupCheck(World& world);

	// ---- Writing a .cprefab from elsewhere (Prefab Mode) ----

	// Call just BEFORE replacing the file: keeps the current version in memory, so instances can
	// work out their overrides against it exactly when they update.
	void RememberPrefabVersion(std::uint64_t prefabAsset);

	// Call just AFTER replacing the file: drops the cached copies and loads the new version.
	void PrefabFileReplaced(std::uint64_t prefabAsset);

	// ---- Nested prefabs (format 4, see Scene/Prefab.h) ----
	//
	// An outer prefab's file holds a full copy of each prefab nested in it, whose entries also
	// link to the inner prefab. A scene instance links to the outer prefab only: its parts have
	// the outer file's entry ids, so three nested Windows never share an identity. When an inner
	// prefab changes, the outer FILE is updated (keeping what it overrode), its content hash
	// changes, and scenes follow through the usual UpdateChangedInstances.

	// True if `outer` holds `target` at any depth.
	bool PrefabContains(std::uint64_t outer, std::uint64_t target);

	// Brings every prefab file that holds `changed`, at any depth, up to date, inner ones first,
	// and saves those that changed. Files in `skip` (open in Prefab Mode) are left alone. Not
	// undoable: they are other files, and git has the old versions. Returns the rewritten ones.
	std::vector<std::uint64_t> UpdatePrefabsContaining(std::uint64_t changed, std::vector<std::uint64_t> const& skip);

	// The other direction: brings the prefabs nested in `asset`, at any depth, up to date in
	// their files, innermost first, then `asset`'s own file. Prefab Mode runs it before opening
	// a prefab. Files in `skip` are left alone (neither rewritten nor looked into). Not undoable.
	// Returns the rewritten ones.
	std::vector<std::uint64_t> BringNestedUpToDate(std::uint64_t asset, std::vector<std::uint64_t> const& skip);

	// ---- Every scene at once ("Apply to All Scenes...") ----

	// The scene files other than the open one that contain an instance of the prefab, or of a
	// prefab holding it (virtual paths, sorted). Found by the guid in their text, so no scene is
	// loaded to find out.
	std::vector<std::string> ScenesUsing(std::uint64_t prefabAsset);

	// Brings the prefab's instances in every other scene file up to date (the same update as
	// scene open, overrides kept) and saves the files that changed. Each scene is loaded into its
	// own scratch World, never the open one. NOT undoable - other files; git has the old
	// versions. Not in Play Mode. Returns how many scene files were rewritten. The root bar's
	// "Apply to All Scenes..." runs ApplyToPrefab for this scene first, then this.
	std::size_t UpdateOtherScenes(std::uint64_t prefabAsset);
}
