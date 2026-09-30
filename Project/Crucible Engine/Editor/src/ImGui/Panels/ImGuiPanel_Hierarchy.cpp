/*!
* @file			ImGuiPanel_Hierarchy.cpp
* @brief		The editor's Hierarchy: a clickable tree of every entity in the open scene.
*/
#include <ImGui/ImGuiManager.h>
#include <ECS/World.h>
#include <ECS/Components/Components.h>
#include "Resources/ResourceManager.h"
#include <Scene/SceneManager.h>

#include "ImGui/Panels/ImGuiPanel_Hierarchy.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"

void Crucible::Hierarchy::Init() {
	m_panelName = "Hierarchy";

	// Left column above the bottom area (Unity default layout).
	m_docked = true;
	m_dockArea = DockArea::Left;
	m_dockRatio = 0.22f;

	// ImGuiFlags...
	m_flags |= ImGuiWindowFlags_MenuBar;
}

void Crucible::Hierarchy::Update(float) {
	if (ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("+")) {
				if (ImGui::BeginMenu("3D Objects")) {
					if (ImGui::MenuItem("Create Cube")) {
						CreatePrimitive("Cube");
					}
					if (ImGui::MenuItem("Create Sphere")) {
						CreatePrimitive("Sphere");
					}
					if (ImGui::MenuItem("Create Cylinder")) {
						CreatePrimitive("Cylinder");
					}
					if (ImGui::MenuItem("Create Cone")) {
						CreatePrimitive("Cone");
					}
					if (ImGui::MenuItem("Create Pyramid")) {
						CreatePrimitive("Pyramid");
					}
					if (ImGui::MenuItem("Create Capsule")) {
						CreatePrimitive("Capsule");
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("2D Objects")) {
					if (ImGui::MenuItem("Create Plane")) {
						CreatePrimitive("Plane");
					}
					if (ImGui::MenuItem("Create Disc")) {
						CreatePrimitive("Disc");
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}


		if (!m_query) { m_query.emplace(m_world.CreateQuery<const HierarchyNode>()); }

		// This world call, and every one made from DrawEntityNode below, runs from an ImGui
		// layer's OnUpdate() - after ECSSystemManager::Update() has already finished for this
		// frame (see Core::Run()) - so none of them are inside any IECSSystem's Update(), and
		// the ECS's per-call access-declaration checks (CheckDeclaredAccess/CheckHierarchyRead)
		// are no-ops here, same as for any other editor-only, main-thread ImGui code.
		m_query->ForEach([this](Entity entity, const HierarchyNode& node) {
			if (node.parent == kNullEntity) { DrawEntityNode(entity); }
		});
	}
	ImGui::End();
}

void Crucible::Hierarchy::DrawEntityNode(Entity entity) {
	std::uint32_t const childCount = m_world.ChildCount(entity);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (childCount == 0) { flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; }

	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (inspector && inspector->GetSelectedEntity() == entity) { flags |= ImGuiTreeNodeFlags_Selected; }

	ImGui::PushID(static_cast<int>(entity.index));

	// No Name/Tag component exists yet - label by raw index until one does.
	bool const open = ImGui::TreeNodeEx("##node", flags, "Entity %u", entity.index);
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && inspector) {
		inspector->SetSelectedEntity(entity);
	}

	// NoTreePushOnOpen (set above for a leaf) means no matching push happened regardless of
	// what TreeNodeEx returned, so only ever call TreePop when this node actually has children.
	if (childCount > 0 && open) {
		m_world.ForEachChild(entity, [this](Entity child) { DrawEntityNode(child); });
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void Crucible::Hierarchy::CreatePrimitive(const std::string& meshName) {

	auto e = m_world.CreateEntity();
	MeshRenderer mr{};

	auto id = m_resource.BuiltinId(meshName.c_str());
	mr.mesh = m_resource.GetMesh(id);
	mr.colour = { .5f, .5f, .5f, .5f };        // default colour is (0,0,0,0)
	m_world.AddComponent<MeshRenderer>(e, mr);

	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR))
		inspector->SetSelectedEntity(e);

	m_scenes.MarkDirty();   // records that there are unsaved changes
}
