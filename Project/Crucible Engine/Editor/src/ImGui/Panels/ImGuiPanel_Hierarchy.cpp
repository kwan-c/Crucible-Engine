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
#include "ImGui/Panels/ImGuiPanel_SceneView.h"
#include "ImGui/Panels/ImGuiPanel_UIView.h"
#include "Undo/EntityCommands.h"
#include "ImGui/EditorWidgets.h"
#include "Prefabs/PrefabAuthoring.h"
#include "Diagnostics/Logger.h"
#include <Graphics/ParticlePresets.h>
#include <imgui_internal.h>   // BeginDragDropTargetCustom (drop on empty space)
#include <cstring>

#include <memory>
#include <algorithm>
#include <unordered_set>

namespace {
	// Drag-and-drop payload for a Hierarchy row (the dragged Entity, by value). Shared through
	// EditorWidgets.h because the Asset Browser accepts it too (drop an entity = create a prefab).
	constexpr char const* kEntityPayload = Crucible::EditorWidgets::kEntityPayload;

	// The row label: the entity's name, or "Entity <index>" if it has none.
	std::string LabelOf(Crucible::World const& world, Crucible::Entity entity) {
		Crucible::EntityName const* name = world.Read<Crucible::EntityName>(entity);
		if (name && !name->name.empty()) { return name->name; }
		return "Entity " + std::to_string(entity.index);
	}

	// "Camera" -> "Camera (1)", "Camera (1)" -> "Camera (2)": the first "<base> (k)" no entity
	// uses yet. A " (k)" suffix counts as a copy number only while "<base>" itself is still in
	// the scene, so "Wave (2026)" on its own -> "Wave (2026) (1)", not "Wave (1)".
	// taken holds every name in the scene and receives the new one.
	std::string CopyName(std::string const& original, std::unordered_set<std::string>& taken) {
		std::string base = original;
		std::size_t const open = base.rfind(" (");
		if (open != std::string::npos && base.size() > open + 3 && base.back() == ')' &&
			std::all_of(base.begin() + static_cast<std::ptrdiff_t>(open) + 2, base.end() - 1, [](char c) { return c >= '0' && c <= '9'; }) &&
			taken.count(base.substr(0, open)) != 0) {
			base.erase(open);
		}
		for (int k = 1;; ++k) {
			std::string candidate = base + " (" + std::to_string(k) + ")";
			if (taken.insert(candidate).second) { return candidate; }
		}
	}
}

void Crucible::Hierarchy::Init() {
	m_panelName = "Hierarchy";

	// Prefab override lists are refreshed before every scene save, and instances are brought up
	// to date with changed prefabs whenever a scene is opened (PrefabAuthoring.h).
	PrefabAuthoring::InstallSceneHooks(m_world, m_scenes);

	// Left column above the bottom area (Unity default layout).
	m_docked = true;
	m_dockArea = DockArea::Left;
	m_dockRatio = 0.22f;

	// ImGuiFlags...
	m_flags |= ImGuiWindowFlags_MenuBar;
}

