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
#include "Undo/EntityCommands.h"
#include "Diagnostics/Logger.h"

#include <memory>

void Crucible::Hierarchy::Init() {
	m_panelName = "Hierarchy";

	// Left column above the bottom area (Unity default layout).
	m_docked = true;
	m_dockArea = DockArea::Left;
	m_dockRatio = 0.22f;

	// ImGuiFlags...
	m_flags |= ImGuiWindowFlags_MenuBar;
}

void Crucible::Hierarchy::Update(float dt) {
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
		m_query->ForEach([this, dt](Entity entity, const HierarchyNode& node) {
			if (node.parent == kNullEntity) { DrawEntityNode(entity); }
		});

		// Right-click on empty space in the panel: paste as a new root entity.
		if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboard.empty())) { RequestAction(PendingAction::Paste, kNullEntity); }
			ImGui::EndPopup();
		}

		// Keyboard shortcuts - same rules as the Menu panel's Ctrl+Z: not while a popup is open or a
		// widget is held, and a focused text field keeps Ctrl+C/V for itself (its route wins).
		if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && !ImGui::IsAnyItemActive()) {
			auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
			Entity const selected = inspector ? inspector->GetSelectedEntity() : kNullEntity;
			bool const hasSelection = !(selected == kNullEntity) && m_world.IsAlive(selected);

			if (hasSelection && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, ImGuiInputFlags_RouteGlobal)) { RequestAction(PendingAction::Copy, selected); }
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V, ImGuiInputFlags_RouteGlobal))                  { RequestAction(PendingAction::Paste, selected); }
			if (hasSelection && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, ImGuiInputFlags_RouteGlobal)) { RequestAction(PendingAction::Duplicate, selected); }
			if (hasSelection && ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal))            { RequestAction(PendingAction::Delete, selected); }
		}

		// Now that the query has finished iterating, it is safe to change the World.
		RunPendingAction();
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

	// Right-click menu. BeginPopupContextItem opens a popup for the tree node above on right-click
	// and keeps it open across frames until the user picks something or clicks away.
	if (ImGui::BeginPopupContextItem("EntityContext")) {
		if (ImGui::IsWindowAppearing() && inspector) { inspector->SetSelectedEntity(entity); }
		// All deferred: we are inside m_query->ForEach here, and creating or destroying an entity
		// mid-iteration is a structural change the World rejects (it throws). Run after the loop.
		if (ImGui::MenuItem("Copy", "Ctrl+C"))                              { RequestAction(PendingAction::Copy, entity); }
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboard.empty())) { RequestAction(PendingAction::Paste, entity); }
		if (ImGui::MenuItem("Duplicate", "Ctrl+D"))                         { RequestAction(PendingAction::Duplicate, entity); }
		ImGui::Separator();
		if (ImGui::MenuItem("Delete Entity", "Del"))                        { RequestAction(PendingAction::Delete, entity); }
		ImGui::EndPopup();
	}

	// NoTreePushOnOpen (set above for a leaf) means no matching push happened regardless of
	// what TreeNodeEx returned, so only ever call TreePop when this node actually has children.
	if (childCount > 0 && open) {
		m_world.ForEachChild(entity, [this](Entity child) { DrawEntityNode(child); });
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void Crucible::Hierarchy::RunPendingAction() {
	PendingAction const action = m_pendingAction;
	Entity const target = m_pendingTarget;
	m_pendingAction = PendingAction::None;
	m_pendingTarget = kNullEntity;

	bool const targetAlive = !(target == kNullEntity) && m_world.IsAlive(target);

	switch (action) {
	case PendingAction::None:
		break;

	case PendingAction::Copy:
		if (targetAlive) {
			m_clipboard = CopySubtree(target);
			CRU_USER_INFO("Copied entity {} ({} entit{})", target.index, m_clipboard.size(), m_clipboard.size() == 1 ? "y" : "ies");
		}
		break;

	case PendingAction::Paste:
		if (m_clipboard.empty()) { break; }
		// Pasted next to the selected entity (same parent), or as a root if nothing is selected.
		PasteSubtree(m_clipboard, targetAlive ? m_world.GetParent(target) : kNullEntity, "Paste Entity");
		break;

	case PendingAction::Duplicate:
		if (targetAlive) {
			// Does not touch the clipboard, same as Unity's Ctrl+D.
			// Placed right after the original instead of at the end of the list.
			std::uint32_t const after = m_world.GetSiblingIndex(target) + 1;
			PasteSubtree(CopySubtree(target), m_world.GetParent(target), "Duplicate Entity", static_cast<int>(after));
		}
		break;

	case PendingAction::Delete:
		if (targetAlive) { DeleteEntity(target); }
		break;
	}
}

std::vector<Crucible::Hierarchy::ClipNode> Crucible::Hierarchy::CopySubtree(Entity root) const {
	std::vector<ClipNode> nodes;
	// Depth-first, parent before children, children first-to-last, so pasting appends each child
	// to an already-created parent in the original sibling order.
	auto visit = [&](auto&& self, Entity node, int parent) -> void {
		int const index = static_cast<int>(nodes.size());
		nodes.push_back({ parent, EntityState::Capture(m_world, node) });
		m_world.ForEachChild(node, [&](Entity child) { self(self, child, index); });
	};
	visit(visit, root, -1);
	return nodes;
}

Crucible::Entity Crucible::Hierarchy::PasteSubtree(std::vector<ClipNode> const& nodes, Entity parent, std::string const& label, int siblingIndex) {
	if (nodes.empty()) { return kNullEntity; }

	std::vector<Entity> created;
	created.reserve(nodes.size());

	for (ClipNode const& node : nodes) {
		Entity const entity = m_world.CreateEntity();
		EntityState::Apply(m_world, entity, node.snapshot);

		Entity const newParent = node.parent >= 0 ? created[static_cast<std::size_t>(node.parent)] : parent;
		if (!(newParent == kNullEntity) && m_world.IsAlive(newParent)) {
			// false: the snapshot holds the LOCAL Transform, so the copy sits exactly where the
			// original did when pasted under the same parent.
			if (m_world.TrySetParent(entity, newParent, false) != ParentResult::Ok) {
				CRU_WARN("[Hierarchy] {}: could not parent entity {} - left as a root", label, entity.index);
			}
		}
		created.push_back(entity);
	}

	Entity const root = created.front();

	// Before the undo step is captured, so redo puts it back in the same place.
	if (siblingIndex >= 0 && !(m_world.GetParent(root) == kNullEntity)) {
		m_world.SetSiblingIndex(root, static_cast<std::uint32_t>(siblingIndex));
	}

	// Select first: commits any open Inspector edit so it lands in the history before this step.
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) {
		inspector->SetSelectedEntity(root);
	}

	// A paste is a delete run backwards: capture the new subtree as a delete, then invert it.
	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	history.Push(std::make_unique<InvertedCommand>(DeleteEntityCommand::Capture(history, root, label), label));

	m_scenes.MarkDirty();
	CRU_USER_INFO("{}: created entity {} ({} entit{})", label, root.index, created.size(), created.size() == 1 ? "y" : "ies");
	return root;
}

