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

		// Draws one entity's row (and, if expanded, its children) recursively.
		void DrawEntityNode(Entity entity);

		// Creating Primitives (cube, sphere, etc.) is a common editor action, so the "+" menu in the toolbar
		// calls this to spawn a new entity with a MeshRenderer and the requested primitive mesh.
		void CreatePrimitive(const std::string& meshName);
	};

}