void Crucible::Hierarchy::Update(float dt) {
	// The scene open at startup was loaded before the prefab hooks existed: check it once.
	PrefabAuthoring::RunStartupCheck(m_world);

	if (ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("+")) {
				if (ImGui::MenuItem("Create Empty")) {
					CreateEmptyObject();
				}
			if (ImGui::MenuItem("Create Animated Model")) { CreateAnimatedModel(); }
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
				if (ImGui::BeginMenu("Particle Effects")) {
					for (char const* effect : { "Sparks", "Fire", "Smoke", "Splash" }) {
						if (ImGui::MenuItem(effect)) { CreateParticleObject(effect); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("UI")) {
					if (ImGui::MenuItem("Create Canvas")) {
						CreateCanvasObject();
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Create Image")) {
						CreateUIElement("Image");
					}
					if (ImGui::MenuItem("Create Button")) {
						CreateUIElement("Button");
					}
					if (ImGui::MenuItem("Create Slider")) {
						CreateUIElement("Slider");
					}
					if (ImGui::MenuItem("Create Text")) {
						CreateUIElement("Text");
					}
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Create Camera")) {
					CreateCameraObject();
				}
				if (ImGui::MenuItem("Create Water")) {
					CreateWaterObject();
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
		m_revealEntity = kNullEntity;   // its rows were opened this frame (see DrawEntityNode)

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
				// A .cprefab from the Asset Browser: placed as a root, at the origin.
				if (PrefabAuthoring::IsDraggingPrefab()) {
					if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload)) {
						m_pendingPrefab = static_cast<char const*>(payload->Data);
						m_pendingParent = kNullEntity;
						RequestAction(PendingAction::PlacePrefab, kNullEntity);
					}
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

			// Ctrl+] / Ctrl+[ (+Shift): sibling order. Only while the Hierarchy is focused (default
			// route); the UI Editor tab has the same keys.
			OrderMove move{};
			if (hasSelection && OrderShortcutPressed(move)) { m_pendingMove = move; RequestAction(PendingAction::Reorder, selected); }

			// F: frame the selection where it is edited (see FocusSelection). Default (focus) route,
			// so only while the Hierarchy is focused - the Scene / UI views handle F themselves.
			if (hasSelection && ImGui::Shortcut(ImGuiKey_F)) { FocusSelection(); }

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

	// The ID stays the entity index (PushID above); only the visible label is the name.
	std::string const label = LabelOf(m_world, entity);
	// Prefab instances in the prefab colour (red if their .cprefab is gone), like Unity.
	ImVec4 prefabColour;
	bool const tinted = PrefabAuthoring::RowColour(m_world, entity, prefabColour);
	if (tinted) { ImGui::PushStyleColor(ImGuiCol_Text, prefabColour); }
	bool const open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
	if (tinted) { ImGui::PopStyleColor(); }
	// A UI element was just created somewhere under this row: open it once, so the new entity shows.
	if (childCount > 0 && m_revealEntity.IsValid()) {
		for (Entity p = m_world.GetParent(m_revealEntity); p.IsValid(); p = m_world.GetParent(p)) {
			if (p == entity) { ImGui::SetNextItemOpen(true); break; }
		}
	}


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

	// Double-click a row: frame it where it is edited - the UI Editor tab for screen-space UI, the
	// Scene tab for everything else (see FocusSelection). The first click already selected it.
	// Only reads the World, so it is safe mid-query.
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
		FocusSelection();
	}

	// Drag this row onto another row to make it that entity's child. Dragging a selected row
	// drags the whole selection (see ActionTargets).
	if (ImGui::BeginDragDropSource()) {
		ImGui::SetDragDropPayload(kEntityPayload, &entity, sizeof(Entity));
		if (selected && inspector->SelectionCount() > 1) { ImGui::Text("%zu entities", inspector->SelectionCount()); }
		else                                             { ImGui::Text("%s", label.c_str()); }
		ImGui::EndDragDropSource();
	}
	// Dropping on a row: the top / bottom quarter puts the dragged entities next to it (sibling
	// order = UI draw order), the middle makes them its children. Drawn ourselves (a line or a box),
	// so the user can see which it will be before letting go.
	if (ImGui::BeginDragDropTarget()) {
		if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(kEntityPayload,
				ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
			ImVec2 const rmin = ImGui::GetItemRectMin();
			ImVec2 const rmax = ImGui::GetItemRectMax();
			float const t = (ImGui::GetIO().MousePos.y - rmin.y) / (std::max)(rmax.y - rmin.y, 1.0f);
			bool const hasParent = m_world.GetParent(entity).IsValid();   // roots have no sibling order
			bool const expanded  = childCount > 0 && open;

			DropZone zone = DropZone::Into;
			if (t < 0.25f && hasParent)      { zone = DropZone::Before; }
			else if (t > 0.75f && expanded)  { zone = DropZone::FirstChild; }
			else if (t > 0.75f && hasParent) { zone = DropZone::After; }

			ImDrawList* dl = ImGui::GetWindowDrawList();
			ImU32 const colour = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
			float const right = rmax.x;   // the row spans the panel's width (SpanAvailWidth)
			switch (zone) {
				case DropZone::Into:       dl->AddRect(rmin, rmax, colour, 2.0f, 0, 2.0f); break;
				case DropZone::Before:     dl->AddLine(ImVec2(rmin.x, rmin.y), ImVec2(right, rmin.y), colour, 2.0f); break;
				case DropZone::After:      dl->AddLine(ImVec2(rmin.x, rmax.y), ImVec2(right, rmax.y), colour, 2.0f); break;
				case DropZone::FirstChild: dl->AddLine(ImVec2(rmin.x + ImGui::GetStyle().IndentSpacing, rmax.y), ImVec2(right, rmax.y), colour, 2.0f); break;
			}

			if (payload->IsDelivery()) {
				Entity dragged{};
				std::memcpy(&dragged, payload->Data, sizeof(Entity));
				if (zone == DropZone::Into) {
					m_pendingParent = entity;
					RequestAction(PendingAction::Reparent, dragged);   // deferred: we are inside the query loop
				} else {
					m_pendingParent = zone == DropZone::FirstChild ? entity : m_world.GetParent(entity);
					m_pendingAnchor = zone == DropZone::FirstChild ? kNullEntity : entity;
					m_pendingZone   = zone;
					RequestAction(PendingAction::Place, dragged);
				}
			}
		}
		// A .cprefab from the Asset Browser: placed as this entity's child, sitting exactly on it.
		// Only accepted for prefabs, so other assets don't light the row up.
		if (PrefabAuthoring::IsDraggingPrefab()) {
			if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload)) {
				m_pendingPrefab = static_cast<char const*>(payload->Data);
				m_pendingParent = entity;
				RequestAction(PendingAction::PlacePrefab, entity);   // deferred: we are inside the query loop
			}
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
		// Greyed out in Play Mode (PrefabAuthoring::CanCreate).
		// Not on a part of an instance: re-linking it would tear it out of that instance.
		if (ImGui::MenuItem("Create Prefab", nullptr, false, PrefabAuthoring::CanCreate() && !PrefabAuthoring::IsLockedPart(m_world, entity))) { RequestAction(PendingAction::CreatePrefab, entity); }
		if (PrefabAuthoring::IsInstanceRoot(m_world, entity) && ImGui::MenuItem("Unpack Prefab")) { RequestAction(PendingAction::UnpackPrefab, entity); }
		ImGui::Separator();
		// Sibling order: what draws on top in the UI. Roots have none (canvases use sortOrder).
		bool const orderable = m_world.GetParent(entity).IsValid();
		if (ImGui::BeginMenu("Order", orderable)) {
			OrderMove move{};
			if (DrawOrderMenuItems(move, true)) { m_pendingMove = move; RequestAction(PendingAction::Reorder, entity); }
			ImGui::EndMenu();
		}
		if (!orderable) { ImGui::SetItemTooltip("Root entities have no order. Canvases: use UICanvas sortOrder."); }
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

	// A screen-space canvas, or an entity with a UIRect whose nearest canvas above it is a screen-space
	// one: what the UI Editor tab shows (World Space canvases are not drawn there).
	bool IsScreenSpaceUI(Crucible::World const& world, Crucible::Entity entity) {
		if (Crucible::UICanvas const* canvas = world.Read<Crucible::UICanvas>(entity)) {
			return canvas->renderMode == Crucible::UICanvas::RenderMode::ScreenSpace;
		}
		if (!world.HasComponent<Crucible::UIRect>(entity)) { return false; }
		for (Crucible::Entity p = world.GetParent(entity); p.IsValid(); p = world.GetParent(p)) {
			if (Crucible::UICanvas const* canvas = world.Read<Crucible::UICanvas>(p)) {
				return canvas->renderMode == Crucible::UICanvas::RenderMode::ScreenSpace;
			}
		}
		return false;
	}

	// A canvas, or an entity with a UIRect somewhere under a canvas: a place a new UI element can go.
	bool IsInUITree(Crucible::World const& world, Crucible::Entity entity) {
		if (world.HasComponent<Crucible::UICanvas>(entity)) { return true; }
		if (!world.HasComponent<Crucible::UIRect>(entity))  { return false; }
		for (Crucible::Entity p = world.GetParent(entity); p.IsValid(); p = world.GetParent(p)) {
			if (world.HasComponent<Crucible::UICanvas>(p)) { return true; }
		}
		return false;
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
		for (Entity const r : roots) { PrefabAuthoring::DetachCopiedPart(m_world, r); }   // a copied piece of an instance is not an instance
		RecordCreated(roots, "Paste");
		break;
	}

	case PendingAction::Duplicate: {
		// Does not touch the clipboard, same as Unity's Ctrl+D.
		// Each copy is placed right after its original instead of at the end of the list.
		std::vector<Entity> roots;
		// Each copy's root is named "<name> (k)", the first k no entity uses yet (Unity).
		std::unordered_set<std::string> taken;
		{
			auto names = m_world.CreateQuery<const EntityName>();
			names.ForEach([&](Entity, EntityName const& n) { taken.insert(n.name); });
		}
		for (Entity const t : targets) {
			std::uint32_t const after = m_world.GetSiblingIndex(t) + 1;
			Entity const copy = PasteSubtree(CopySubtree(t), m_world.GetParent(t), static_cast<int>(after));
			if (m_world.IsAlive(copy)) { m_world.SetEntityName(copy, CopyName(LabelOf(m_world, t), taken)); }
			roots.push_back(copy);
		}
		for (Entity const r : roots) { PrefabAuthoring::DetachCopiedPart(m_world, r); }   // a copied piece of an instance is not an instance
		RecordCreated(roots, "Duplicate");   // after the rename, so redo brings the copy back named
		break;
	}

	case PendingAction::PlacePrefab: {
		// Selects the copy and records its own undo step (PrefabAuthoring::PlacePrefab).
		Entity const placed = PrefabAuthoring::PlacePrefab(m_world, m_pendingPrefab, m_pendingParent);
		if (m_world.IsAlive(placed)) {
			m_revealEntity = placed;   // open its parent's rows so the new row is visible
			m_rangeAnchor  = placed;
		}
		m_pendingPrefab.clear();
		m_pendingParent = kNullEntity;
		break;
	}

	case PendingAction::UnpackPrefab:
		for (Entity const t : targets) { PrefabAuthoring::Unpack(m_world, t); }   // skips anything that isn't an instance root
		break;

	case PendingAction::CreatePrefab:
		// One .cprefab per top-level target (children included), in Assets/Prefabs. To pick the
		// folder, drag the rows onto the Asset Browser instead. Saving reads the World only.
		PrefabAuthoring::CreatePrefabs(m_world, targets, PrefabAuthoring::kDefaultFolder);
		break;

	case PendingAction::Delete:
		// Parts of a prefab instance stay (unpack first); the rest is deleted as before.
		DeleteEntities(PrefabAuthoring::WithoutLockedParts(m_world, targets, "delete"));
		break;

	case PendingAction::Reparent: {
		// Parts of a prefab instance can't change parent (unpack first).
		std::vector<Entity> const movable = PrefabAuthoring::WithoutLockedParts(m_world, targets, "move", m_pendingParent);
		if (!movable.empty()) { ReparentAll(movable, m_pendingParent); }
		m_pendingParent = kNullEntity;
		break;
	}
	case PendingAction::Place: {
		// Same rule, but a part may still be reordered among its own siblings.
		std::vector<Entity> const movable = PrefabAuthoring::WithoutLockedParts(m_world, targets, "move", m_pendingParent);
		if (!movable.empty()) { PlaceAll(movable, m_pendingParent, m_pendingAnchor, m_pendingZone); }
		m_pendingParent = kNullEntity;
		m_pendingAnchor = kNullEntity;
		break;
	}
	case PendingAction::Reorder:
		Reorder(targets, m_pendingMove);
		break;
	}
}

// ------------------------------------------------------------------------------------------------ //
//  Sibling order
// ------------------------------------------------------------------------------------------------ //

char const* Crucible::Hierarchy::OrderLabel(OrderMove move) {
	switch (move) {
		case OrderMove::Forward:  return "Move Forward";
		case OrderMove::Backward: return "Move Backward";
		case OrderMove::ToFront:  return "Bring to Front";
		case OrderMove::ToBack:   return "Send to Back";
	}
	return "Reorder";
}

bool Crucible::Hierarchy::DrawOrderMenuItems(OrderMove& picked, bool enabled) {
	bool chosen = false;
	auto item = [&](OrderMove move, char const* keys) {
		if (ImGui::MenuItem(OrderLabel(move), keys, false, enabled)) { picked = move; chosen = true; }
	};
	item(OrderMove::ToFront,  "Ctrl+Shift+]");
	item(OrderMove::Forward,  "Ctrl+]");
	item(OrderMove::Backward, "Ctrl+[");
	item(OrderMove::ToBack,   "Ctrl+Shift+[");
	return chosen;
}

bool Crucible::Hierarchy::OrderShortcutPressed(OrderMove& move, ImGuiInputFlags route) {
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_RightBracket, route)) { move = OrderMove::ToFront;  return true; }
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_LeftBracket, route))  { move = OrderMove::ToBack;   return true; }
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_RightBracket, route))                  { move = OrderMove::Forward;  return true; }
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_LeftBracket, route))                   { move = OrderMove::Backward; return true; }
	return false;
}