void Crucible::Hierarchy::DeleteEntity(Entity entity) {
	// Deselect first so the Inspector commits any open edit and stops drawing the entity.
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (inspector && inspector->GetSelectedEntity() == entity) { inspector->SetSelectedEntity(kNullEntity); }

	// Snapshot the entity and its children BEFORE destroying, then record the delete
	// (commands are pushed after their action has been applied).
	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	auto command = DeleteEntityCommand::Capture(history, entity, "Delete Entity");

	m_world.DestroyEntity(entity);   // also destroys its children
	history.Push(std::move(command));

	m_scenes.MarkDirty();
	CRU_USER_INFO("Deleted entity {}", entity.index);
}

void Crucible::Hierarchy::CreatePrimitive(const std::string& meshName) {

	auto e = m_world.CreateEntity();
	MeshRenderer mr{};

	auto id = m_resource.BuiltinId(meshName.c_str());
	// AssignMesh sets BOTH meshAsset (what the scene file saves) and the runtime mesh handle.
	// Setting only mr.mesh left meshAsset = 0, so the mesh was lost on save/reload.
	m_resource.AssignMesh(mr, id);
	mr.colour = { .5f, .5f, .5f, .5f };        // default colour is (0,0,0,0)
	m_world.AddComponent<MeshRenderer>(e, mr);

	// Select first: that commits any Inspector edit still open on the previous entity, so it
	// lands in the history BEFORE this create - the order they actually happened in.
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR))
		inspector->SetSelectedEntity(e);

	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	history.Push(std::make_unique<CreateEntityCommand>(
		history.KeyOf(e), EntityState::Capture(m_world, e), "Create " + meshName));

	m_scenes.MarkDirty();   // records that there are unsaved changes
}
