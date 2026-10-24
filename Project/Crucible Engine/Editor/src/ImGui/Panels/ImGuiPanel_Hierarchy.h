#pragma once
/*!
* @file			ImGuiPanel_Hierarchy.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's Hierarchy: a clickable tree of every entity in the open scene.
*
* Reserves the left column of the default layout. Lists every root entity (HierarchyNode::parent
* == kNullEntity) and its subtree, and calls Inspector::SetSelectedEntity() when one is clicked -
* fetched each frame via EditorManager::GetInstance().GetPanel<Inspector>(), so Hierarchy never
* needs to own an Inspector& of its own (both panels are built by the same EditorManager::Init(),
* and neither is guaranteed to exist before the other).
*
* Multi-selection: Ctrl+click adds/removes a row, Shift+click selects the range from the last
* clicked row, Ctrl+A selects everything (Hierarchy focused). Copy, Paste, Duplicate, Delete and
* drag-to-parent then act on the whole selection as ONE undo step. When both a parent and its
* child are selected, only the parent is acted on (the child comes along with it).
* Right-click > Create Prefab saves each selected top-level entity, with its children, as a
* .cprefab in Assets/Prefabs (see Prefabs/PrefabAuthoring.h). Dragging rows onto the Asset
* Browser does the same into a folder of your choice.
* Dropping a .cprefab from the Asset Browser on a row places a copy as that entity's child
* (sitting exactly on it); on the empty space below the tree, as a root at the origin
* (see PrefabAuthoring::PlacePrefab). Each placement is one undo step.
*
* Sibling order (Unity's): children are listed - and screen-space UI is drawn - first to last, so
* a later sibling draws on top of an earlier one and gets clicks first. Change it by:
*	drag & drop      top quarter of a row: put the dragged entities just BEFORE it (same parent);
*	                 bottom quarter: just AFTER it (or, on an open row with children, as its FIRST
*	                 child); middle: make them its children (as before). A line / box shows which.
*	right-click      Order > Bring to Front / Move Forward / Move Backward / Send to Back.
*	Ctrl+] / Ctrl+[  move forward / backward one step; with Shift: to front / back. Also in the UI
*	                 Editor tab (UIView calls Reorder / ReorderSelection).
* Roots have no sibling order in the World, so they can't be reordered; whole canvases are ordered
* by UICanvas::sortOrder instead. Each reorder is one undo step (ReparentCommand per entity moved).
*
* Rows are labelled with the entity's EntityName (see ECS/Components/Components.h), falling back
* to its raw index ("Entity 3") if the name is empty.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/ImGuiManager.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/Query.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Crucible {

	class Hierarchy : public EditorPanel {
	public:
		explicit Hierarchy(World& world, ResourceManager& resource, SceneManager& scenes) : m_world(world), m_resource(resource), m_scenes(scenes) {}

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		// ---- Sibling order (draw order for UI: later = on top) ----
		enum class OrderMove { Forward, Backward, ToFront, ToBack };

		// Moves the entities among their siblings, keeping their order relative to each other, as
		// one undo step. Roots are skipped (they have no order). Must not be called while the
		// World is being iterated (the Hierarchy defers its own calls to after its tree loop).
		void Reorder(std::vector<Entity> const& entities, OrderMove move);
		// Reorder() on the Inspector's selection (top-level entities only).
		void ReorderSelection(OrderMove move);
		// "Bring to Front", ... - for menus.
		static char const* OrderLabel(OrderMove move);
		// The four Order menu items (Hierarchy row menu, UI Editor tab's right-click menu). Returns
		// the move picked this frame, if any. `enabled` greys them out (e.g. a root).
		static bool DrawOrderMenuItems(OrderMove& picked, bool enabled);
		// Ctrl+] / Ctrl+[ (+Shift). Returns true and the move if one was pressed this frame.
		static bool OrderShortcutPressed(OrderMove& move, ImGuiInputFlags route = 0);

	private:
		World& m_world;
		SceneManager& m_scenes;
		ResourceManager& m_resource;

		// Every entity carries HierarchyNode (it's a Mandatory<> component - see Components.h),
		// so this query matches the whole scene; roots are filtered out of it below by parent ==
		// kNullEntity. Built once and reused (see World::CreateQuery's own note on why).
		std::optional<Query<const HierarchyNode>> m_query;
		// Actions requested by the right-click menu or a shortcut. They change the World, which is not
		// allowed while m_query is iterating, so they are stored and run after the loop.
		enum class PendingAction { None, Copy, Paste, Duplicate, Delete, Reparent, Place, Reorder, CreatePrefab, PlacePrefab };
		PendingAction m_pendingAction = PendingAction::None;
		std::string   m_pendingPrefab;                 // PlacePrefab: virtual path of the dropped .cprefab
		Entity        m_pendingTarget = kNullEntity;   // the row acted on; if it is selected, the whole selection is
		Entity        m_pendingParent = kNullEntity;   // Reparent / Place: the new parent (kNullEntity = root)

		// Place: a drop between rows. The dragged entities go next to m_pendingAnchor (before or
		// after it), or first under m_pendingParent when there is no anchor.
		enum class DropZone { Into, Before, After, FirstChild };
		Entity        m_pendingAnchor = kNullEntity;
		DropZone      m_pendingZone   = DropZone::Into;
		OrderMove     m_pendingMove   = OrderMove::Forward;   // Reorder

		// One copied entity: its component snapshot, and the index of its parent in the same list
		// (-1 for the top entity). Parents always come before their children.
		struct ClipNode { int parent; std::string snapshot; };
		// One subtree per copied entity (several when a multi-selection was copied).
		std::vector<std::vector<ClipNode>> m_clipboard;

		void RequestAction(PendingAction action, Entity target) { m_pendingAction = action; m_pendingTarget = target; }
		void RunPendingAction();
		// What an action on `target` applies to: the whole selection (top-level entities only, in
		// row order) if `target` is part of a multi-selection, otherwise just `target`.
		std::vector<Entity> ActionTargets(Entity target) const;
		// Drops entities whose parent/ancestor is also in the list, and sorts the rest by row order.
		std::vector<Entity> TopLevel(std::vector<Entity> const& entities) const;

		std::vector<ClipNode> CopySubtree(Entity root) const;
		// Creates one copied subtree under `parent` (kNullEntity = root) and returns its top entity.
		// siblingIndex >= 0 moves the new top entity to that position among its siblings.
		// Records nothing - see RecordCreated.
		Entity PasteSubtree(std::vector<ClipNode> const& nodes, Entity parent, int siblingIndex = -1);
		// Selects the new top entities and records them as one undo step ("Paste 3 Entities").
		void RecordCreated(std::vector<Entity> const& roots, std::string const& verb);
		// Deletes the entities (and their children) as one undo step.
		void DeleteEntities(std::vector<Entity> const& entities);
		// Moves each entity under `parent` (kNullEntity = make it a root), keeping its world
		// position, as one undo step. Entities that would create a cycle are skipped with a warning.
		void ReparentAll(std::vector<Entity> const& children, Entity parent);
		// Moves one entity and returns its undo command (nullptr if nothing changed or refused).
		std::unique_ptr<EditorCommand> Reparent(Entity child, Entity parent, std::string const& label);
		// A drop between rows: moves the entities under `parent` (keeping their world position)
		// and puts them, in row order, before / after `anchor` (or first, with no anchor). One undo step.
		void PlaceAll(std::vector<Entity> const& entities, Entity parent, Entity anchor, DropZone zone);
		// Makes `parent`'s children follow `order` (every child, once), recording one
		// ReparentCommand per child whose index changes (in the order applied, so undoing them
		// backwards restores the original order exactly).
		void ApplyChildOrder(Entity parent, std::vector<Entity> const& order, std::string const& label,
		                     std::vector<std::unique_ptr<EditorCommand>>& parts);
		std::vector<Entity> ChildrenOf(Entity parent) const;

		// ---- Click selection ----
		std::vector<Entity> m_rows;                      // every row drawn this frame, top to bottom
		Entity m_rangeAnchor    = kNullEntity;           // Shift+click selects from here...
		Entity m_pendingRangeTo = kNullEntity;           // ...to here (resolved once every row is known)
		Entity m_selectOnRelease = kNullEntity;          // plain click on a selected row: select only it on release, unless it became a drag
		void ResolveClicks();

		// Draws one entity's row (and, if expanded, its children) recursively.
		void DrawEntityNode(Entity entity);

		// Creates a new entity with only the mandatory base components (Transform, WorldTransform, HierarchyNode - see kMandatoryComponents), default-valued: a root at the origin.
		void CreateEmptyObject();
		void CreateAnimatedModel();

		// Creating Primitives (cube, sphere, etc.) is a common editor action, so the "+" menu in the toolbar
		// calls this to spawn a new entity with a MeshRenderer and the requested primitive mesh.
		void CreatePrimitive(const std::string& meshName);

		// Creating Light Objects (directional, point, spot) is a common editor action, so the "+" menu in the toolbar
		// calls this to spawn a new entity with a Light component and the requested light type.
		void CreateLightObject(const std::string& lightType);

		// Spawns a new entity with a Camera component (the Game view looks through it).
		void CreateCameraObject();

		// A root entity with a UICanvas: where screen-space UI elements go (as its children).
		void CreateCanvasObject();

		// "Image", "Button" or "Slider": a UI element (UIRect + UIImage / UIButton / UISlider) placed under the selected
		// entity if it is part of a UI tree, else under the first screen-space canvas, else under a
		// new canvas made with it. One undo step either way.
		void CreateUIElement(const std::string& kind);

		// Double-click / F: frames the selected entity where it is edited. A screen-space UI element
		// (or its canvas) switches to the UI Editor tab and frames it there; anything else switches
		// to the Scene tab and frames the selection (SceneView::FrameSelected).
		void FocusSelection();

		// The entity just created by CreateUIElement: the rows above it open once so it is visible.
		// Cleared after the tree is drawn.
		Entity m_revealEntity = kNullEntity;

		// Spawns a new entity with a ParticleEmitter set up as one of the presets in
		// Graphics/ParticlePresets.h: "Sparks", "Fire", "Smoke" or "Splash".
		void CreateParticleObject(const std::string& effect);

		// Spawns a new entity with a Water component: a 10 x 10 m surface (the built-in plane,
		// scaled) at the origin. It needs ground under it to show depth, shores and foam.
		void CreateWaterObject();
	};

}