std::vector<Crucible::Entity> Crucible::Hierarchy::ChildrenOf(Entity parent) const {
	std::vector<Entity> children;
	if (parent.IsValid() && m_world.IsAlive(parent)) {
		m_world.ForEachChild(parent, [&children](Entity child) { children.push_back(child); });
	}
	return children;
}

void Crucible::Hierarchy::ApplyChildOrder(Entity parent, std::vector<Entity> const& order, std::string const& label,
                                          std::vector<std::unique_ptr<EditorCommand>>& parts) {
	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	EntityKey const parentKey = history.KeyOf(parent);
	for (std::uint32_t i = 0; i < order.size(); ++i) {
		Entity const child = order[i];
		std::uint32_t const index = m_world.GetSiblingIndex(child);
		if (index == i) { continue; }
		Transform const* local = m_world.Read<Transform>(child);
		if (!local) { continue; }
		ReparentCommand::Placement const before{ parentKey, index, *local };
		m_world.SetSiblingIndex(child, i);
		ReparentCommand::Placement const after{ parentKey, m_world.GetSiblingIndex(child), *local };
		parts.push_back(std::make_unique<ReparentCommand>(history.KeyOf(child), before, after, label));
	}
}

void Crucible::Hierarchy::PlaceAll(std::vector<Entity> const& entities, Entity parent, Entity anchor, DropZone zone) {
	if (!parent.IsValid() || !m_world.IsAlive(parent)) { return; }
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) { inspector->CommitPendingEdit(); }

	std::string const label = CountLabel("Move", entities.size());
	std::vector<std::unique_ptr<EditorCommand>> parts;

	// 1. Under the new parent first (keeps their world position, like a normal drop). The anchor
	//    itself can be in the dragged selection: it stays where it is and the rest go around it.
	std::vector<Entity> moving;
	for (Entity const e : entities) {
		if (e == anchor || e == parent || !m_world.IsAlive(e)) { continue; }
		if (!(m_world.GetParent(e) == parent)) {
			if (auto command = Reparent(e, parent, label)) { parts.push_back(std::move(command)); }
		}
		if (m_world.GetParent(e) == parent) { moving.push_back(e); }   // refused (e.g. a cycle): left out
	}

	// 2. Then the order: everyone else as they are, with the moved ones (in row order) dropped in
	//    next to the anchor - or at the front for "first child".
	if (!moving.empty()) {
		std::vector<Entity> order;
		for (Entity const child : ChildrenOf(parent)) {
			if (std::find(moving.begin(), moving.end(), child) == moving.end()) { order.push_back(child); }
		}
		std::size_t at = 0;
		if (zone != DropZone::FirstChild && anchor.IsValid()) {
			auto const it = std::find(order.begin(), order.end(), anchor);
			at = it == order.end() ? order.size() : static_cast<std::size_t>(it - order.begin()) + (zone == DropZone::After ? 1 : 0);
		}
		order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), moving.begin(), moving.end());
		ApplyChildOrder(parent, order, label, parts);
	}
	if (parts.empty()) { return; }

	EditorManager::GetInstance().GetHistory().Push(GroupCommand::Make(std::move(parts), label));
	m_scenes.MarkDirty();
}

