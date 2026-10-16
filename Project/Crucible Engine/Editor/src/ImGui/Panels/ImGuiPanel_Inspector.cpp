/*!
* @file			ImGuiPanel_Inspector.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's inspector for viewing and editing object properties.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGuiPanel_Inspector.h"
#include "Core.h"                       // EntityRefField reaches the scene and world through EditorManager's Core
#include "ImGui/EditorWidgets.h"
#include "ImGui/EditorTheme.h"
#include "Diagnostics/Logger.h"
#include "Scene/SceneManager.h"
#include <ECS/World.h>
#include "Resources/ResourceManager.h"
#include "Undo/EntityCommands.h"
#include "../EditorWidgets.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <cstdint>
#include <cstdio>
#include <cstring>      // std::memcpy, EntityRefField
#include <string>
#include <vector>

namespace {
	// One Unity-style constraints row: "Freeze Position   [ ] X  [ ] Y  [ ] Z".
	// Both rows line their checkboxes up at the same x, past the longer of the two labels.
	bool DrawFreezeRow(char const* label, bool& x, bool& y, bool& z) {
		float const labelWidth = std::max(ImGui::CalcTextSize("Freeze Position").x,
		                                  ImGui::CalcTextSize("Freeze Rotation").x)
		                       + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		bool changed = false;

		ImGui::PushID(label);   // the two rows both have "X", "Y" and "Z" checkboxes
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine(labelWidth);
		changed |= ImGui::Checkbox("X", &x);
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Y", &y);
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Z", &z);
		ImGui::PopID();

		return changed;
	}

	// ---- Add Component rules --------------------------------------------------------------
	//
	// Three hooks, each a do-nothing template plus specialisations/overloads for the components
	// that need them. ALL of them must be declared above AddNewComponent: a template only sees
	// overloads declared before it, and a missing one silently falls back to the do-nothing version.
	//
	// The physics rules come from ECSPhysicsSystem:
	//   - Collider alone            -> static scenery (NonMoving layer is right).
	//   - Collider + RigidBody      -> dynamic body; the Collider must be on Moving, since two
	//                                  NonMoving bodies never collide (it would fall through).
	//   - RigidBody, no Collider    -> no body at all; the RigidBody does nothing.
	//   - CharacterController       -> its own capsule. Never combined with a Collider (the
	//                                  CharacterController wins), and a RigidBody beside it is ignored.

	// 1. Why T can't be added to this entity right now (shown greyed out with this tooltip),
	//    or nullptr if it can.
	template <typename T>
	char const* AddBlockedReason(Crucible::World&, Crucible::Entity) { return nullptr; }

	template <>
	char const* AddBlockedReason<Crucible::Collider>(Crucible::World& world, Crucible::Entity entity) {
		return world.HasComponent<Crucible::CharacterController>(entity)
			? "This entity has a CharacterController, which is its collider.\nAn entity uses one or the other."
			: nullptr;
	}

	template <>
	char const* AddBlockedReason<Crucible::CharacterController>(Crucible::World& world, Crucible::Entity entity) {
		return world.HasComponent<Crucible::Collider>(entity)
			? "This entity has a Collider. Remove it first:\nan entity uses a Collider or a CharacterController, never both."
			: nullptr;
	}

	// 2. Defaults for the new component before it goes on the entity. The plain version does
	//    nothing; add an overload for any component whose T{} isn't a useful starting point.
	template <typename T>
	void SetupNewComponent(T&, Crucible::World&, Crucible::Entity) {}

	void SetupNewComponent(Crucible::MeshRenderer& renderer, Crucible::World&, Crucible::Entity) {
		// A bare MeshRenderer{} has no mesh and a transparent colour, so it would draw nothing.
		renderer.colour = { .5f, .5f, .5f, 1.f };
		if (Crucible::ResourceManager* rm = Crucible::EditorManager::GetInstance().GetResources())
			rm->AssignMesh(renderer, rm->BuiltinId("Cube"));
	}

	void SetupNewComponent(Crucible::Collider& collider, Crucible::World& world, Crucible::Entity entity) {
		// Already has a RigidBody: this collider makes it a dynamic body, which needs Moving.
		// (Without one, the NonMoving default is right for static scenery.)
		if (world.HasComponent<Crucible::RigidBody>(entity)) {
			collider.m_layer = Crucible::PhysicsLayer::NonMoving;
		}
	}

	// 3. After T is on the entity: add or adjust the other components it needs to work.
	template <typename T>
	void AddRequiredComponents(Crucible::World&, Crucible::Entity) {}

	template <>
	void AddRequiredComponents<Crucible::RigidBody>(Crucible::World& world, Crucible::Entity entity) {
		if (world.HasComponent<Crucible::CharacterController>(entity)) {
			CRU_USER_WARN("RigidBody on entity {} has no effect: it has a CharacterController, which moves itself", entity.index);
			return;
		}

		if (Crucible::Collider* collider = world.GetComponent<Crucible::Collider>(entity)) {
			if (collider->m_layer == Crucible::PhysicsLayer::NonMoving) {
				collider->m_layer = Crucible::PhysicsLayer::Moving;
				CRU_USER_INFO("Entity {}: Collider moved to the Moving layer so the RigidBody collides with the world", entity.index);
			}
			return;
		}

		// No collider: without one the physics system never creates a body for this entity.
		Crucible::Collider collider{};
		collider.m_layer = Crucible::PhysicsLayer::Moving;
		world.AddComponent<Crucible::Collider>(entity, collider);
		CRU_USER_INFO("Entity {}: also added a Collider (Box, Moving) - a RigidBody needs one to be simulated", entity.index);
	}

	template <>
	void AddRequiredComponents<Crucible::CharacterController>(Crucible::World& world, Crucible::Entity entity) {
		if (world.HasComponent<Crucible::RigidBody>(entity)) {
			CRU_USER_WARN("Entity {}: its RigidBody is ignored now that it has a CharacterController - consider removing it", entity.index);
		}
	}

	// MUST come after every hook above.
	template <typename T>
	void AddNewComponent(Crucible::World& world, Crucible::SceneManager& scenes, Crucible::Entity entity) {
		if (char const* reason = AddBlockedReason<T>(world, entity)) {
			// The menu greys these out, so this only fires if something else calls it.
			CRU_USER_WARN("Cannot add {} to entity {}: {}", Crucible::ComponentTraits<T>::name, entity.index, reason);
			return;
		}

		T component{};
		SetupNewComponent(component, world, entity);
		world.AddComponent<T>(entity, std::move(component));
		CRU_USER_INFO("Added {} to entity {}", Crucible::ComponentTraits<T>::name, entity.index);

		// After the add: may add or change other components (never holds a pointer across an add).
		AddRequiredComponents<T>(world, entity);
		scenes.MarkDirty();
	}

	// Builds the deferred "Remove Component" action for T, or an empty function when T can't be
	// removed (Transform, WorldTransform, HierarchyNode). It has to be a template: `if constexpr`
	// only skips code that depends on a template parameter. Written straight into the
	// CRU_INSPECT_COMPONENT macro (inside a normal function), RemoveComponent<Transform> was still
	// checked in the discarded branch and failed its RemovableComponent constraint (C2672).
	template <typename T>
	std::function<void()> MakeComponentRemover(Crucible::World& world, Crucible::Entity entity) {
		if constexpr (Crucible::RemovableComponent<T>) {
			return [&world, entity] {
				world.RemoveComponent<T>(entity);
				CRU_USER_INFO("Removed {} from entity {}", Crucible::ComponentTraits<T>::name, entity.index);
			};
		} else {
			return {};
		}
	}

	// One material texture slot's drag-drop target. Draws a small placeholder box (no GPU->ImGui
	// texture preview for material slots yet - ViewportPanel is the one place that pattern already
	// exists, for the Scene/Game render targets) plus the slot's label, and returns the accepted
	// asset's virtual path, or an empty string if nothing was dropped onto it this frame.
	std::string TextureDropTarget(char const* label, bool bound) {
		ImGui::Selectable(bound ? "(assigned)" : "(none)", false, ImGuiSelectableFlags_None, ImVec2(120.0f, 0.0f));

		std::string dropped;
		if (ImGui::BeginDragDropTarget()) {
			if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(Crucible::EditorWidgets::kAssetPayload)) {
				dropped.assign(static_cast<char const*>(payload->Data));
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();
		ImGui::TextUnformatted(label);
		return dropped;
	}

	char const* KindName(Crucible::ResourceKind kind) {
		switch (kind) {
			case Crucible::ResourceKind::Mesh:     return "mesh";
			case Crucible::ResourceKind::Material: return "material";
			case Crucible::ResourceKind::Texture:  return "texture";
			case Crucible::ResourceKind::Audio:    return "audio";
		}
		return "unknown";
	}

	// Picker for a uint64 property tagged MetaDataTypes::ASSET_REF. Lists every asset of `kind`
	// the ResourceManager knows (built-ins first, e.g. "Cube"), plus "None", and also accepts an
	// asset dragged from the Asset Browser. Only the id is changed: RenderExtractSystem resolves
	// ids to handles every frame, and the id is what the scene file saves.
	bool AssetRefField(char const* label, Crucible::ResourceKind kind, std::uint64_t& id) {
		Crucible::ResourceManager* rm = Crucible::EditorManager::GetInstance().GetResources();
		if (!rm) {
			static bool s_warned = false;
			if (!s_warned) {
				s_warned = true;
				CRU_ERROR("[Inspector] '{}': no ResourceManager. EditorManager::SetResources() must run "
				          "before the editor layer is pushed (see main.cpp)", label);
			}
			ImGui::TextDisabled("%s: (no ResourceManager)", label);
			return false;
		}

		std::uint64_t const before = id;
		bool changed = false;
		std::vector<Crucible::ResourceInfo> const options = rm->List(kind);

		bool const open = ImGui::BeginCombo(label, rm->GetName(id).c_str());

		// Drop target, checked while the combo's own item is still the "last item" (once the popup
		// is open, the last item is the popup window instead). Only accepts an asset of the right
		// kind, so dropping a texture onto the mesh slot does nothing.
		if (!open && ImGui::BeginDragDropTarget()) {
			if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(Crucible::EditorWidgets::kAssetPayload)) {
				char const* path = static_cast<char const*>(payload->Data);
				Crucible::AssetId const dropped = rm->GetIdForPath(path);
				bool accepted = false;
				for (Crucible::ResourceInfo const& info : options) {
					if (dropped != 0 && info.id == dropped) { accepted = true; break; }
				}

				if (dropped == 0) {
					CRU_USER_WARN("[Inspector] '{}' dropped on {}: not in the ResourceManager's asset index "
					         "(no .meta/GUID yet, or the index needs a Refresh) - ignored", path, label);
				} else if (!accepted) {
					CRU_USER_WARN("[Inspector] '{}' dropped on {}: not a {} asset - ignored",
					         path, label, KindName(kind));
				} else {
					id = dropped;
					changed = (id != before);
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (open) {
			if (ImGui::IsWindowAppearing()) {
				CRU_TRACE("[Inspector] {} picker opened: {} {} option(s), current = {}",
				          label, options.size(), KindName(kind), rm->GetName(id));

				// Two entries with the same id would also share an ImGui ID (see PushID below).
				for (std::size_t i = 0; i < options.size(); ++i)
					for (std::size_t j = i + 1; j < options.size(); ++j)
						if (options[i].id == options[j].id)
							CRU_WARN("[Inspector] {} picker: '{}' and '{}' share asset id {:016X}",
							         label, options[i].name, options[j].name, options[i].id);
			}

			if (ImGui::Selectable("None", id == 0)) { id = 0; changed = (id != before); }
			for (Crucible::ResourceInfo const& info : options) {
				// ID by the asset id, NOT by &info. `options` is rebuilt every frame, so its address
				// can move between the mouse-down and mouse-up frames; a combo Selectable only fires
				// if its ID is the same on both, so a pointer-based ID silently dropped clicks.
				ImGui::PushID(reinterpret_cast<void const*>(static_cast<std::uintptr_t>(info.id)));
				bool const selected = (info.id == id);
				if (ImGui::Selectable(info.name.c_str(), selected)) { id = info.id; changed = (id != before); }
				if (selected) ImGui::SetItemDefaultFocus();
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		if (changed) {
			CRU_INFO("[Inspector] {}: {} -> {} ({:016X})", label, rm->GetName(before), rm->GetName(id), id);
		}
		return changed;
	}

	// Picker for a uint64 property tagged MetaDataTypes::ENTITY_REF: another entity's scene id
	// (Scene::IdOf), e.g. a PhysicsJoint's connected body. Drawn as "Entity N" - drop an entity
	// from the Hierarchy onto it to set it, X clears it. Only the scene id is stored, never the
	// Entity handle, so the link survives saving, reloading and play-mode Stop (all of which
	// hand out new handles but keep scene ids).
	bool EntityRefField(char const* label, std::uint64_t& id) {
		Crucible::Core* core = Crucible::EditorManager::GetInstance().GetCore();
		if (!core) {
			ImGui::TextDisabled("%s: (no Core)", label);
			return false;
		}
		Crucible::SceneManager& scenes = core->GetScenes();
		Crucible::World&        world  = core->GetWorld();

		// What the id names right now. EntityOf searches the open scene's guid map; fine here,
		// since it only runs while this entity is selected in the Inspector.
		Crucible::Entity const target = (id == 0) ? Crucible::kNullEntity
		                                          : scenes.Current().EntityOf(xresource::instance_guid{ id });
		std::string text;
		if (id == 0)                                          text = "None";
		else if (!target.IsValid() || !world.IsAlive(target)) text = "Missing entity";   // deleted, or from another scene
		else                                                  text = "Entity " + std::to_string(target.index);

		bool changed = false;
		ImGui::PushID(label);

		float const spacing    = ImGui::GetStyle().ItemInnerSpacing.x;
		float const clearWidth = ImGui::GetFrameHeight();
		ImGui::Button(text.c_str(), ImVec2(ImGui::CalcItemWidth() - clearWidth - spacing, 0.0f));
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag an entity here from the Hierarchy");

		if (ImGui::BeginDragDropTarget()) {
			if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(Crucible::EditorWidgets::kEntityPayload)) {
				Crucible::Entity dropped{};
				std::memcpy(&dropped, payload->Data, sizeof(Crucible::Entity));   // the Hierarchy sends the Entity by value
				if (world.IsAlive(dropped)) {
					// EnsureId, not IdOf: an entity created since the last save has no id yet.
					std::uint64_t const droppedId = scenes.EnsureId(dropped).m_Value;
					if (droppedId != id) { id = droppedId; changed = true; }
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine(0.0f, spacing);
		if (ImGui::Button("X", ImVec2(clearWidth, 0.0f)) && id != 0) { id = 0; changed = true; }
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear");

		ImGui::SameLine(0.0f, spacing);
		ImGui::TextUnformatted(label);
		ImGui::PopID();

		if (changed) CRU_INFO("[Inspector] {}: now {:016X}", label, id);
		return changed;
	}

}

std::size_t Crucible::Inspector::SaveUnsavedMaterials() {
	if (m_unsavedMaterials.empty()) return 0;

	ResourceManager* rm = EditorManager::GetInstance().GetResources();
	if (!rm) {
		CRU_ERROR("[Inspector] {} material(s) not saved: no ResourceManager", m_unsavedMaterials.size());
		return m_unsavedMaterials.size();
	}

	std::size_t failed = 0;
	for (auto it = m_unsavedMaterials.begin(); it != m_unsavedMaterials.end();) {
		if (rm->SaveMaterial(*it)) {
			it = m_unsavedMaterials.erase(it);
		} else {
			CRU_USER_ERROR("[Inspector] Saving {} with the scene failed - still unsaved", rm->GetName(*it));
			++failed;
			++it;
		}
	}
	return failed;
}

void Crucible::Inspector::Init() {
	m_panelName = "Inspector";

	// Start docked on the right side, taking a quarter of the width.
	m_docked = true;
	m_dockArea = DockArea::Right;
	m_dockRatio = 0.25f;

}

// ---- Selection ----

void Crucible::Inspector::SetSelectedEntity(Entity entity) {
	if (!(entity == m_selected)) { CommitPendingEdit(); }
	m_selected = entity;
	m_selection.clear();
	if (entity.IsValid()) { m_selection.push_back(entity); }
	m_draw = nullptr;
}

void Crucible::Inspector::SetSelection(std::vector<Entity> const& entities, Entity active) {
	std::vector<Entity> unique;
	unique.reserve(entities.size());
	for (Entity const e : entities) {
		if (e.IsValid() && std::find(unique.begin(), unique.end(), e) == unique.end()) { unique.push_back(e); }
	}
	if (unique.empty()) { SetSelectedEntity(kNullEntity); return; }
	if (!active.IsValid() || std::find(unique.begin(), unique.end(), active) == unique.end()) { active = unique.back(); }

	if (!(active == m_selected)) { CommitPendingEdit(); }
	m_selected = active;
	m_selection = std::move(unique);
	m_draw = nullptr;
}

void Crucible::Inspector::ToggleSelected(Entity entity) {
	if (!entity.IsValid()) { return; }
	auto const it = std::find(m_selection.begin(), m_selection.end(), entity);
	if (it == m_selection.end()) { AddToSelection(entity); return; }

	m_selection.erase(it);
	if (entity == m_selected) {
		// Removing the active entity: the most recently selected of the rest takes over.
		CommitPendingEdit();
		m_selected = m_selection.empty() ? kNullEntity : m_selection.back();
	}
}

void Crucible::Inspector::AddToSelection(Entity entity) {
	if (!entity.IsValid()) { return; }
	if (m_draw) { m_draw = nullptr; m_selection.clear(); }   // an asset preview was showing: start fresh
	if (!(entity == m_selected)) { CommitPendingEdit(); }

	// Moved to the back, so "selection order" stays "the order they were last clicked".
	auto const it = std::find(m_selection.begin(), m_selection.end(), entity);
	if (it != m_selection.end()) { m_selection.erase(it); }
	m_selection.push_back(entity);
	m_selected = entity;
}

bool Crucible::Inspector::IsSelected(Entity entity) const {
	return entity.IsValid() && std::find(m_selection.begin(), m_selection.end(), entity) != m_selection.end();
}

std::vector<Crucible::Entity> Crucible::Inspector::GetSelection() const {
	std::vector<Entity> alive;
	alive.reserve(m_selection.size());
	for (Entity const e : m_selection) {
		if (m_world.IsAlive(e)) { alive.push_back(e); }
	}
	return alive;
}

void Crucible::Inspector::PruneSelection() {
	auto const dead = [this](Entity e) { return !m_world.IsAlive(e); };
	if (std::none_of(m_selection.begin(), m_selection.end(), dead)) { return; }
	m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(), dead), m_selection.end());
	if (!m_world.IsAlive(m_selected)) { m_selected = m_selection.empty() ? kNullEntity : m_selection.back(); }
}

void Crucible::Inspector::Update(float) {
	PruneSelection();   // deleted / undone entities drop out of the selection

	if (ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {
		if (m_selected.IsValid() && m_world.IsAlive(m_selected)) {
			// Several selected: say so. The fields below edit the active entity only.
			if (m_selection.size() > 1) {
				ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%zu entities selected", m_selection.size());
				ImGui::SetItemTooltip("Showing the active one (last clicked). Delete, Duplicate, Copy and\n"
				                      "the Scene view gizmo apply to all of them; the fields below edit\n"
				                      "only this one.");
			}

			// Undo: while nothing is being edited, keep a fresh "before" snapshot ready.
			if (!m_editOpen) {
				m_editBefore = EntityState::Capture(m_world, m_selected);
				m_editEntity = m_selected;
			}

			DrawEntityInspector(m_selected);
			DrawAddComponentButton(m_selected);

			// A drag keeps its widget active across many frames: commit once it is released,
			// so the whole drag is one undo step. A checkbox or menu click commits this frame.
			if (m_editOpen && !ImGui::IsAnyItemActive()) {
				CommitPendingEdit();
			}
		} else if (m_draw) {
			m_draw();
		} else {
			ImGui::TextDisabled("Nothing selected");
		}
	}

	ImGui::End();
}

void Crucible::Inspector::DrawEntityInspector(Entity entity) {
	ImGui::Text("Entity %u", entity.index);
	ImGui::Separator();

	bool changed = false;

	// Run after the loop: removing a component moves the entity to another archetype, 
	// which would invalidate the component pointers the loop is still using.
	// Local, so it can never run twice or on another entity.
	std::function<void()> deferredRemove;
	std::string           removeLabel;   // "Remove Collider", for the Edit menu

	// One collapsible section per component the entity has. Skips the two the World manages
	// itself (same rule EntityIO uses for saving). PushID keeps same-named fields in different
	// components (Collider::radius vs CharacterController::radius) from sharing a widget ID.
#define CRU_INSPECT_COMPONENT(T)                                                                         \
		if constexpr (!WorldManaged<T>::value && !std::is_same_v<T, WorldTransform>)                         \
		{                                                                                                    \
			if (T* component = m_world.GetComponent<T>(entity))                                              \
			{                                                                                                \
				std::string const header(ComponentTraits<T>::name);                                          \
				ImGui::PushID(header.c_str());                                                               \
				bool const open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);    \
				if constexpr (RemovableComponent<T>) {                                                       \
					if (ImGui::BeginPopupContextItem("##ComponentMenu")) {                                   \
						if (ImGui::MenuItem("Remove Component"))                                             \
							{ deferredRemove = MakeComponentRemover<T>(m_world, entity); removeLabel = "Remove " + header; }  \
						ImGui::EndPopup();                                                                   \
					}                                                                                        \
				}                                                                                            \
				if (open)                                                                                    \
				{                                                                                            \
					bool const fieldEdited  = DrawInstance(*component);  \
					bool const extrasEdited = DrawComponentExtras(*component);  \
					if (fieldEdited || extrasEdited) { changed = true; NoteEdit("Edit " + header); }  \
				}                                                                                            \
				ImGui::PopID();                                                                              \
			}                                                                                                \
		}

CRUCIBLE_COMPONENT_LIST(CRU_INSPECT_COMPONENT)
#undef CRU_INSPECT_COMPONENT

		if (deferredRemove) { deferredRemove(); changed = true; NoteEdit(removeLabel); }

		if (changed) {
			m_scenes.MarkDirty();
		}
}

void Crucible::Inspector::DrawAddComponentButton(Entity entity) {
	if (ImGui::Button("Add Component", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		ImGui::OpenPopup("AddComponentPopup");
	}

	if (ImGui::BeginPopup("AddComponentPopup")) {
		// Add component selection logic here
		if (ImGui::IsWindowAppearing()) { m_addComponentFilter.Clear(); ImGui::SetKeyboardFocusHere(); }
		m_addComponentFilter.Draw("##search", 200.0f);

		bool any = false;

		// Same X-macro as the inspector loop. Skips mandatory components (Transform,
		// WorldTransform, HierarchyNode), World-managed ones, and ones already present.
#define CRU_ADD_COMPONENT_ITEM(T)																			    \
		if constexpr (!Mandatory<T>::value && !WorldManaged<T>::value) {									        \
			std::string_view const name = ComponentTraits<T>::name;											        \
			if (!m_world.HasComponent<T>(entity) &&															        \
				m_addComponentFilter.PassFilter(name.data(), name.data() + name.size())) {					        \
				any = true;																					        \
				char const* const blocked = AddBlockedReason<T>(m_world, entity);                                   \
				ImGui::BeginDisabled(blocked != nullptr);                                                           \
				if (ImGui::Selectable(std::string(name).c_str())) {  \
				    AddNewComponent<T>(m_world, m_scenes, entity);  \
				    NoteEdit("Add " + std::string(name));  \
				}  \
				ImGui::EndDisabled();                                                                               \
				if (blocked) ImGui::SetItemTooltip("%s", blocked);                                                  \
			}                                                                                                       \
		}
CRUCIBLE_COMPONENT_LIST(CRU_ADD_COMPONENT_ITEM)
#undef CRU_ADD_COMPONENT_ITEM

		if (!any) {
			ImGui::TextDisabled("No components available");
		}
		ImGui::EndPopup();
	}
}

bool Crucible::Inspector::DrawMaterialSection(MeshRenderer& renderer) {
	// What gets saved for a material is only MeshRenderer::materialAsset (an id). Everything this
	// section edits - tint, factors, textures - lives in the material itself, so it survives a
	// reload only if that material is a .mat file and the edits are written to it (SaveMaterial).
	// Built-in and model-embedded materials have no file of their own, and are shared by every
	// entity using them, so they are shown read-only with an option to make an editable copy.
	ResourceManager* rm = EditorManager::GetInstance().GetResources();
	if (!rm) {
		ImGui::TextDisabled("(no ResourceManager - materials unavailable)");
		return false;
	}

	// Picks a free Materials/<base>[_n].mat path so creating never overwrites an existing file.
	auto uniquePath = [rm](std::string const& base) {
		for (int n = 0;; ++n) {
			std::string const path = "Materials/" + base + (n ? "_" + std::to_string(n) : std::string()) + ".mat";
			if (rm->GetIdForPath(path) == 0) return path;
		}
	};

	// "Materials/Rock.mat" -> "Rock", so new files and copies get readable names.
	auto baseName = [](std::string name) {
		if (auto const slash = name.find_last_of("/\\"); slash != std::string::npos) name.erase(0, slash + 1);
		if (auto const dot = name.rfind('.'); dot != std::string::npos) name.erase(dot);
		for (char& c : name) { if (c == ' ') c = '_'; }
		return name.empty() ? std::string("Material") : name;
	};

	// Writes `data` as a new Materials/<base>.mat and points this renderer at it.
	auto createAsset = [&](MaterialData const& data, std::string const& base, std::string const& reason) -> bool {
		std::string const path = uniquePath(baseName(base));
		AssetId const id = rm->CreateMaterialAsset(path, data);
		if (id == 0) {
			CRU_USER_ERROR("[Inspector] {}: could not create {} - material not saved (see [Resources] log)", reason, path);
			return false;
		}
		rm->AssignMaterial(renderer, id);
		CRU_INFO("[Inspector] {}: entity {} now uses {}", reason, m_selected.index, path);
		return true;
	};

	// ---- Which material ------------------------------------------------------------------------
	// Picking an existing .mat SHARES it: every object pointing at the same asset id draws with the
	// same GPU material (ResourceManager caches one per id), and edits below change all of them.
	// Only "New..." and "Make Unique" create files.
	std::uint64_t picked = renderer.materialAsset;
	if (AssetRefField("Material", ResourceKind::Material, picked)) {
		rm->AssignMaterial(renderer, picked);
		return true;
	}

	static char s_newName[64] = "NewMaterial";
	if (ImGui::Button("New...")) {
		std::snprintf(s_newName, sizeof(s_newName), "%s", "NewMaterial");
		ImGui::OpenPopup("New Material");
	}
	ImGui::SetItemTooltip("Create a new .mat file in Materials/ and use it on this object");
	if (ImGui::BeginPopup("New Material")) {
		if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
		bool const enter = ImGui::InputText("Name", s_newName, sizeof(s_newName), ImGuiInputTextFlags_EnterReturnsTrue);
		if (enter || ImGui::Button("Create")) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return createAsset(MaterialData{}, s_newName, "New Material");
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}

	if (!renderer.material.IsValid()) {
		if (renderer.materialAsset != 0) {
			ImGui::TextColored(Crucible::EditorTheme::GetPalette().error, "Material %s failed to load", rm->GetName(renderer.materialAsset).c_str());
		} else {
			ImGui::TextDisabled("Pick a material above (or drag a .mat onto it), or create a new one.");
		}
		return false;
	}

	bool const editable = rm->IsMaterialFile(renderer.materialAsset);
	if (!editable) {
		if (renderer.materialAsset == 0) {
			// Created by older code (or at runtime) with no asset behind it.
			ImGui::TextColored(Crucible::EditorTheme::GetPalette().warning, "Unsaved material (no asset) - lost on reload");
		} else {
			ImGui::TextDisabled("%s is shared and has no file of its own (read-only).", rm->GetName(renderer.materialAsset).c_str());
		}
		ImGui::SameLine();
		if (ImGui::Button("Make Editable Copy")) {
			// Factors copy across. Textures copy only if they came from Assets/ - ones embedded in
			// a .glb have no id and SerializeMaterial will warn and save that slot empty.
			std::string const source = renderer.materialAsset ? rm->GetName(renderer.materialAsset) : std::string("Material");
			return createAsset(m_renderer.GetMaterialData(renderer.material), baseName(source), "Make Editable Copy");
		}
	}
	else {
		// How many objects in the scene use this .mat - edits below change every one of them.
		int users = 0;
		m_world.CreateQuery<const MeshRenderer>().ForEach(
			[&](Entity, MeshRenderer const& other) { if (other.materialAsset == renderer.materialAsset) { ++users; } });

		if (users > 1) {
			ImGui::TextColored(Crucible::EditorTheme::GetPalette().warning, "Shared by %d objects - edits change all of them", users);
			ImGui::SameLine();
			if (ImGui::SmallButton("Make Unique")) {
				return createAsset(m_renderer.GetMaterialData(renderer.material),
				                   baseName(rm->GetName(renderer.materialAsset)) + "_Copy", "Make Unique");
			}
			ImGui::SetItemTooltip("Copy this material to a new .mat used only by this object");
		}
	}

	// GetMaterialData returns the live GPU-side copy; edit a local copy and only push it back
	// (UpdateMaterial) when something actually changed this frame - see UpdateMaterial's own
	// comment on why that distinction matters (a full descriptor rewrite + wait-idle when a
	// texture binding changes, versus free when only a push-constant factor changed).
	MaterialData data = m_renderer.GetMaterialData(renderer.material);
	bool changed = false;

	ImGui::BeginDisabled(!editable);

	changed |= ImGui::ColorEdit4("Albedo Tint", &data.factors.albedoTint.x);
	changed |= ImGui::SliderFloat("Metallic", &data.factors.metallicFactor, 0.0f, 1.0f);
	changed |= ImGui::SliderFloat("Roughness", &data.factors.roughnessFactor, 0.0f, 1.0f);
	changed |= ImGui::ColorEdit3("Emissive Factor", &data.factors.emissiveFactor.x);

	ImGui::Separator();
	ImGui::TextDisabled("Drag a texture asset from the Asset Browser onto a slot:");

	struct Slot { char const* label; TextureHandle MaterialData::* member; bool isSRGB; };

	// sRGB for the two slots that store colour data (albedo, emissive); the other three are
	// linear (a normal map, and the metallic-roughness/occlusion packed channels) - same
	// isSRGB convention documented on Renderer::CreateTexture, and matching
	// VulkanPipeline::CreateMaterialDescriptorSetLayout's binding order.
	constexpr Slot kSlots[] = {
		{ "Albedo",             &MaterialData::albedo,            true  },
		{ "Normal",             &MaterialData::normal,            false },
		{ "Metallic/Roughness", &MaterialData::metallicRoughness, false },
		{ "Emissive",           &MaterialData::emissive,          true  },
		{ "Occlusion",          &MaterialData::occlusion,         false },
	};

	for (Slot const& slot : kSlots) {
		ImGui::PushID(slot.label);

		TextureHandle& handle = data.*slot.member;
		std::string const dropped = TextureDropTarget(slot.label, handle.IsValid());

		if (!dropped.empty()) {
			AssetEntry const* entry = m_assets.Find(dropped);
			if (!entry) {
				CRU_USER_WARN("[Inspector] Dropped '{}' onto {}: not in the asset index - ignored", dropped, slot.label);
			} else if (entry->type != AssetType::Texture) {
				CRU_USER_WARN("[Inspector] Dropped asset '{}' onto {} is not a texture - ignored", dropped, slot.label);
			} else {
				// Through the ResourceManager, NOT Renderer::CreateTexture(path): only textures it
				// created have an asset id, and the id is what the .mat file stores.
				TextureHandle const loaded = rm->GetTextureByPath(dropped, slot.isSRGB);
				if (!loaded.IsValid()) {
					CRU_USER_WARN("[Inspector] Texture '{}' failed to load - {} unchanged (see [Resources] log)", dropped, slot.label);
				} else {
					handle  = loaded;
					changed = true;
					CRU_DEBUG("[Inspector] {} <- {}", slot.label, dropped);
				}
			}
		}

		ImGui::PopID();
	}

	ImGui::EndDisabled();

	if (changed) {
		m_renderer.UpdateMaterial(renderer.material, data);
	}

	if (editable) {
		// Edits apply live but are written to the .mat only when the user clicks Save Material,
		// so dragging a slider doesn't rewrite the file every frame.
		AssetId const id = renderer.materialAsset;
		if (changed && m_unsavedMaterials.insert(id).second) {
			CRU_DEBUG("[Inspector] {} has unsaved changes", rm->GetName(id));
		}
		bool const unsaved = m_unsavedMaterials.count(id) != 0;

		if (ImGui::Button("Save Material")) {
			if (rm->SaveMaterial(id)) {
				m_unsavedMaterials.erase(id);
			} else {
				CRU_USER_ERROR("[Inspector] Saving {} failed - edits are live but NOT on disk", rm->GetName(id));
			}
		}
		ImGui::SameLine();
		if (unsaved) {
			ImGui::TextColored(Crucible::EditorTheme::GetPalette().warning, "%s *", rm->GetName(id).c_str());
			ImGui::SetItemTooltip("Unsaved changes - lost on restart unless saved");
		} else {
			ImGui::TextDisabled("%s", rm->GetName(id).c_str());
		}
	}

	return changed;
}

bool Crucible::Inspector::DrawInstance(rttr::instance obj) {
	bool changed = false;

	auto const props = obj.get_derived_type().get_properties();
	if (props.empty()) {
		// Catches a component someone forgot to register.
		ImGui::TextDisabled("(no properties registered with rttr)");
		return false;
	}

	for (auto& prop : props) {
		if (prop.get_metadata(MetaDataTypes::NOT_ON_IMGUI).to_bool()) continue;

		rttr::type const  type = prop.get_type();
		rttr::variant     value = prop.get_value(obj);
		std::string const label = prop.get_name().to_string();
		char const* name = label.c_str();

		ImGui::BeginDisabled(prop.is_readonly() ||
			prop.get_metadata(MetaDataTypes::DISABLED_ON_IMGUI).to_bool());

		rttr::variant newValue;
		bool edited = false;

		rttr::variant const assetKind = prop.get_metadata(MetaDataTypes::ASSET_REF);

		if (assetKind.is_type<ResourceKind>() && type == rttr::type::get<std::uint64_t>()) {
			// Asset id: picker instead of the raw hex field below.
			std::uint64_t v = value.get_value<std::uint64_t>();
			if (AssetRefField(name, assetKind.get_value<ResourceKind>(), v)) { newValue = v; edited = true; }
		}
		else if (prop.get_metadata(MetaDataTypes::ENTITY_REF).to_bool() && type == rttr::type::get<std::uint64_t>()) {
			// Another entity's scene id: picker instead of the raw hex field below.
			std::uint64_t v = value.get_value<std::uint64_t>();
			if (EntityRefField(name, v)) { newValue = v; edited = true; }
		}
		else if (type == rttr::type::get<bool>()) {
			bool v = value.get_value<bool>();
			if (ImGui::Checkbox(name, &v)) { newValue = v; edited = true; }
		}
		else if (type == rttr::type::get<int>()) {
			int v = value.get_value<int>();
			if (ImGui::DragInt(name, &v)) { newValue = v; edited = true; }
			EditorWidgets::InfiniteDrag();
		}
		else if (type == rttr::type::get<std::uint32_t>()) {
			std::uint32_t v = value.get_value<std::uint32_t>();
			if (ImGui::DragScalar(name, ImGuiDataType_U32, &v)) { newValue = v; edited = true; }
			EditorWidgets::InfiniteDrag();
		}
		else if (type == rttr::type::get<std::uint64_t>()) {
			// Shown as hex, same format as the guid= line in .meta files.
			std::uint64_t v = value.get_value<std::uint64_t>();
			if (ImGui::InputScalar(name, ImGuiDataType_U64, &v, nullptr, nullptr, "%016llX",
				ImGuiInputTextFlags_CharsHexadecimal)) {
				newValue = v; edited = true;
			}
		}
		else if (type == rttr::type::get<float>()) {
			float v = value.get_value<float>();
			rttr::variant const range = prop.get_metadata(MetaDataTypes::SLIDER);
			bool const isSlider = range.is_type<Math::vec2>();
			edited = isSlider
				? ImGui::SliderFloat(name, &v, range.get_value<Math::vec2>().x, range.get_value<Math::vec2>().y)
				: ImGui::DragFloat(name, &v, 0.05f);
			// Sliders map cursor position to value, so only unbounded drags get the infinite drag.
			if (!isSlider) EditorWidgets::InfiniteDrag();
			if (edited) newValue = v;
		}
		else if (type == rttr::type::get<std::string>()) {
			char buffer[256];
			std::snprintf(buffer, sizeof(buffer), "%s", value.get_value<std::string>().c_str());
			if (ImGui::InputText(name, buffer, sizeof(buffer))) { newValue = std::string(buffer); edited = true; }
		}
		else if (type.is_enumeration()) {
			rttr::enumeration const e = type.get_enumeration();
			std::string const current = e.value_to_name(value).to_string();
			if (ImGui::BeginCombo(name, current.c_str())) {
				for (auto const& option : e.get_names()) {
					std::string const optionName = option.to_string();
					bool const selected = (optionName == current);
					if (ImGui::Selectable(optionName.c_str(), selected)) { newValue = e.name_to_value(option); edited = true; }
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		else if (type == rttr::type::get<Math::vec2>()) {
			Math::vec2 v = value.get_value<Math::vec2>();
			if (ImGui::DragFloat2(name, &v.x, 0.05f)) { newValue = v; edited = true; }
			EditorWidgets::InfiniteDrag();
		}
		else if (type == rttr::type::get<Math::vec3>()) {
			Math::vec3 v = value.get_value<Math::vec3>();
			bool const isColour = prop.get_metadata(MetaDataTypes::COLOUR).to_bool();
			edited = isColour
				? ImGui::ColorEdit3(name, &v.x)
				: ImGui::DragFloat3(name, &v.x, 0.05f);
			if (!isColour) EditorWidgets::InfiniteDrag();   // colour pickers keep a normal cursor
			if (edited) newValue = v;
		}
		else if (type == rttr::type::get<Math::vec4>()) {
			Math::vec4 v = value.get_value<Math::vec4>();
			bool const isColour = prop.get_metadata(MetaDataTypes::COLOUR).to_bool();
			edited = isColour
				? ImGui::ColorEdit4(name, &v.x)
				: ImGui::DragFloat4(name, &v.x, 0.05f);
			if (!isColour) EditorWidgets::InfiniteDrag();   // colour pickers keep a normal cursor
			if (edited) newValue = v;
		}
		else if (type == rttr::type::get<Math::quat>()) {
			// Edited as Euler degrees, stored as a quaternion (same trade-off as before).
			Math::vec3 euler = glm::degrees(glm::eulerAngles(value.get_value<Math::quat>()));
			if (ImGui::DragFloat3(name, &euler.x, 0.5f)) { newValue = Math::quat(glm::radians(euler)); edited = true; }
			EditorWidgets::InfiniteDrag();
		}
		else if (type == rttr::type::get<RigidBodyConstraints>()) {
			// Two rows like Unity's Rigidbody "Constraints", instead of a tree of six
			// separate checkboxes. Still one property, so it saves exactly as before.
			RigidBodyConstraints frozen = value.get_value<RigidBodyConstraints>();
			ImGui::TextDisabled("Constraints");
			bool rowEdited = false;
			rowEdited |= DrawFreezeRow("Freeze Position", frozen.freezePositionX, frozen.freezePositionY, frozen.freezePositionZ);
			rowEdited |= DrawFreezeRow("Freeze Rotation", frozen.freezeRotationX, frozen.freezeRotationY, frozen.freezeRotationZ);
			if (rowEdited) { newValue = frozen; edited = true; }
		}
		else if (type.is_class()) {
			// Any other registered struct: recurse into its properties.
			if (ImGui::TreeNode(name)) {
				if (DrawInstance(value)) { newValue = value; edited = true; }
				ImGui::TreePop();
			}
		}
		else {
			ImGui::TextDisabled("%s: unsupported type %s", name, type.get_name().to_string().c_str());
		}

		if (edited) {
			if (prop.set_value(obj, newValue)) {
				changed = true;
			} else {
				// RTTR refuses a value whose type does not match the property exactly.
				CRU_ERROR("[Inspector] Could not write '{}': property is {}, value is {}",
				          label, type.get_name().to_string(), newValue.get_type().get_name().to_string());
			}
		}
		ImGui::EndDisabled();
	}
	return changed;
}

bool Crucible::Inspector::DrawComponentExtras(MeshRenderer& renderer) {
	// The pickers above only change meshAsset/materialAsset. Pull the runtime handles in line with
	// them so the mesh label and the material section below act on what is actually drawn.
	// (Cheap: ResourceManager caches, so this is a map lookup per frame.)
	if (ResourceManager* rm = EditorManager::GetInstance().GetResources()) {
		rm->Resolve(renderer);
	}

	if (renderer.mesh.IsValid()) {
		ImGui::Text("Mesh: %s (id %u)", m_renderer.GetMeshName(renderer.mesh).c_str(), renderer.mesh.id);
	}
	else {
		ImGui::TextDisabled("Mesh: none");
	}

	ImGui::SeparatorText("Material");
	return DrawMaterialSection(renderer);   // your existing function, returning bool
}

// ===========================================================================
//  Undo
// ===========================================================================

void Crucible::Inspector::NoteEdit(std::string label) {
	// Keep the FIRST label of an edit, so a drag reads as the field it started on.
	if (!m_editOpen) {
		m_editOpen = true;
		m_editLabel = std::move(label);
	}
}

void Crucible::Inspector::CommitPendingEdit() {
	if (!m_editOpen) { return; }
	m_editOpen = false;
	std::string label = std::exchange(m_editLabel, std::string{});

	if (!m_world.IsAlive(m_editEntity)) { return; }   // destroyed mid-edit: nothing to record

	// Compare snapshots rather than trusting the "changed" flags: a material-only edit, or a
	// value dragged away and back again, changes nothing a scene would save, so no step.
	std::string after = EntityState::Capture(m_world, m_editEntity);
	if (after == m_editBefore) { return; }

	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	history.Push(std::make_unique<EntityStateCommand>(
		history.KeyOf(m_editEntity), std::move(m_editBefore), std::move(after), std::move(label)));
	m_editBefore.clear();
}
