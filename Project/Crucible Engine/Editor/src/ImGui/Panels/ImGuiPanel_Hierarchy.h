#pragma once
/*!
* @file			ImGuiPanel_Hierarchy.h
* @brief		The editor's Hierarchy: a clickable tree of every entity in the open scene.
*
* Reserves the left column of the default layout. Lists every root entity (HierarchyNode::parent
* == kNullEntity) and its subtree, and calls Inspector::SetSelectedEntity() when one is clicked -
* fetched each frame via EditorManager::GetInstance().GetPanel<Inspector>(), so Hierarchy never
* needs to own an Inspector& of its own (both panels are built by the same EditorManager::Init(),
* and neither is guaranteed to exist before the other).
*
* There is no Name/Tag component yet (see ECS/Components/Components.h), so entities are labelled
* by their raw index for now ("Entity 3") - swap in a real name lookup once one exists.
*/

#include "ImGui/ImGuiManager.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/Query.h"

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
		enum class PendingAction { None, Copy, Paste, Duplicate, Delete, Reparent };
		PendingAction m_pendingAction = PendingAction::None;
		Entity        m_pendingTarget = kNullEntity;
		Entity        m_pendingParent = kNullEntity;   // Reparent only: the new parent (kNullEntity = root)

		// One copied entity: its component snapshot, and the index of its parent in the same list
		// (-1 for the top entity). Parents always come before their children.
		struct ClipNode { int parent; std::string snapshot; };
		std::vector<ClipNode> m_clipboard;

		void RequestAction(PendingAction action, Entity target) { m_pendingAction = action; m_pendingTarget = target; }
		void RunPendingAction();
		std::vector<ClipNode> CopySubtree(Entity root) const;
		// Creates the clipboard's entities under `parent` (kNullEntity = root), records one undo
		// step, selects the new top entity and returns it.
		// siblingIndex >= 0 moves the new top entity to that position among its siblings.
		Entity PasteSubtree(std::vector<ClipNode> const& nodes, Entity parent, std::string const& label, int siblingIndex = -1);
		void DeleteEntity(Entity entity);
		// Moves `child` under `parent` (kNullEntity = make it a root), keeping its world position,
		// and records one undo step. Refused (with a warning) if it would create a cycle.
		void Reparent(Entity child, Entity parent);

		// Draws one entity's row (and, if expanded, its children) recursively.
		void DrawEntityNode(Entity entity);

		// Creating Primitives (cube, sphere, etc.) is a common editor action, so the "+" menu in the toolbar
		// calls this to spawn a new entity with a MeshRenderer and the requested primitive mesh.
		void CreatePrimitive(const std::string& meshName);

		// Creating Light Objects (directional, point, spot) is a common editor action, so the "+" menu in the toolbar
		// calls this to spawn a new entity with a Light component and the requested light type.
		void CreateLightObject(const std::string& lightType);

		// Spawns a new entity with a Camera component (the Game view looks through it).
		void CreateCameraObject();

		// Spawns a new entity with a ParticleEmitter set up as one of the presets in
		// Graphics/ParticlePresets.h: "Sparks", "Fire", "Smoke" or "Splash".
		void CreateParticleObject(const std::string& effect);
	};

}