void Crucible::Hierarchy::ReorderSelection(OrderMove move) {
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (!inspector || inspector->SelectionCount() == 0) { return; }
	Reorder(TopLevel(inspector->GetSelection()), move);
}

void Crucible::Hierarchy::Reorder(std::vector<Entity> const& entities, OrderMove move) {
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) { inspector->CommitPendingEdit(); }

	// Grouped by parent: each family is reordered on its own.
	std::vector<Entity> parents;
	bool skippedRoot = false;
	for (Entity const e : entities) {
		if (!m_world.IsAlive(e)) { continue; }
		Entity const parent = m_world.GetParent(e);
		if (!parent.IsValid()) { skippedRoot = true; continue; }
		if (std::find(parents.begin(), parents.end(), parent) == parents.end()) { parents.push_back(parent); }
	}
	if (skippedRoot) {
		CRU_USER_INFO("Root entities have no order to change - order canvases with UICanvas sortOrder");
	}

	std::string const label = OrderLabel(move);
	std::vector<std::unique_ptr<EditorCommand>> parts;
	auto const picked = [&entities](Entity e) { return std::find(entities.begin(), entities.end(), e) != entities.end(); };

	for (Entity const parent : parents) {
		std::vector<Entity> order = ChildrenOf(parent);
		switch (move) {
			case OrderMove::ToFront:   // the picked ones last (= on top), keeping their order
				std::stable_partition(order.begin(), order.end(), [&](Entity e) { return !picked(e); });
				break;
			case OrderMove::ToBack:    // the picked ones first
				std::stable_partition(order.begin(), order.end(), picked);
				break;
			case OrderMove::Forward:   // each picked one swaps with the unpicked one after it
				for (std::size_t i = order.size(); i-- > 1;) {
					if (picked(order[i - 1]) && !picked(order[i])) { std::swap(order[i - 1], order[i]); }
				}
				break;
			case OrderMove::Backward:  // each picked one swaps with the unpicked one before it
				for (std::size_t i = 1; i < order.size(); ++i) {
					if (picked(order[i]) && !picked(order[i - 1])) { std::swap(order[i - 1], order[i]); }
				}
				break;
		}
		ApplyChildOrder(parent, order, label, parts);
	}
	if (parts.empty()) { return; }   // already at the front / back

	EditorManager::GetInstance().GetHistory().Push(GroupCommand::Make(std::move(parts), label));
	m_scenes.MarkDirty();
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
		m_world.ReissueDefaultName(entity);   // a copied "Entity 3" gets a fresh default; custom names stay

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

