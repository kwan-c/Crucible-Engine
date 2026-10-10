/*!
* @file			ImGuiPanel_Hierarchy.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's Hierarchy: a clickable tree of every entity in the open scene.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
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
#include <Graphics/ParticlePresets.h>
#include <imgui_internal.h>   // BeginDragDropTargetCustom (drop on empty space)
#include <cstring>

#include <memory>
#include <algorithm>

namespace {
	// Drag-and-drop payload for a Hierarchy row (the dragged Entity, by value).
	constexpr char const* kEntityPayload = "CRUCIBLE_ENTITY";
}

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
				if (ImGui::BeginMenu("Light Objects")) {
					if (ImGui::MenuItem("Create Directional Light")) {
						CreateLightObject("Directional");
					}
					if (ImGui::MenuItem("Create Point Light")) {
						CreateLightObject("Point");
					}
					if (ImGui::MenuItem("Create Spot Light")) {
						CreateLightObject("Spot");
					}
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Create Camera")) {
					CreateCameraObject();
				}
				if (ImGui::BeginMenu("Particle Effects")) {
					for (char const* effect : { "Sparks", "Fire", "Smoke", "Splash" }) {
						if (ImGui::MenuItem(effect)) { CreateParticleObject(effect); }
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
		m_rows.clear();
		m_query->ForEach([this, dt](Entity entity, const HierarchyNode& node) {
			if (node.parent == kNullEntity) { DrawEntityNode(entity); }
		});

		// Shift+click ranges and click-on-release need every row, so they are settled here.
		ResolveClicks();

		// Drop on the empty space below the tree: make the dragged entity a root again. A custom
		// target over a plain rectangle (not an item), so right-clicking that space still opens the
		// window's context menu below.
		{
			ImVec2 const min = ImGui::GetCursorScreenPos();
			ImVec2 const max = ImVec2(min.x + ImGui::GetContentRegionAvail().x,
			                          min.y + (std::max)(ImGui::GetContentRegionAvail().y, ImGui::GetFrameHeight()));
			// A plain click on the empty space clears the selection (Ctrl/Shift+click leaves it).
			ImGuiIO const& io = ImGui::GetIO();
			if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
				&& !io.KeyCtrl && !io.KeyShift) {
				if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) {
					if (inspector->SelectionCount() > 0) { inspector->SetSelectedEntity(kNullEntity); }
				}
			}

			if (ImGui::BeginDragDropTargetCustom(ImRect(min, max), ImGui::GetID("##HierarchyRootDrop"))) {
				if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(kEntityPayload)) {
					Entity dragged{};
					std::memcpy(&dragged, payload->Data, sizeof(Entity));
					m_pendingParent = kNullEntity;
					RequestAction(PendingAction::Reparent, dragged);
				}
				ImGui::EndDragDropTarget();
			}
		}

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

			// Ctrl+A: select every entity. Only while the Hierarchy is focused (default route), so
			// it never steals Ctrl+A from a text field elsewhere.
			if (inspector && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
				std::vector<Entity> all;
				m_query->ForEach([&all](Entity entity, const HierarchyNode&) { all.push_back(entity); });
				inspector->SetSelection(all, selected);
			}
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
	bool const selected = inspector && inspector->IsSelected(entity);
	if (selected) { flags |= ImGuiTreeNodeFlags_Selected; }
	m_rows.push_back(entity);

	ImGui::PushID(static_cast<int>(entity.index));

	// No Name/Tag component exists yet - label by raw index until one does.
	bool const open = ImGui::TreeNodeEx("##node", flags, "Entity %u", entity.index);

	// The active entity (the one the Inspector shows) gets a thin accent bar on the left.
	if (selected && inspector->SelectionCount() > 1 && inspector->GetSelectedEntity() == entity) {
		ImVec2 const rmin = ImGui::GetItemRectMin();
		ImVec2 const rmax = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddRectFilled(rmin, ImVec2(rmin.x + 2.0f, rmax.y), ImGui::GetColorU32(ImGuiCol_CheckMark));
	}

	// Clicking the arrow only opens/closes the node; it never changes the selection.
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && inspector) {
		ImGuiIO const& io = ImGui::GetIO();
		if (io.KeyCtrl) {
			inspector->ToggleSelected(entity);   // Ctrl+click: add / remove this row
			m_rangeAnchor = entity;
		}
		else if (io.KeyShift) {
			m_pendingRangeTo = entity;           // Shift+click: range, settled in ResolveClicks
		}
		else if (selected && inspector->SelectionCount() > 1) {
			// Could be the start of dragging the whole selection: only narrow it to this row
			// if the button comes back up without a drag (ResolveClicks).
			m_selectOnRelease = entity;
		}
		else {
			inspector->SetSelectedEntity(entity);
			m_rangeAnchor = entity;
		}
	}

	// Drag this row onto another row to make it that entity's child. Dragging a selected row
	// drags the whole selection (see ActionTargets).
	if (ImGui::BeginDragDropSource()) {
		ImGui::SetDragDropPayload(kEntityPayload, &entity, sizeof(Entity));
		if (selected && inspector->SelectionCount() > 1) { ImGui::Text("%zu entities", inspector->SelectionCount()); }
		else                                             { ImGui::Text("Entity %u", entity.index); }
		ImGui::EndDragDropSource();
	}
	if (ImGui::BeginDragDropTarget()) {
		if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(kEntityPayload)) {
			Entity dragged{};
			std::memcpy(&dragged, payload->Data, sizeof(Entity));
			m_pendingParent = entity;
			RequestAction(PendingAction::Reparent, dragged);   // deferred: we are inside the query loop
		}
		ImGui::EndDragDropTarget();
	}

	// Right-click menu. BeginPopupContextItem opens a popup for the tree node above on right-click
	// and keeps it open across frames until the user picks something or clicks away.
	if (ImGui::BeginPopupContextItem("EntityContext")) {
		// Right-clicking a row outside the selection selects just that row; right-clicking inside
		// a multi-selection keeps it, and the menu acts on all of it.
		if (ImGui::IsWindowAppearing() && inspector && !inspector->IsSelected(entity)) {
			inspector->SetSelectedEntity(entity);
			m_rangeAnchor = entity;
		}
		std::size_t const count = (inspector && inspector->IsSelected(entity)) ? inspector->SelectionCount() : 1;
		std::string const deleteLabel = count > 1 ? "Delete " + std::to_string(count) + " Entities" : std::string("Delete Entity");
		// All deferred: we are inside m_query->ForEach here, and creating or destroying an entity
		// mid-iteration is a structural change the World rejects (it throws). Run after the loop.
		if (ImGui::MenuItem("Copy", "Ctrl+C"))                              { RequestAction(PendingAction::Copy, entity); }
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboard.empty())) { RequestAction(PendingAction::Paste, entity); }
		if (ImGui::MenuItem("Duplicate", "Ctrl+D"))                         { RequestAction(PendingAction::Duplicate, entity); }
		ImGui::Separator();
		if (ImGui::MenuItem(deleteLabel.c_str(), "Del"))                    { RequestAction(PendingAction::Delete, entity); }
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

void Crucible::Hierarchy::ResolveClicks() {
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (!inspector) { m_pendingRangeTo = m_selectOnRelease = kNullEntity; return; }

	auto const rowOf = [this](Entity e) { return std::find(m_rows.begin(), m_rows.end(), e); };

	// Shift+click: every row from the anchor (last plain/Ctrl click) to the clicked row, top to
	// bottom as shown. Without a usable anchor it acts like a plain click.
	if (m_pendingRangeTo.IsValid()) {
		Entity const to = m_pendingRangeTo;
		m_pendingRangeTo = kNullEntity;

		Entity anchor = m_rangeAnchor;
		if (!anchor.IsValid() || !m_world.IsAlive(anchor) || rowOf(anchor) == m_rows.end()) { anchor = inspector->GetSelectedEntity(); }

		auto const a = rowOf(anchor);
		auto const b = rowOf(to);
		if (anchor.IsValid() && a != m_rows.end() && b != m_rows.end()) {
			auto const first = (std::min)(a, b);
			auto const last  = (std::max)(a, b);
			inspector->SetSelection(std::vector<Entity>(first, last + 1), to);   // the clicked row is active
		}
		else {
			inspector->SetSelectedEntity(to);
			m_rangeAnchor = to;
		}
	}

	// Plain click on a row that was part of a multi-selection: on release, select only that row -
	// unless the press turned into a drag (dragging the whole selection onto a new parent).
	if (m_selectOnRelease.IsValid()) {
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			ImGuiIO const& io = ImGui::GetIO();
			bool const dragged = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] > io.MouseDragThreshold * io.MouseDragThreshold;
			if (!dragged && m_world.IsAlive(m_selectOnRelease)) {
				inspector->SetSelectedEntity(m_selectOnRelease);
				m_rangeAnchor = m_selectOnRelease;
			}
			m_selectOnRelease = kNullEntity;
		}
		else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			m_selectOnRelease = kNullEntity;   // release was missed (window lost focus): forget it
		}
	}
}

std::vector<Crucible::Entity> Crucible::Hierarchy::TopLevel(std::vector<Entity> const& entities) const {
	auto const inList = [&entities](Entity e) { return std::find(entities.begin(), entities.end(), e) != entities.end(); };

	std::vector<Entity> top;
	for (Entity const e : entities) {
		if (!m_world.IsAlive(e) || std::find(top.begin(), top.end(), e) != top.end()) { continue; }
		bool covered = false;   // a parent/ancestor is in the list too: it carries this one along
		for (Entity p = m_world.GetParent(e); p.IsValid(); p = m_world.GetParent(p)) {
			if (inList(p)) { covered = true; break; }
		}
		if (!covered) { top.push_back(e); }
	}

	// Row order (top to bottom as shown), so copies and moves keep the order the user sees.
	// Entities inside a collapsed node have no row: they go last, in their original order.
	auto const rowIndex = [this](Entity e) {
		auto const it = std::find(m_rows.begin(), m_rows.end(), e);
		return it == m_rows.end() ? m_rows.size() : static_cast<std::size_t>(it - m_rows.begin());
	};
	std::stable_sort(top.begin(), top.end(), [&](Entity a, Entity b) { return rowIndex(a) < rowIndex(b); });
	return top;
}

std::vector<Crucible::Entity> Crucible::Hierarchy::ActionTargets(Entity target) const {
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (inspector && inspector->SelectionCount() > 1 && inspector->IsSelected(target)) {
		return TopLevel(inspector->GetSelection());
	}
	return { target };
}

namespace {
	// "Delete Entity" / "Delete 3 Entities", for the Edit menu.
	std::string CountLabel(std::string const& verb, std::size_t count) {
		return count == 1 ? verb + " Entity" : verb + " " + std::to_string(count) + " Entities";
	}
}

void Crucible::Hierarchy::RunPendingAction() {
	PendingAction const action = m_pendingAction;
	Entity const target = m_pendingTarget;
	m_pendingAction = PendingAction::None;
	m_pendingTarget = kNullEntity;
	if (action == PendingAction::None) { return; }

	bool const targetAlive = !(target == kNullEntity) && m_world.IsAlive(target);
	std::vector<Entity> const targets = targetAlive ? ActionTargets(target) : std::vector<Entity>{};

	switch (action) {
	case PendingAction::None:
		break;

	case PendingAction::Copy:
		if (!targets.empty()) {
			m_clipboard.clear();
			std::size_t total = 0;
			for (Entity const t : targets) {
				m_clipboard.push_back(CopySubtree(t));
				total += m_clipboard.back().size();
			}
			if (targets.size() == 1) { CRU_USER_INFO("Copied entity {} ({} entit{})", targets.front().index, total, total == 1 ? "y" : "ies"); }
			else                     { CRU_USER_INFO("Copied {} entities ({} in all, with their children)", targets.size(), total); }
		}
		break;

	case PendingAction::Paste: {
		if (m_clipboard.empty()) { break; }
		// Pasted next to the selected entity (same parent), or as roots if nothing is selected.
		Entity const parent = targetAlive ? m_world.GetParent(target) : kNullEntity;
		std::vector<Entity> roots;
		for (auto const& subtree : m_clipboard) { roots.push_back(PasteSubtree(subtree, parent)); }
		RecordCreated(roots, "Paste");
		break;
	}

	case PendingAction::Duplicate: {
		// Does not touch the clipboard, same as Unity's Ctrl+D.
		// Each copy is placed right after its original instead of at the end of the list.
		std::vector<Entity> roots;
		for (Entity const t : targets) {
			std::uint32_t const after = m_world.GetSiblingIndex(t) + 1;
			roots.push_back(PasteSubtree(CopySubtree(t), m_world.GetParent(t), static_cast<int>(after)));
		}
		RecordCreated(roots, "Duplicate");
		break;
	}

	case PendingAction::Delete:
		DeleteEntities(targets);
		break;

	case PendingAction::Reparent:
		ReparentAll(targets, m_pendingParent);
		m_pendingParent = kNullEntity;
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

Crucible::Entity Crucible::Hierarchy::PasteSubtree(std::vector<ClipNode> const& nodes, Entity parent, int siblingIndex) {
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
				CRU_WARN("[Hierarchy] Paste: could not parent entity {} - left as a root", entity.index);
			}
		}
		created.push_back(entity);
	}

	Entity const root = created.front();

	// Before the undo step is captured, so redo puts it back in the same place.
	if (siblingIndex >= 0 && !(m_world.GetParent(root) == kNullEntity)) {
		m_world.SetSiblingIndex(root, static_cast<std::uint32_t>(siblingIndex));
	}
	return root;
}

void Crucible::Hierarchy::RecordCreated(std::vector<Entity> const& roots, std::string const& verb) {
	std::vector<Entity> alive;
	for (Entity const r : roots) { if (r.IsValid() && m_world.IsAlive(r)) { alive.push_back(r); } }
	if (alive.empty()) { return; }

	std::string const label = CountLabel(verb, alive.size());

	// Select first: commits any open Inspector edit so it lands in the history before this step.
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) {
		inspector->SetSelection(alive);
	}
	m_rangeAnchor = alive.back();

	// A paste is a delete run backwards: capture each new subtree as a delete, then invert it.
	// Captured in creation order, so a redo recreates them in the same order (and the same
	// sibling positions) as the original action.
	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	std::vector<std::unique_ptr<EditorCommand>> parts;
	for (Entity const root : alive) {
		parts.push_back(std::make_unique<InvertedCommand>(DeleteEntityCommand::Capture(history, root, label), label));
	}
	history.Push(GroupCommand::Make(std::move(parts), label));

	m_scenes.MarkDirty();
	CRU_USER_INFO("{}: created {}", label, alive.size() == 1 ? "entity " + std::to_string(alive.front().index)
	                                                          : std::to_string(alive.size()) + " entities");
}

void Crucible::Hierarchy::DeleteEntities(std::vector<Entity> const& entities) {
	if (entities.empty()) { return; }

	// Finish any Inspector edit first (it may be on an entity about to go), then drop the
	// selection if it includes anything being deleted.
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (inspector) {
		inspector->CommitPendingEdit();
		bool const touchesSelection = std::any_of(entities.begin(), entities.end(), [&](Entity e) { return inspector->IsSelected(e); });
		if (touchesSelection) { inspector->SetSelectedEntity(kNullEntity); }
	}

	std::string const label = CountLabel("Delete", entities.size());
	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	std::vector<std::unique_ptr<EditorCommand>> parts;

	for (Entity const entity : entities) {
		if (!m_world.IsAlive(entity)) { continue; }   // already gone with an earlier one's subtree
		// Snapshot the entity and its children BEFORE destroying; the step is pushed after.
		auto command = DeleteEntityCommand::Capture(history, entity, label);
		m_world.DestroyEntity(entity);   // also destroys its children
		parts.push_back(std::move(command));
	}
	if (parts.empty()) { return; }

	std::size_t const deleted = parts.size();
	history.Push(GroupCommand::Make(std::move(parts), label));

	m_scenes.MarkDirty();
	if (deleted == 1) { CRU_USER_INFO("Deleted entity {}", entities.front().index); }
	else              { CRU_USER_INFO("Deleted {} entities", deleted); }
}

void Crucible::Hierarchy::ReparentAll(std::vector<Entity> const& children, Entity parent) {
	if (!(parent == kNullEntity) && !m_world.IsAlive(parent)) { return; }

	// The drop target itself can be part of the dragged selection: it stays where it is.
	std::vector<Entity> moving;
	for (Entity const c : children) { if (!(c == parent)) { moving.push_back(c); } }
	if (moving.empty()) { return; }

	std::string const label = CountLabel(parent == kNullEntity ? "Unparent" : "Parent", moving.size());
	std::vector<std::unique_ptr<EditorCommand>> parts;
	for (Entity const child : moving) {   // row order, so they keep their order under the new parent
		if (auto command = Reparent(child, parent, label)) { parts.push_back(std::move(command)); }
	}
	if (parts.empty()) { return; }

	std::size_t const moved = parts.size();
	EditorManager::GetInstance().GetHistory().Push(GroupCommand::Make(std::move(parts), label));
	m_scenes.MarkDirty();

	if (moved > 1) {
		if (parent == kNullEntity) { CRU_USER_INFO("{} entities are now roots", moved); }
		else                       { CRU_USER_INFO("{} entities are now children of entity {}", moved, parent.index); }
	}
	else if (parent == kNullEntity) { CRU_USER_INFO("Entity {} is now a root", moving.front().index); }
	else                            { CRU_USER_INFO("Entity {} is now a child of entity {}", moving.front().index, parent.index); }
}

std::unique_ptr<Crucible::EditorCommand> Crucible::Hierarchy::Reparent(Entity child, Entity parent, std::string const& label) {
	if (child == parent) { return nullptr; }                       // dropped onto itself
	if (!m_world.IsAlive(child)) { return nullptr; }
	if (m_world.GetParent(child) == parent) { return nullptr; }   // dropped where it already is
	if (!(parent == kNullEntity) && !m_world.IsAlive(parent)) { return nullptr; }

	Transform const* local = m_world.Read<Transform>(child);
	if (!local) { return nullptr; }

	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	Entity const oldParent = m_world.GetParent(child);
	ReparentCommand::Placement const before{
		oldParent == kNullEntity ? kNullEntity : history.KeyOf(oldParent),
		m_world.GetSiblingIndex(child),
		*local };

	// Keep the world position (Unity's default), so the object doesn't jump when it is parented.
	ParentResult result = m_world.TrySetParent(child, parent, true);
	if (result == ParentResult::UnsupportedTransform) {
		// The new parent has a zero/negative scale, so no local Transform can keep the world pose.
		CRU_USER_WARN("Entity {}: can't keep its world position under entity {} ({}) - kept its local Transform instead",
		              child.index, parent.index, ToString(result));
		result = m_world.TrySetParent(child, parent, false);
	}
	if (result != ParentResult::Ok) {
		// e.g. WouldCycle: dropping a parent onto one of its own children.
		CRU_USER_WARN("Can't parent entity {} to entity {}: {}", child.index, parent.index, ToString(result));
		return nullptr;
	}

	ReparentCommand::Placement const after{
		parent == kNullEntity ? kNullEntity : history.KeyOf(parent),
		m_world.GetSiblingIndex(child),
		*m_world.Read<Transform>(child) };

	return std::make_unique<ReparentCommand>(history.KeyOf(child), before, after, label);
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

void Crucible::Hierarchy::CreateLightObject(const std::string& lightType) {

	auto e = m_world.CreateEntity();
	Light light{};

	if (lightType == "Directional") {
		light.type = LightType::Directional;
		light.intensity = 1.f;
	}
	else if (lightType == "Point") {
		light.type = LightType::Point;
		light.intensity = 10.f;
	}
	else if (lightType == "Spot") {
		light.type = LightType::Spot;
		light.intensity = 10.f;
	}
	else {
		CRU_USER_WARN("Unknown light type: {}", lightType);
		return;
	}

	m_world.AddComponent<Light>(e, light);

	// Select first: that commits any Inspector edit still open on the previous entity, so it
	// lands in the history BEFORE this create - the order they actually happened in.
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR))
		inspector->SetSelectedEntity(e);

	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	history.Push(std::make_unique<CreateEntityCommand>(
		history.KeyOf(e), EntityState::Capture(m_world, e), "Create " + lightType));

	m_scenes.MarkDirty();   // records that there are unsaved changes

}

void Crucible::Hierarchy::CreateCameraObject() {

	auto e = m_world.CreateEntity();
	m_world.AddComponent<CameraComponent>(e, CameraComponent{});

	// Somewhere useful rather than the origin: back and up a little, looking down -Z at the origin.
	if (Transform* t = m_world.Write<Transform>(e)) { t->positions = { 0.f, 1.5f, 6.f }; }

	// Select first, for the same history-ordering reason as CreateLightObject.
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR))
		inspector->SetSelectedEntity(e);

	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	history.Push(std::make_unique<CreateEntityCommand>(
		history.KeyOf(e), EntityState::Capture(m_world, e), "Create Camera"));

	m_scenes.MarkDirty();   // records that there are unsaved changes

}

void Crucible::Hierarchy::CreateParticleObject(const std::string& effect) {

	ParticlePreset preset;
	if      (effect == "Sparks") { preset = ParticlePreset::Sparks; }
	else if (effect == "Fire")   { preset = ParticlePreset::Fire; }
	else if (effect == "Smoke")  { preset = ParticlePreset::Smoke; }
	else if (effect == "Splash") { preset = ParticlePreset::Splash; }
	else {
		CRU_USER_WARN("Unknown particle effect: {}", effect);
		return;
	}

	auto e = m_world.CreateEntity();
	m_world.AddComponent<ParticleEmitter>(e, MakeParticlePreset(preset));

	// A little above the ground, so the effect is visible straight away.
	if (Transform* t = m_world.Write<Transform>(e)) { t->positions = { 0.f, 0.5f, 0.f }; }

	// Select first, for the same history-ordering reason as CreateLightObject.
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR))
		inspector->SetSelectedEntity(e);

	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	history.Push(std::make_unique<CreateEntityCommand>(
		history.KeyOf(e), EntityState::Capture(m_world, e), "Create " + effect));

	m_scenes.MarkDirty();   // records that there are unsaved changes

}