void Crucible::Hierarchy::CreateAnimatedModel() {
    const auto entity = m_world.CreateEntity();
    m_world.AddComponent<MeshRenderer>(entity, MeshRenderer{});
    m_world.AddComponent<AnimatedModel>(entity, AnimatedModel{});
    if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR))
        inspector->SetSelectedEntity(entity);
    auto& history = EditorManager::GetInstance().GetHistory();
    history.Push(std::make_unique<CreateEntityCommand>(history.KeyOf(entity), EntityState::Capture(m_world, entity),
                                                     "Create Animated Model"));
    m_scenes.MarkDirty();
}

void Crucible::Hierarchy::CreateEmptyObject() {
	auto e = m_world.CreateNamedEntity("Empty");   // "Empty"

	// Select first: that commits any Inspector edit still open on the previous entity, so it
	// lands in the history BEFORE this create - the order they actually happened in.
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR))
		inspector->SetSelectedEntity(e);

	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	history.Push(std::make_unique<CreateEntityCommand>(
		history.KeyOf(e), EntityState::Capture(m_world, e), "Create Empty Object"));

	m_scenes.MarkDirty();   // records that there are unsaved changes
}

void Crucible::Hierarchy::CreatePrimitive(const std::string& meshName) {

	auto e = m_world.CreateNamedEntity(meshName);   // "Cube", "Sphere"...
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

	auto e = m_world.CreateNamedEntity(lightType + " Light");   // "Directional Light", "Point Light"...
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

	auto e = m_world.CreateNamedEntity("Camera");
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

void Crucible::Hierarchy::CreateCanvasObject() {

	// Screen space, 1920 x 1080 reference (UICanvas's defaults): what the UI Editor tab edits.
	// Its Transform is ignored for screen-space UI, so it stays at the origin.
	auto e = m_world.CreateNamedEntity("Canvas");
	m_world.AddComponent<UICanvas>(e, UICanvas{});

	// Select first, for the same history-ordering reason as CreateLightObject.
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR))
		inspector->SetSelectedEntity(e);

	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	history.Push(std::make_unique<CreateEntityCommand>(
		history.KeyOf(e), EntityState::Capture(m_world, e), "Create Canvas"));

	m_scenes.MarkDirty();   // records that there are unsaved changes

}

void Crucible::Hierarchy::FocusSelection() {
	EditorManager& editor = EditorManager::GetInstance();
	auto inspector = editor.GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	Entity const active = inspector ? inspector->GetSelectedEntity() : kNullEntity;

	// m_makeFocus brings that tab to the front on the next frame (as Play / Stop do).
	if (active.IsValid() && m_world.IsAlive(active) && IsScreenSpaceUI(m_world, active)) {
		if (auto uiView = editor.GetPanel<UIView>(ImguiPanels::UISCENE)) {
			uiView->m_makeFocus = true;
			uiView->FrameEntity(active);
			return;
		}
	}
	if (auto sceneView = editor.GetPanel<SceneView>(ImguiPanels::EDITORSCENE)) {
		sceneView->m_makeFocus = true;
		sceneView->FrameSelected();
	}
}

void Crucible::Hierarchy::CreateUIElement(const std::string& kind) {

	if (kind != "Image" && kind != "Button" && kind != "Slider" && kind != "Text") {
		CRU_USER_WARN("Unknown UI element: {}", kind);
		return;
	}

	// Where it goes, decided before the selection changes. Like Unity: under the selected entity
	// when that is part of a UI tree, otherwise under the first screen-space canvas, otherwise
	// under a canvas made here.
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	Entity const selected = inspector ? inspector->GetSelectedEntity() : kNullEntity;
	Entity parent = kNullEntity;
	if (selected.IsValid() && m_world.IsAlive(selected) && IsInUITree(m_world, selected)) {
		parent = selected;
	}
	if (parent == kNullEntity) {
		m_world.CreateQuery<const UICanvas>().ForEach([&](Entity e, const UICanvas& canvas) {
			if (parent == kNullEntity && canvas.renderMode == UICanvas::RenderMode::ScreenSpace) { parent = e; }
		});
	}

	Entity createdRoot = kNullEntity;   // the top of what this step creates: a new canvas, or the element
	if (parent == kNullEntity) {
		parent = m_world.CreateEntity();
		m_world.AddComponent<UICanvas>(parent, UICanvas{});
		createdRoot = parent;
	}

	// The element itself. Centred on its parent (UIRect's default anchors and pivot).
	// A slider draws its own track / fill / handle, and text its glyphs, so neither has a UIImage.
	Entity const element = m_world.CreateNamedEntity(kind);
	UIRect rect{};
	if (kind == "Button") { rect.size = { 160.0f, 40.0f }; }
	if (kind == "Slider") { rect.size = { 240.0f, 24.0f }; }
	if (kind == "Text")   { rect.size = { 200.0f, 50.0f }; rect.raycastTarget = false; }   // a label never blocks a click (e.g. on a button under it)
	m_world.AddComponent<UIRect>(element, rect);
	if (kind == "Image" || kind == "Button") { m_world.AddComponent<UIImage>(element, UIImage{}); }
	if (kind == "Button") { m_world.AddComponent<UIButton>(element, UIButton{}); }
	if (kind == "Slider") { m_world.AddComponent<UISlider>(element, UISlider{}); }
	if (kind == "Text") {
		UIText text{};
		text.text = "New Text";
		text.fontSize = 24.0f;
		text.horizontalAlign = TextAlignH::Center;
		text.verticalAlign = TextAlignV::Middle;
		m_world.AddComponent<UIText>(element, text);
	}

	// Last child = drawn on top of its siblings. worldPositionStays = false keeps the identity local
	// Transform (screen-space UI ignores it anyway), so a parent's scale can never refuse the move.
	if (m_world.TrySetParent(element, parent, false) != ParentResult::Ok) {
		CRU_USER_WARN("Could not put the new {} under entity {}; it was created at the top level", kind, parent.index);
	}
	if (createdRoot == kNullEntity) { createdRoot = element; }

	// Select first, for the same history-ordering reason as CreateLightObject.
	if (inspector) { inspector->SetSelectedEntity(element); }
	m_rangeAnchor = element;
	m_revealEntity = element;

	// Recorded the way RecordCreated records a paste: a delete of the new subtree, run backwards.
	// The delete remembers each entity's parent and sibling position, so a redo puts the element
	// back under its canvas - and a canvas made here comes back with it, in the same single step.
	std::string const label = "Create " + kind;
	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	history.Push(std::make_unique<InvertedCommand>(DeleteEntityCommand::Capture(history, createdRoot, label), label));

	m_scenes.MarkDirty();   // records that there are unsaved changes
	CRU_USER_INFO("{}: entity {} under entity {}", label, element.index, parent.index);

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

	auto e = m_world.CreateNamedEntity(effect);   // "Sparks", "Fire"...
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

void Crucible::Hierarchy::CreateWaterObject() {

	auto e = m_world.CreateNamedEntity("Water");
	m_world.AddComponent<Water>(e, Water{});

	// A 10 x 10 m pond at the origin: the surface is the built-in 1 x 1 m plane, scaled.
	if (Transform* t = m_world.Write<Transform>(e)) { t->scale = { 10.f, 1.f, 10.f }; }

	// Select first, for the same history-ordering reason as CreateLightObject.
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR))
		inspector->SetSelectedEntity(e);

	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	history.Push(std::make_unique<CreateEntityCommand>(
		history.KeyOf(e), EntityState::Capture(m_world, e), "Create Water"));

	m_scenes.MarkDirty();   // records that there are unsaved changes

}
