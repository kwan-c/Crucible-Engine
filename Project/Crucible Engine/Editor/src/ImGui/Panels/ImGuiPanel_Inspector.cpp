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
#include "ImGui/Panels/ImGuiPanel_Animator.h"
#include "ImGui/EditorWidgets.h"
#include "ImGui/EditorTheme.h"
#include "Shaders/MaterialShaderUI.h"
#include "Diagnostics/Logger.h"
#include "Scene/SceneManager.h"
#include "AssetPipe/Assetmanager.h"   // AssetRefField
#include <filesystem>
#include <ECS/World.h>
#include "Resources/ResourceManager.h"
#include "UI/UISystem.h"   // UI::MaxTextEffectWidth, for the text effects
#include "Resources/ModelResource.h"
#include "Resources/RenderedModel.h"
#include "Animation/ModelSockets.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/EntityScriptSystem.h"
#include "Scripting/ScriptAuthoring.h"
#include "ECS/ECSNavigationSystem.h"   // the FlowFieldSurface Bake button
#include "Core.h"
#include "Undo/EntityCommands.h"
#include "Prefabs/PrefabAuthoring.h"
#include "Tags/TagEditor.h"                        // the Tag field and the Tag Manager window
#include "Network/NetContent.h"                    // the registered serializers, for the networking policy table
#include "Network/NetworkAuthoringInspector.h"     // NetworkObjectAuthoring's table (Networking Phase 14.3)
#include "../EditorWidgets.h"
#include "misc/cpp/imgui_stdlib.h"   // ImGui::InputText(std::string*)

#include <algorithm>
#include <memory>
#include <utility>
#include <cstdint>
#include <cstdio>
#include <cstring>      // std::memcpy, EntityRefField
#include <cmath>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {
	constexpr char ProjectScriptAssembly[] = "Managed/Crucible.Scripts.dll";
	constexpr char ProjectScriptNamespace[] = "Crucible.Scripts";
	template<class T> std::string ComponentHeader(const T&) { return std::string(Crucible::ComponentTraits<T>::name); }
	// One script's section header: its file name.
	std::string ScriptHeader(const Crucible::ScriptEntry& script) {
		return script.className.empty() ? "Missing C# script" : script.className + ".cs";
	}

	bool SameScriptName(std::string_view a, std::string_view b) {
		auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
			[&](char left, char right) { return lower(left) == lower(right); });
	}

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
	// Four hooks, each a do-nothing template plus specialisations/overloads for the components
	// that need them. ALL of them must be declared above AddNewComponent / MakeComponentRemover: a template only sees
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

	// A network object cannot contain another (Networking Phase 14.3).
	template <>
	char const* AddBlockedReason<Crucible::NetworkObjectAuthoring>(Crucible::World& world, Crucible::Entity entity) {
		return Crucible::NetworkAuthoringInspector::AddBlockedReason(world, entity);
	}

	template <>
	char const* AddBlockedReason<Crucible::CharacterController>(Crucible::World& world, Crucible::Entity entity) {
		return world.HasComponent<Crucible::Collider>(entity)
			? "This entity has a Collider. Remove it first:\nan entity uses a Collider or a CharacterController, never both."
			: nullptr;
	}

	// Text: UIText for a UI element (it has a UIRect), WorldText for anything in the 3D world.
	template <>
	char const* AddBlockedReason<Crucible::WorldText>(Crucible::World& world, Crucible::Entity entity) {
		return world.HasComponent<Crucible::UIRect>(entity)
			? "This entity is a UI element (it has a UIRect).\nUse UIText for text on a canvas."
			: nullptr;
	}

	template <>
	char const* AddBlockedReason<Crucible::UIRect>(Crucible::World& world, Crucible::Entity entity) {
		return world.HasComponent<Crucible::WorldText>(entity)
			? "This entity has a WorldText, so it lives in the 3D world.\nUI elements go under a UICanvas."
			: nullptr;
	}

	template <>
	char const* AddBlockedReason<Crucible::UIText>(Crucible::World& world, Crucible::Entity entity) {
		return world.HasComponent<Crucible::WorldText>(entity)
			? "This entity has a WorldText (text in the 3D world).\nAn entity uses one or the other."
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
	void AddRequiredComponents<Crucible::AnimatedModel>(Crucible::World& world, Crucible::Entity entity) {
		if (!world.HasComponent<Crucible::MeshRenderer>(entity))
			world.AddComponent<Crucible::MeshRenderer>(entity, Crucible::MeshRenderer{});
	}

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

	// ---- UI rules (UI/UIComponents.h): Unity's [RequireComponent] for the UI types ----
	//   - UIImage, UIText, UISlider -> need a UIRect: it is where they sit and how big they are.
	//   - UIButton          -> needs a UIRect, and a UIImage: the button's picture, which its
	//                          state colours tint.
	//   - All of them (UIRect too) are only drawn under an entity with a UICanvas. That is a
	//     warning, not an automatic add: the canvas is normally a parent entity, not this one.

	// True when the entity or one of its ancestors has a UICanvas.
	bool IsUnderUICanvas(Crucible::World& world, Crucible::Entity entity) {
		for (Crucible::Entity e = entity; e.IsValid(); e = world.GetParent(e)) {
			if (world.HasComponent<Crucible::UICanvas>(e)) return true;
		}
		return false;
	}

	void WarnIfNotUnderUICanvas(Crucible::World& world, Crucible::Entity entity) {
		if (!IsUnderUICanvas(world, entity)) {
			CRU_USER_WARN("Entity {}: not under a UICanvas, so it won't be drawn - make it a child of an entity with a UICanvas", entity.index);
		}
	}

	void EnsureUIRect(Crucible::World& world, Crucible::Entity entity, char const* addedName) {
		if (!world.HasComponent<Crucible::UIRect>(entity)) {
			world.AddComponent<Crucible::UIRect>(entity, Crucible::UIRect{});
			CRU_USER_INFO("Entity {}: also added a UIRect - a {} needs one for its position and size", entity.index, addedName);
		}
		WarnIfNotUnderUICanvas(world, entity);
	}

	template <>
	void AddRequiredComponents<Crucible::UIRect>(Crucible::World& world, Crucible::Entity entity) {
		WarnIfNotUnderUICanvas(world, entity);
	}

	template <>
	void AddRequiredComponents<Crucible::UIImage>(Crucible::World& world, Crucible::Entity entity) {
		EnsureUIRect(world, entity, "UIImage");
	}

	template <>
	void AddRequiredComponents<Crucible::UIText>(Crucible::World& world, Crucible::Entity entity) {
		EnsureUIRect(world, entity, "UIText");
	}

	template <>
	void AddRequiredComponents<Crucible::UISlider>(Crucible::World& world, Crucible::Entity entity) {
		EnsureUIRect(world, entity, "UISlider");
	}

	template <>
	void AddRequiredComponents<Crucible::UIButton>(Crucible::World& world, Crucible::Entity entity) {
		EnsureUIRect(world, entity, "UIButton");
		if (!world.HasComponent<Crucible::UIImage>(entity)) {
			world.AddComponent<Crucible::UIImage>(entity, Crucible::UIImage{});
			CRU_USER_INFO("Entity {}: also added a UIImage - it is the button's picture, tinted by the button's state colours", entity.index);
		}
	}

	// 4. Why T can't be removed from this entity right now (the Remove Component item is greyed
	//    out with this tooltip), or nullptr if it can. The other side of the "also added" rules
	//    in hook 3: a component another one depends on stays until that one is removed (Unity's
	//    [RequireComponent] blocks removal the same way).
	template <typename T>
	char const* RemoveBlockedReason(Crucible::World&, Crucible::Entity) { return nullptr; }

	template <>
	char const* RemoveBlockedReason<Crucible::UIRect>(Crucible::World& world, Crucible::Entity entity) {
		if (world.HasComponent<Crucible::UIButton>(entity)) return "The UIButton on this entity needs it.\nRemove the UIButton (and UIImage) first.";
		if (world.HasComponent<Crucible::UIImage>(entity))  return "The UIImage on this entity needs it for its position and size.\nRemove the UIImage first.";
		if (world.HasComponent<Crucible::UIText>(entity))   return "The UIText on this entity needs it for its position and size.\nRemove the UIText first.";
		if (world.HasComponent<Crucible::UISlider>(entity)) return "The UISlider on this entity needs it for its position and size.\nRemove the UISlider first.";
		return nullptr;
	}

	template <>
	char const* RemoveBlockedReason<Crucible::UIImage>(Crucible::World& world, Crucible::Entity entity) {
		return world.HasComponent<Crucible::UIButton>(entity)
			? "The UIButton on this entity uses it as its picture.\nRemove the UIButton first."
			: nullptr;
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
			if (char const* reason = RemoveBlockedReason<T>(world, entity)) {
				// The menu greys these out, so this only fires if something else calls it.
				CRU_USER_WARN("Cannot remove {} from entity {}: {}", Crucible::ComponentTraits<T>::name, entity.index, reason);
				return {};
			}
			return [&world, entity] {
				world.RemoveComponent<T>(entity);
				CRU_USER_INFO("Removed {} from entity {}", Crucible::ComponentTraits<T>::name, entity.index);
			};
		} else {
			return {};
		}
	}

	// Header label with room on its left for the on/off checkbox (Unity style). "###" keeps the
	// header's ID the component name, whatever text the header shows.
	std::string HeaderLabelWithToggle(std::string const& header, std::string_view id) {
		float const room  = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
		float const space = std::max(ImGui::CalcTextSize(" ").x, 1.0f);
		return std::string(static_cast<std::size_t>(room / space) + 1, ' ') + header + "###" + std::string(id);
	}

	// The on/off checkbox for components the ECS can switch off (World::SetComponentEnabled), drawn
	// over the left end of the header just submitted (headerMin = its top-left). Template for the
	// same reason as MakeComponentRemover: SetComponentEnabled<Transform> must not even be checked.
	// Returns true when clicked; nowEnabled is the component's own on/off choice afterwards.
	template <typename T>
	bool DrawEnabledToggle(Crucible::World& world, Crucible::Entity entity, ImVec2 headerMin, bool& nowEnabled) {
		if constexpr (Crucible::ToggleableComponent<T>) {
			ImVec2 const resume = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(ImVec2(headerMin.x + ImGui::GetTreeNodeToLabelSpacing(), headerMin.y));
			bool enabled = world.IsComponentEnabled<T>(entity);
			bool const clicked = ImGui::Checkbox("##Enabled", &enabled);
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
				// Ticked but not running: say which component it is waiting for.
				Crucible::ComponentId const blocker = world.FirstDisabledRequirement(entity, Crucible::ComponentTypeId<T>());
				if (enabled && blocker != Crucible::kInvalidComponentId) {
					ImGui::SetTooltip("Enabled, but not running: %s is switched off", world.Components().Get(blocker).name.data());
				} else {
					ImGui::SetTooltip(enabled ? "Enabled - untick to switch this component off (its settings are kept)"
					                          : "Disabled - tick to switch this component back on");
				}
			}
			ImGui::SetCursorScreenPos(resume);   // the fields continue under the header
			if (clicked) { world.SetComponentEnabled<T>(entity, enabled); }
			nowEnabled = enabled;
			return clicked;
		} else {
			(void)world; (void)entity; (void)headerMin; (void)nowEnabled;
			return false;
		}
	}

	// Whether T is running: switched on, with every component it requires switched on (true for
	// components that can't be switched off). A component that isn't gets a greyed header.
	template <typename T>
	bool IsComponentRunning(Crucible::World& world, Crucible::Entity entity) {
		if constexpr (Crucible::ToggleableComponent<T>) {
			return world.IsComponentActive<T>(entity);
		} else {
			(void)world; (void)entity;
			return true;
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
			case Crucible::ResourceKind::Font:     return "font";
			case Crucible::ResourceKind::Model:    return "model";
			case Crucible::ResourceKind::AnimatorController: return "animator controller";
			case Crucible::ResourceKind::StateGraph: return "state graph";
			case Crucible::ResourceKind::BehaviorGraph: return "behavior graph";
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
	// (Scene::IdOf), e.g. a PhysicsJoint's connected body. Drawn as the entity's name - drop an entity
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
		// Shown by its EntityName, the same label the Hierarchy uses, so a renamed entity reads
		// as "LeftThigh" rather than "Entity 12". Read every frame, so a rename shows straight away.
		std::string text;
		if (id == 0)                                          text = "None";
		else if (!target.IsValid() || !world.IsAlive(target)) text = "Missing entity";   // deleted, or from another scene
		else {
			Crucible::EntityName const* name = world.Read<Crucible::EntityName>(target);
			text = (name && !name->name.empty()) ? name->name : "Entity " + std::to_string(target.index);
		}

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

	// Picker for a script's `public Prefab` / `AudioClip` / `Texture` / `Font` / `Material` / `Model` field: the asset's
	// guid. Drawn as the file name - drop a matching file from the Asset Browser onto it to set it,
	// X clears it. The guid (not the path) is stored, so renaming or moving the file keeps the link.
	bool AssetRefField(char const* label, std::uint64_t& id, Crucible::ScriptFieldType fieldType) {
		Crucible::Core* core = Crucible::EditorManager::GetInstance().GetCore();
		if (!core) {
			ImGui::TextDisabled("%s: (no Core)", label);
			return false;
		}
		Crucible::AssetManager& assets = core->GetAssets();
		using FT = Crucible::ScriptFieldType;
		char const* const kind = fieldType == FT::AudioClip ? "audio clip"
		                       : fieldType == FT::Texture   ? "texture"
		                       : fieldType == FT::Font      ? "font"
		                       : fieldType == FT::Material  ? "material"
		                       : fieldType == FT::Model     ? "model"
		                                                    : "prefab";
		auto const accepts = [fieldType](Crucible::AssetEntry const& entry) {
			switch (fieldType) {
			case FT::AudioClip: return entry.type == Crucible::AssetType::Audio;
			case FT::Texture:   return entry.type == Crucible::AssetType::Texture;
			case FT::Font:      return entry.type == Crucible::AssetType::Font;
			case FT::Material:  return entry.type == Crucible::AssetType::Material;
			case FT::Model:     return entry.type == Crucible::AssetType::Model;
			default:            return std::filesystem::path(entry.virtualPath).extension() == ".cprefab";
			}
		};

		std::string text;
		if (id == 0) text = "None";
		else if (Crucible::AssetEntry const* entry = assets.FindByGuid(xresource::instance_guid{ id }))
			text = std::filesystem::path(entry->virtualPath).filename().string();
		else text = std::string("Missing ") + kind;   // deleted, or its .meta was regenerated

		bool changed = false;
		ImGui::PushID(label);

		float const spacing    = ImGui::GetStyle().ItemInnerSpacing.x;
		float const clearWidth = ImGui::GetFrameHeight();
		ImGui::Button(text.c_str(), ImVec2(ImGui::CalcItemWidth() - clearWidth - spacing, 0.0f));
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Drag a %s here from the Asset Browser", kind);

		if (ImGui::BeginDragDropTarget()) {
			if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(Crucible::EditorWidgets::kAssetPayload)) {
				std::string const virtualPath = static_cast<char const*>(payload->Data);
				std::uint64_t const droppedId = assets.GetGuid(virtualPath).m_Value;
				Crucible::AssetEntry const* entry = droppedId ? assets.FindByGuid(xresource::instance_guid{ droppedId }) : nullptr;
				if (!entry) CRU_USER_WARN("[Inspector] {}: '{}' has no asset id yet - refresh the Asset Browser", label, virtualPath);
				else if (!accepts(*entry)) CRU_USER_WARN("[Inspector] {}: '{}' is not a {}", label, virtualPath, kind);
				else if (droppedId != id) { id = droppedId; changed = true; }
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine(0.0f, spacing);
		if (ImGui::Button("X", ImVec2(clearWidth, 0.0f)) && id != 0) { id = 0; changed = true; }
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear");

		ImGui::SameLine(0.0f, spacing);
		ImGui::TextUnformatted(label);
		ImGui::PopID();
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
			if (m_world.HasComponent<SocketAttachment>(m_selected) && ImGui::Button("Detach socket (keep world pose)")) {
				CommitPendingEdit();
				auto& history = EditorManager::GetInstance().GetHistory();
				const auto key = history.KeyOf(m_selected);
				const ReparentCommand::Placement beforePlacement{history.KeyOf(m_world.GetParent(m_selected)),
					m_world.GetSiblingIndex(m_selected), *m_world.Read<Transform>(m_selected)};
				const auto before = EntityState::Capture(m_world, m_selected);
				if (DetachModelSocket(m_world, m_selected, true)) {
					const ReparentCommand::Placement afterPlacement{kNullEntity, 0, *m_world.Read<Transform>(m_selected)};
					std::vector<std::unique_ptr<EditorCommand>> parts;
					parts.push_back(std::make_unique<ReparentCommand>(key, beforePlacement, afterPlacement, "Detach socket"));
					parts.push_back(std::make_unique<EntityStateCommand>(key, before, EntityState::Capture(m_world, m_selected), "Detach socket"));
					history.Push(std::make_unique<GroupCommand>(std::move(parts), "Detach socket"));
					m_scenes.MarkDirty();
				} else CRU_USER_WARN("Cannot detach socket: its world pose is unavailable or cannot be stored as Transform TRS.");
			}
			// Unity's NavMeshSurface Bake button: a preview of the blocked cells (Play bakes again on its own).
			if (m_world.HasComponent<FlowFieldSurface>(m_selected)) {
				if (ImGui::Button("Bake", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
					Crucible::Core* core = EditorManager::GetInstance().GetCore();
					if (ECSNavigationSystem* navigation = core ? core->Systems().FindSystem<ECSNavigationSystem>() : nullptr) navigation->Bake(m_world);
				}
				ImGui::SetItemTooltip("Builds the navigation grid from this surface and every static collider,\n"
				                      "and draws the blocked cells in red (Gizmos > Nav grid).\n"
				                      "Pressing Play bakes again on its own, so this is only a preview.");
			}
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
		DrawCreateScriptModal();
	}

	ImGui::End();
	TagEditor::DrawManager(m_world, m_scenes);   // the Tag Manager window, while open (Inspector > Tag > Manage Tags...)
}

void Crucible::Inspector::DrawEntityInspector(Entity entity) {
	// Name, at the top (Unity). Applied when the field is left (see m_nameBuffer); the edit
	// grouping makes the whole rename ONE undo step. Left blank, it becomes "Entity N".
	if (m_world.Read<EntityName>(entity)) {
		if (!(m_nameEntity == entity)) { ApplyPendingName(); }   // another entity's unfinished name
		if (!m_nameDirty) {
			m_nameBuffer = m_world.Read<EntityName>(entity)->name;   // shows renames from anywhere (undo, scripts)
			m_nameEntity = entity;
		}

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Name");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::PushID(static_cast<int>(entity.index));   // another entity gets a fresh field, not this text
		if (ImGui::InputText("##EntityName", &m_nameBuffer)) {
			m_nameDirty = true;
			NoteEdit("Rename");
		}
		bool const left = ImGui::IsItemDeactivated();
		ImGui::PopID();
		if (left) { ApplyPendingName(); }   // before this frame's commit captures the "after" state
	}
	// Tag, under the name (Unity). Picking one is an ordinary edit: one undo step. A tag made
	// with "Add New Tag..." is its own undo step first (Tags/TagEditor.h).
	{
		TagEditor::TagPick const tagPick = TagEditor::DrawTagField(m_world, entity);
		if (tagPick.create) {
			CommitPendingEdit();
			if (!TagEditor::CreateTag(tagPick.tag)) { CRU_USER_WARN("[Inspector] Could not create tag '{}'", tagPick.tag); }
			m_editBefore = EntityState::Capture(m_world, entity);   // the assignment's "before"
			m_editEntity = entity;
		}
		if (tagPick.assign && !m_world.CompareTag(entity, tagPick.tag)
			&& m_world.TrySetEntityTag(entity, tagPick.tag) == TagResult::Ok) {
			NoteEdit("Change Tag");
			m_scenes.MarkDirty();
		}
	}
	// Prefab instances: "Prefab: Crate  [Select] [Unpack]" (PrefabInstance itself is never listed).
	PrefabAuthoring::DrawInspectorBar(m_world, entity);
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
		if constexpr (!WorldManaged<T>::value && !std::is_same_v<T, WorldTransform> && !std::is_same_v<T, EntityName> && !std::is_same_v<T, EntityTag> && !std::is_same_v<T, PrefabInstance> && !std::is_same_v<T, ScriptComponent>) \
		{                                                                                                    \
			if (T* component = m_world.GetComponent<T>(entity))                                              \
			{                                                                                                \
				std::string const header = ComponentHeader(*component);                                     \
				ImGui::PushID(ComponentTraits<T>::name.data());                                               \
				bool const running = IsComponentRunning<T>(m_world, entity);   /* off: greyed header */    \
				if (!running) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); } \
				bool const open = ToggleableComponent<T>                                                     \
					? ImGui::CollapsingHeader(HeaderLabelWithToggle(header, ComponentTraits<T>::name).c_str(),  \
					                          ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap) \
					: ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);                \
				if (!running) { ImGui::PopStyleColor(); }                                                    \
				ImVec2 const headerMin = ImGui::GetItemRectMin();                                            \
				PrefabAuthoring::MarkComponentHeader(m_world, entity, ComponentTraits<T>::name);   /* prefab override bar */ \
				if constexpr (RemovableComponent<T>) {                                                       \
					if (ImGui::BeginPopupContextItem("##ComponentMenu")) {                                   \
						char const* const removeBlocked = RemoveBlockedReason<T>(m_world, entity);           \
						if (ImGui::MenuItem("Remove Component", nullptr, false, removeBlocked == nullptr))   \
							{ deferredRemove = MakeComponentRemover<T>(m_world, entity); removeLabel = "Remove " + header; }  \
						if (removeBlocked) ImGui::SetItemTooltip("%s", removeBlocked);                       \
						PrefabAuthoring::ComponentMenuItems(m_world, entity, ComponentTraits<T>::name);  /* Revert to Prefab */ \
						ImGui::EndPopup();                                                                   \
					}                                                                                        \
				}                                                                                            \
				bool nowEnabled = true;                                                                      \
				if (DrawEnabledToggle<T>(m_world, entity, headerMin, nowEnabled))                            \
					{ changed = true; NoteEdit((nowEnabled ? "Enable " : "Disable ") + header); }            \
				if (open)                                                                                    \
				{                                                                                            \
					PrefabAuthoring::BeginDrawComponent(m_world, entity, ComponentTraits<T>::name);  /* override marks */ \
					bool const fieldEdited  = DrawInstance(*component);  \
					PrefabAuthoring::EndDrawComponent();  \
					bool const extrasEdited = DrawComponentExtras(*component);  \
					if (fieldEdited || extrasEdited) { changed = true; NoteEdit("Edit " + header); }  \
				}                                                                                            \
				ImGui::PopID();                                                                              \
			}                                                                                                \
		}

CRUCIBLE_COMPONENT_LIST(CRU_INSPECT_COMPONENT)
#undef CRU_INSPECT_COMPONENT

		// C# scripts last, one section each (ScriptComponent was skipped above).
		if (DrawScripts(entity, deferredRemove, removeLabel)) { changed = true; }

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
		if (ImGui::IsWindowAppearing()) {
			m_addComponentFilter.Clear();
			RefreshScriptClasses(ProjectScriptAssembly);
			ImGui::SetKeyboardFocusHere();
		}
		m_addComponentFilter.Draw("##search", 200.0f);

		bool any = false;

		// Same X-macro as the inspector loop. Skips mandatory components (Transform,
		// WorldTransform, HierarchyNode), World-managed ones, runtime-only ones (never saved,
		// so an added one would vanish on save), and ones already present.
#define CRU_ADD_COMPONENT_ITEM(T)																			    \
		if constexpr (!Mandatory<T>::value && !WorldManaged<T>::value && !RuntimeOnly<T>::value && !std::is_same_v<T, ScriptComponent> && !std::is_same_v<T, PrefabInstance>) { \
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

		// ScriptComponent is an implementation detail here: users attach a named C# class.
		auto* core = EditorManager::GetInstance().GetCore();
		// Any number of scripts per entity, including the same class twice (as in Unity).
		const char* blocked = core && core->IsInPlaySession() ? "Stop to attach or create scripts." : nullptr;
		std::string search = m_addComponentFilter.InputBuf;
		const auto first = search.find_first_not_of(" \t\r\n");
		search = first == std::string::npos ? "" : search.substr(first, search.find_last_not_of(" \t\r\n") - first + 1);
		bool exactScript = false, scriptHeading = false;
		for (const auto& type : m_scriptClasses) {
			const auto fullName = (type.nameSpace.empty() ? "" : type.nameSpace + ".") + type.name;
			exactScript |= SameScriptName(search, type.name) || SameScriptName(search, fullName);
			if (!m_addComponentFilter.PassFilter(fullName.c_str())) continue;
			if (!scriptHeading) { ImGui::SeparatorText("C# scripts"); scriptHeading = true; }
			any = true;
			ImGui::BeginDisabled(blocked != nullptr);
			if (ImGui::Selectable((fullName + " (C#)").c_str()))
				AssignScript(entity, ProjectScriptAssembly, type.nameSpace, type.name);
			ImGui::EndDisabled();
			if (blocked) ImGui::SetItemTooltip("%s", blocked);
		}
		if (!search.empty() && !exactScript) {
			if (!scriptHeading) ImGui::SeparatorText("C# scripts");
			any = true;
			std::error_code ec;
			const auto source = std::filesystem::path(m_assets.GetAssetRoot()) /
				"Scripts" / (search + ".cs");
			// Never suggest creating a duplicate source file just because its assembly is stale.
			if (ScriptAuthoring::IsIdentifier(search) && std::filesystem::exists(source, ec)) {
				ImGui::TextDisabled("Source file already exists.");
				ImGui::TextDisabled("It is compiling (see the menu bar); attach it once scripts reload.");
			} else {
				const bool validName = ScriptAuthoring::IsIdentifier(search);
				ImGui::BeginDisabled(blocked != nullptr || !validName);
				if (ImGui::Selectable(("Create C# script \"" + search + "\"").c_str()))
					RequestCreateScript(entity, search, ProjectScriptNamespace);
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("%s", blocked ? blocked : !validName ?
					"Use letters, digits and underscores, starting with a letter or underscore." :
					"Create and attach a new C# script. It compiles and reloads by itself before Play.");
			}
		}
		if (!m_scriptPickerError.empty()) {
			ImGui::TextDisabled("Script class list unavailable (hover for details)");
			ImGui::SetItemTooltip("%s", m_scriptPickerError.c_str());
		}
		if (!any) {
			ImGui::TextDisabled("No matching components or scripts");
		}
		ImGui::EndPopup();
	}
}

bool Crucible::Inspector::DrawMaterialSection(MeshRenderer& renderer, bool importedDefault) {
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
		if (importedDefault && renderer.materialAsset == 0) {
			ImGui::TextDisabled("Inherited material. Make an editable copy to override its textures or factors.");
		} else if (renderer.materialAsset == 0) {
			// Created by older code (or at runtime) with no asset behind it.
			ImGui::TextColored(Crucible::EditorTheme::GetPalette().warning, "Unsaved material (no asset) - lost on reload");
		} else {
			ImGui::TextDisabled("%s is shared and has no file of its own (read-only).", rm->GetName(renderer.materialAsset).c_str());
		}
		ImGui::SameLine();
		if (ImGui::Button("Make Editable Copy")) {
			// ResourceManager preserves standalone and compiled-model texture references.
			std::string const source = renderer.materialAsset ? rm->GetName(renderer.materialAsset) : std::string("Material");
			return createAsset(m_renderer.GetMaterialData(renderer.material), baseName(source), "Make Editable Copy");
		}
	}
	else {
		// How many objects in the scene use this .mat - edits below change every one of them.
		int users = 0;
		m_world.CreateQuery<const MeshRenderer>().ForEach(
			[&](Entity, MeshRenderer const& other) {
				if (other.materialAsset == renderer.materialAsset) ++users;
				users += static_cast<int>(std::count(other.materialOverrides.begin(), other.materialOverrides.end(), renderer.materialAsset));
			});

		if (users > 1) {
			ImGui::TextColored(Crucible::EditorTheme::GetPalette().warning, "Used by %d assignments - edits change all of them", users);
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

	// Greyed out where the material's shader makes no use of them (a shader graph decides the
	// whole surface itself) - see Shaders/MaterialShaderUI.h.
	const MaterialBuiltIns builtIns = MaterialBuiltInsUsed(m_renderer, data);
	ImGui::BeginDisabled(!builtIns.factors);
	changed |= ImGui::ColorEdit4("Albedo Tint", &data.factors.albedoTint.x);
	changed |= ImGui::SliderFloat("Metallic", &data.factors.metallicFactor, 0.0f, 1.0f);
	changed |= ImGui::SliderFloat("Roughness", &data.factors.roughnessFactor, 0.0f, 1.0f);
	changed |= ImGui::ColorEdit3("Emissive Factor", &data.factors.emissiveFactor.x);
	ImGui::EndDisabled();
	if (!builtIns.factors) ImGui::TextDisabled("Not used: this material's shader graph sets these itself.");

	// The material's shader, and that shader's own parameters and textures (shader graphs,
	// hand-written surfaces) - see Shaders/MaterialShaderUI.h.
	changed |= DrawMaterialShaderParams(m_renderer, data);

	ImGui::Separator();
	// ---- How it is lit, and its shape ----
	// Shading: Scene follows the Skybox's Toon Shading switch; Toon/Realistic ignore it, so one scene
	// can mix styles (a realistic prop in a toon world, or the reverse).
	{
		constexpr char const* kShadingNames[] = { "Scene", "Toon", "Realistic" };
		int shading = std::clamp(static_cast<int>(data.shading), 0, 2);
		if (ImGui::Combo("Shading", &shading, kShadingNames, IM_ARRAYSIZE(kShadingNames))) {
			data.shading = static_cast<MaterialShading>(shading);
			changed = true;
		}
		ImGui::SetItemTooltip("Scene: toon or realistic, as the Skybox's Toon Shading says.\nToon / Realistic: always that, whatever the scene uses.");
	}
	changed |= ImGui::SliderFloat("Alpha Cutoff", &data.alphaCutoff, 0.0f, 1.0f, data.alphaCutoff > 0.0f ? "%.2f" : "%.2f (off)");
	ImGui::SetItemTooltip("Cut-out (leaves, fences): pixels whose alpha (albedo texture x tint alpha) is below\nthis are not drawn - in shadows and ambient occlusion too. 0 = off (solid).");
	changed |= ImGui::Checkbox("Double-Sided", &data.doubleSided);
	ImGui::SetItemTooltip("Draw both sides (leaf cards, thin sheets). The back is lit as its own front.");

	// ---- Toon overrides: only used when this material is toon-shaded ----
	if (ImGui::TreeNode("Toon Overrides")) {
		ImGui::BeginDisabled(data.shading == MaterialShading::Realistic);
		changed |= ImGui::Checkbox("Own Edge", &data.toon.ownEdge);
		ImGui::SetItemTooltip("Use this edge instead of the Skybox's (e.g. a wide soft one for foliage)");
		ImGui::BeginDisabled(!data.toon.ownEdge);
		changed |= ImGui::SliderFloat("Edge Threshold", &data.toon.threshold, 0.0f, 0.9f);
		changed |= ImGui::SliderFloat("Edge Softness", &data.toon.softness, 0.0f, 1.0f);
		ImGui::EndDisabled();
		changed |= ImGui::Checkbox("Own Shade Colour", &data.toon.ownShadeColour);
		ImGui::SetItemTooltip("Tint the shaded side with this colour instead of the Skybox's");
		ImGui::BeginDisabled(!data.toon.ownShadeColour);
		changed |= ImGui::ColorEdit3("Shade Colour", &data.toon.shadeColour.x);
		ImGui::EndDisabled();
		changed |= ImGui::ColorEdit3("Lit Colour", &data.toon.litColour.x);
		ImGui::SetItemTooltip("Multiplies the light on the lit side (white = no change).\nWarm lit + cool shade gives a yellow-to-teal gradient across a canopy.");
		changed |= ImGui::SliderFloat("Outline", &data.toon.outline, 0.0f, 1.0f);
		ImGui::SetItemTooltip("Strength of the toon outlines on this material (the Skybox's Toon Line settings);\n0 = none, e.g. for foliage or terrain.");
		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	ImGui::Separator();
	ImGui::TextDisabled(builtIns.textures ? "Drag a texture asset from the Asset Browser onto a slot:"
	                                      : "Not used: this material's shader has its own textures, above.");

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

	ImGui::BeginDisabled(!builtIns.textures);
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
	ImGui::EndDisabled();   // !builtIns.textures

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

	// Prefab override marks go on a component's own properties, not on the fields of a struct
	// inside one (DrawInstance recurses for those): only depth 1 is marked.
	struct DepthGuard { int& depth; explicit DepthGuard(int& d) : depth(d) { ++depth; } ~DepthGuard() { --depth; } };
	static int s_depth = 0;
	DepthGuard const depthGuard(s_depth);

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
		// Vectors: one box per axis, each led by its letter in the Scene gizmo's colour, so X is
		// the red handle, Y the green and Z the blue (EditorWidgets::AxisDragFloats, which also
		// does the infinite drag). Colour fields keep the colour picker.
		else if (type == rttr::type::get<Math::vec2>()) {
			Math::vec2 v = value.get_value<Math::vec2>();
			if (EditorWidgets::AxisDragFloats(name, &v.x, 2, 0.05f)) { newValue = v; edited = true; }
		}
		else if (type == rttr::type::get<Math::vec3>()) {
			Math::vec3 v = value.get_value<Math::vec3>();
			bool const isColour = prop.get_metadata(MetaDataTypes::COLOUR).to_bool();
			edited = isColour
				? ImGui::ColorEdit3(name, &v.x)
				: EditorWidgets::AxisDragFloats(name, &v.x, 3, 0.05f);
			if (edited) newValue = v;
		}
		else if (type == rttr::type::get<Math::vec4>()) {
			Math::vec4 v = value.get_value<Math::vec4>();
			bool const isColour = prop.get_metadata(MetaDataTypes::COLOUR).to_bool();
			edited = isColour
				? ImGui::ColorEdit4(name, &v.x)
				: EditorWidgets::AxisDragFloats(name, &v.x, 4, 0.05f);
			if (edited) newValue = v;
		}
		else if (type == rttr::type::get<Math::quat>()) {
			// Edited as Euler degrees, stored as a quaternion (same trade-off as before). X, Y, Z
			// here are the turns around the gizmo's red, green and blue axes - the rotate rings.
			Math::vec3 euler = glm::degrees(glm::eulerAngles(value.get_value<Math::quat>()));
			if (EditorWidgets::AxisDragFloats(name, &euler.x, 3, 0.5f)) { newValue = Math::quat(glm::radians(euler)); edited = true; }
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
		if (s_depth == 1) { PrefabAuthoring::MarkProperty(label); }   // overridden on a prefab instance: bar + Revert

		changed |= DrawPropertyExtras(obj, label);
	}
	return changed;
}

bool Crucible::Inspector::DrawPropertyExtras(rttr::instance& obj, std::string const& name) {
	if (name == "ambientFromSky") {
		if (Skybox* skybox = obj.try_convert<Skybox>()) { return DrawPickFromSky(*skybox); }
	}
	return false;
}

bool Crucible::Inspector::DrawPickFromSky(Skybox& skybox) {
	// With Ambient From Sky off, the two hand-picked colours light the scene (the toon scenes'
	// choice). This fills them from the sky image's own light, as a starting point: what a surface
	// facing straight up / straight down gets from it - exactly what Ambient From Sky gives those
	// two directions - or the same colours scaled to a chosen brightness (a night sky's own light
	// is usually far too dark to use as it is).
	ImGui::Indent();
	if (ImGui::SmallButton("Pick from Sky...")) { ImGui::OpenPopup("Pick Ambient from Sky"); }
	ImGui::SetItemTooltip("Fill Ambient Sky and Ambient Ground from the sky image's light,\n"
	                      "as a starting point for hand-picked ambient colours.");
	ImGui::Unindent();

	if (!ImGui::BeginPopup("Pick Ambient from Sky")) { return false; }

	// The sky this Skybox shows (same rule as RenderExtractSystem): its asset, else its runtime
	// cubemap, else the built-in sky. Read every frame the popup is open - a few multiplies.
	CubemapHandle sky = skybox.cubemap;
	if (ResourceManager* rm = EditorManager::GetInstance().GetResources(); rm && skybox.skyboxAsset != 0) {
		sky = rm->GetCubemap(skybox.skyboxAsset);
	}
	// Ambient From Sky scales the sky's light by Intensity (and both modes by Ambient Intensity),
	// so the colours include Intensity to come out the same.
	float const     intensity = std::max(skybox.intensity, 0.0f);
	glm::vec3 const up   = m_renderer.GetSkyIrradiance(sky, glm::vec3(0.0f,  1.0f, 0.0f)) * intensity;
	glm::vec3 const down = m_renderer.GetSkyIrradiance(sky, glm::vec3(0.0f, -1.0f, 0.0f)) * intensity;
	auto const luminance = [](glm::vec3 const& c) { return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f)); };
	float const skyBrightness = luminance(0.5f * (up + down));

	static int   s_mode       = 0;       // 0 = as the sky is, 1 = scaled to s_brightness
	static float s_brightness = 0.5f;    // average brightness of the two colours (mode 1)
	static bool  s_switchOff  = true;    // also turn Ambient From Sky off

	// Swatches show the values as stored (linear), like the colour fields themselves.
	auto const swatchRow = [](char const* id, char const* label, glm::vec3 const& c) {
		ImGui::ColorButton(id, ImVec4(c.x, c.y, c.z, 1.0f), ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Float);
		ImGui::SameLine();
		ImGui::Text("%-7s %.3f, %.3f, %.3f", label, c.x, c.y, c.z);
	};

	ImGui::TextUnformatted("The sky image's light (what Ambient From Sky gives):");
	swatchRow("##skyUp",   "Up",   up);
	swatchRow("##skyDown", "Down", down);
	ImGui::TextDisabled("Average brightness %.3f", skyBrightness);

	ImGui::SeparatorText("Pick");
	bool const canScale = skyBrightness > 1e-5f;
	if (!canScale) { s_mode = 0; }
	ImGui::RadioButton("As the sky is", &s_mode, 0);
	ImGui::SetItemTooltip("Exactly the sky's light: switching Ambient From Sky off then looks almost\n"
	                      "the same (only the sky's side-to-side differences are lost).");
	ImGui::BeginDisabled(!canScale);
	ImGui::RadioButton("At brightness", &s_mode, 1);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::SliderFloat("##brightness", &s_brightness, 0.01f, 2.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) { s_mode = 1; }
	ImGui::SetItemTooltip("The sky's colours, scaled so their average brightness is this.\n"
	                      "About 0.5 for a bright day, 0.03-0.08 for a night scene.");
	ImGui::EndDisabled();
	if (!canScale) { ImGui::TextDisabled("This sky gives no light to scale."); }

	float const     scale     = (s_mode == 1 && canScale) ? s_brightness / skyBrightness : 1.0f;
	glm::vec3 const newSky    = up * scale;
	glm::vec3 const newGround = down * scale;
	swatchRow("##newSky",    "Sky",    newSky);
	swatchRow("##newGround", "Ground", newGround);

	ImGui::Checkbox("Switch Ambient From Sky off", &s_switchOff);
	ImGui::SetItemTooltip("The two colours only light the scene while Ambient From Sky is off.");

	bool applied = false;
	if (ImGui::Button("Apply")) {
		skybox.ambientSky    = newSky;
		skybox.ambientGround = newGround;
		if (s_switchOff) { skybox.ambientFromSky = false; }
		applied = true;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SetItemTooltip("Sets Ambient Sky and Ambient Ground (Ctrl+Z undoes it).");
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
	ImGui::EndPopup();
	return applied;
}

bool Crucible::Inspector::DrawComponentExtras(AnimatedModel& animation) {
    auto* resources = EditorManager::GetInstance().GetResources();
    bool changed = AssetRefField("Controller", ResourceKind::AnimatorController, animation.controllerAsset);
    if (animation.controllerAsset && ImGui::Button("Edit Controller Graph")) {
        if (auto animator = EditorManager::GetInstance().GetPanel<AnimatorPanel>(ImguiPanels::ANIMATOR)) animator->RequestOpen(animation.controllerAsset);
    }
    const auto* renderer = m_world.Read<MeshRenderer>(m_selected);
    if (!resources || !renderer || !renderer->modelAsset) {
        ImGui::TextWrapped("Choose a compiled Model in MeshRenderer. Animation supplies its pose; MeshRenderer supplies its materials and visibility.");
        return changed;
    }
    const auto model = resources->GetModel(renderer->modelAsset);
    if (!model) { ImGui::TextWrapped("Model could not be loaded. See Console."); return changed; }
    if (animation.controllerAsset) {
        const auto definition = resources->GetAnimatorController(animation.controllerAsset);
        if (!definition) { ImGui::TextWrapped("Controller could not be loaded. See Console."); return changed; }
        if (animation.controllerFailed) { ImGui::TextWrapped("Controller is incompatible with this model. See Console."); return changed; }
        if (!animation.controller.Initialized() || animation.evaluatedController != animation.controllerAsset || animation.controllerDefinition != definition) return changed;
        auto& runtime = animation.controller;
        const uint32_t current = runtime.CurrentState(), next = runtime.NextState();
        ImGui::Text("State: %s", current < definition->states.size() ? definition->states[current].name.c_str() : "-");
        if (next < definition->states.size()) ImGui::Text("To: %s (%.0f%%)", definition->states[next].name.c_str(), runtime.TransitionWeight() * 100.0f);
        else ImGui::TextDisabled("To: -"); // reserve the transition row so playback never shifts the controls
        ImGui::Text("Time: %.3fs | cycles: %.3f", runtime.Time(), runtime.NormalizedProgress());
        if (ImGui::Button(animation.playing ? "Pause controller" : "Play controller")) animation.playing = !animation.playing;
        ImGui::SameLine();
        if (ImGui::Button("Stop controller")) {
            runtime.Reset(*definition, *model, nullptr, animation.speed); animation.playing = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart controller")) {
            runtime.Reset(*definition, *model, nullptr, animation.speed); animation.playing = true;
        }
        ImGui::SeparatorText("Runtime parameters");
        for (uint32_t index = 0; index < definition->parameters.size(); ++index) {
            const auto& parameter = definition->parameters[index];
            const auto* value = runtime.Parameter(index);
            if (!value) continue;
            ImGui::PushID(static_cast<int>(index));
            switch (parameter.type) {
            case AnimatorParameterType::Float: {
                float number = value->number;
                if (ImGui::DragFloat(parameter.name.c_str(), &number, 0.01f)) runtime.SetFloat(parameter.name, number);
                break;
            }
            case AnimatorParameterType::Integer: {
                int integer = value->integer;
                if (ImGui::InputInt(parameter.name.c_str(), &integer)) runtime.SetInteger(parameter.name, integer);
                break;
            }
            case AnimatorParameterType::Bool: {
                bool boolean = value->boolean;
                if (ImGui::Checkbox(parameter.name.c_str(), &boolean)) runtime.SetBool(parameter.name, boolean);
                break;
            }
            case AnimatorParameterType::Trigger:
                if (ImGui::Button(parameter.name.c_str())) runtime.SetTrigger(parameter.name);
                if (value->boolean) { ImGui::SameLine(); ImGui::TextDisabled("pending"); }
                break;
            }
            ImGui::PopID();
        }
        ImGui::TextWrapped("Runtime controls and parameters are temporary. Controller defaults are stored in the .animcontroller asset.");
        return changed;
    }
    changed = ImGui::Checkbox("Loop", &animation.loop) || changed;
    const auto name = [&](uint32_t index) {
        if (index >= model->animations.size()) return std::string("Rest pose");
        const auto& clip = model->animations[index];
        return std::to_string(index) + ": " + (clip.name.empty() ? "Unnamed clip" : clip.name);
    };
    if (ImGui::BeginCombo("Clip", name(animation.clip).c_str())) {
        if (ImGui::Selectable("Rest pose", animation.clip == InvalidModelIndex)) {
            animation.clip = InvalidModelIndex; animation.time = 0.0f; changed = true;
        }
        for (uint32_t index = 0; index < model->animations.size(); ++index) {
            if (ImGui::Selectable(name(index).c_str(), animation.clip == index)) {
                animation.clip = index; animation.time = 0.0f; changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (animation.clip < model->animations.size()) {
        const float duration = model->animations[animation.clip].duration;
        if (ImGui::Button(animation.playing ? "Pause" : "Play")) {
            if (!animation.playing && animation.time >= duration && animation.speed >= 0.0f) animation.time = 0.0f;
            if (!animation.playing && animation.time <= 0.0f && animation.speed < 0.0f) animation.time = duration;
            animation.playing = !animation.playing;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) { animation.playing = false; animation.time = 0.0f; }
        if (ImGui::SliderFloat("Time (seconds)", &animation.time, 0.0f, duration, "%.3f")) animation.playing = false;
        ImGui::TextDisabled("Playback buttons and scrub time are temporary; Play On Start is saved.");
    }
    ImGui::Text("%zu nodes, %zu meshes, %zu skins, %zu clips", model->nodes.size(), model->meshes.size(),
                model->skins.size(), model->animations.size());
    return changed;
}

bool Crucible::Inspector::DrawComponentExtras(SocketAttachment& socket) {
    auto* resources = EditorManager::GetInstance().GetResources();
    const auto parent = m_world.GetParent(m_selected);
    const auto* owner = m_world.Read<MeshRenderer>(parent);
    if (!resources || !owner || !owner->modelAsset) {
        ImGui::TextWrapped("Parent this entity to a model entity, then choose its attachment node.");
        return false;
    }
    const auto model = resources->GetModel(owner->modelAsset);
    if (!model) { ImGui::TextWrapped("Parent model could not be loaded."); return false; }
    bool changed = false;
    const std::string preview = socket.modelAsset == owner->modelAsset && socket.node < model->nodes.size()
        ? std::to_string(socket.node) + ": " + model->nodes[socket.node].name : "Choose / rebind node";
    if (ImGui::BeginCombo("Parent model node", preview.c_str())) {
        for (uint32_t i = 0; i < model->nodes.size(); ++i) {
            if (!IsModelNodeInSubtree(*model, i, owner->modelRoot)) continue;
            const std::string label = std::to_string(i) + ": " + model->nodes[i].name;
            if (ImGui::Selectable(label.c_str(), socket.modelAsset == owner->modelAsset && socket.node == i)) {
                socket.modelAsset = owner->modelAsset; socket.node = i; changed = true;
            }
        }
        ImGui::EndCombo();
    }
    glm::mat4 frame;
    ImGui::TextWrapped("%s", ToString(ModelSocketFrame(m_world, parent, socket, frame)));
    ImGui::TextWrapped("Transform is the alignment offset in the animated node's coordinates. "
        "Add AnimatedModel to a weapon for independent playback. Empty socket entities can hold muzzle markers or other children.");
    return changed;
}

bool Crucible::Inspector::DrawComponentExtras(MeshRenderer& renderer) {
    auto* rm = EditorManager::GetInstance().GetResources();
    if (!rm) return false;
    bool changed = false;
    uint64_t modelId = renderer.modelAsset;
    if (AssetRefField("Model", ResourceKind::Model, modelId)) {
        renderer.modelAsset = modelId; renderer.modelRoot = InvalidModelIndex; renderer.materialOverrides.clear();
        if (modelId) { renderer.meshAsset = 0; renderer.mesh = {}; }
        changed = true;
    }
    if (!renderer.modelAsset) {
        uint64_t meshId = renderer.meshAsset;
        if (AssetRefField("Mesh", ResourceKind::Mesh, meshId)) { rm->AssignMesh(renderer, meshId); changed = true; }
        rm->Resolve(renderer);
        if (renderer.mesh.IsValid()) ImGui::Text("Mesh: %s (id %u)", m_renderer.GetMeshName(renderer.mesh).c_str(), renderer.mesh.id);
        ImGui::SeparatorText("Material");
        return DrawMaterialSection(renderer) || changed;
    }
    const auto model = rm->GetRenderedModel(renderer.modelAsset);
    if (!model) { ImGui::TextWrapped("Model could not be loaded. See Console."); return changed; }
    const auto& nodes = model->definition->nodes;
    const std::string rootLabel = renderer.modelRoot == InvalidModelIndex ? "Whole model" :
        renderer.modelRoot < nodes.size() ? std::to_string(renderer.modelRoot) + ": " + nodes[renderer.modelRoot].name : "Invalid node";
    if (ImGui::BeginCombo("Model part root", rootLabel.c_str())) {
        if (ImGui::Selectable("Whole model", renderer.modelRoot == InvalidModelIndex)) { renderer.modelRoot = InvalidModelIndex; changed = true; }
        for (uint32_t i = 0; i < nodes.size(); ++i) {
            const std::string label = std::to_string(i) + ": " + nodes[i].name;
            if (ImGui::Selectable(label.c_str(), renderer.modelRoot == i)) { renderer.modelRoot = i; changed = true; }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Draw this subtree at the entity origin. Choose a rig root to retain animation of its descendants.\nWhole model preserves the authored scene coordinates.");
    ImGui::SeparatorText("All materials");
    ImGui::TextWrapped("Leave Material empty to use imported defaults. Slot overrides below take precedence.");
    MeshRenderer globalView;
    globalView.materialAsset = renderer.materialAsset;
    globalView.material = renderer.materialAsset ? rm->GetMaterial(renderer.materialAsset) : renderer.material;
    if (!globalView.material.IsValid() && !model->materials.empty()) globalView.material = model->materials.front();
    if (DrawMaterialSection(globalView, !renderer.materialAsset && !renderer.material.IsValid())) {
        renderer.materialAsset = globalView.materialAsset;
        renderer.material = globalView.materialAsset ? globalView.material : MaterialHandle{};
        changed = true;
    }
    ImGui::SeparatorText("Material slots");
    for (size_t slot = 0; slot < model->materials.size(); ++slot) {
        ImGui::PushID(static_cast<int>(slot));
        const auto& imported = model->definition->materials[slot];
        const std::string label = std::to_string(slot) + ": " + (imported.name.empty() ? "Unnamed material" : imported.name);
        if (ImGui::TreeNode(label.c_str())) {
            const uint64_t current = slot < renderer.materialOverrides.size() ? renderer.materialOverrides[slot] : 0;
            MeshRenderer view;
            view.materialAsset = current;
            view.material = current ? rm->GetMaterial(current) :
                renderer.materialAsset ? rm->GetMaterial(renderer.materialAsset) :
                renderer.material.IsValid() ? renderer.material : model->materials[slot];
            if (DrawMaterialSection(view, !current)) {
                if (renderer.materialOverrides.size() <= slot) renderer.materialOverrides.resize(slot + 1, 0);
                renderer.materialOverrides[slot] = view.materialAsset;
                changed = true;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    return changed;
}

bool Crucible::Inspector::DrawComponentExtras(UIRect& rect) {
	bool changed = false;

	// ---- position: stored +y down (screen pixels). Shown +y up unless turned off with the checkbox -
	// purely visual: what is stored never changes, and the choice resets to +Y up each start.
	EditorManager& editor = EditorManager::GetInstance();
	bool const yUp = editor.GetUIPositionYUp();
	float shown[2] = { rect.position.x, yUp ? -rect.position.y : rect.position.y };
	if (ImGui::DragFloat2("position", shown, 1.0f, 0.0f, 0.0f, "%.1f")) {
		rect.position = { shown[0], yUp ? -shown[1] : shown[1] };
		changed = true;
	}
	ImGui::SameLine();
	bool up = yUp;
	if (ImGui::Checkbox("+Y up", &up)) { editor.SetUIPositionYUp(up); }
	ImGui::SetItemTooltip(up ? "Position Y shown with positive = up (like Unity). Display only: the scene stores the same value either way."
	                         : "Position Y shown as stored: positive = down (screen pixels). Display only.");

	changed |= ImGui::DragFloat2("size", &rect.size.x, 1.0f, 0.0f, 0.0f, "%.1f");
	changed |= ImGui::DragFloat("rotation", &rect.rotation, 0.5f, 0.0f, 0.0f, "%.1f deg");
	ImGui::SetItemTooltip("Counter-clockwise, around the pivot.");
	changed |= ImGui::Checkbox("raycastTarget", &rect.raycastTarget);
	ImGui::TextDisabled("computed (screen box): %.0f, %.0f  to  %.0f, %.0f", rect.computed.x, rect.computed.y, rect.computed.z, rect.computed.w);

	// Where this element is drawn - or that it isn't (dragged out of its canvas in the Hierarchy).
	Entity const entity = GetSelectedEntity();
	if (!entity.IsValid() || !m_world.IsAlive(entity) || m_world.HasComponent<UICanvas>(entity)) { return changed; }
	Entity canvas = kNullEntity;
	for (Entity p = m_world.GetParent(entity); p.IsValid(); p = m_world.GetParent(p)) {
		if (m_world.HasComponent<UICanvas>(p)) { canvas = p; }   // the topmost canvas owns the tree
	}
	if (canvas == kNullEntity) {
		ImGui::TextColored(EditorTheme::GetPalette().warning, "Not under a UICanvas: not drawn.");
		ImGui::TextDisabled("Make it a child of a canvas (UI, or UI (World) for the 3D world).");
	} else if (UICanvas const* settings = m_world.Read<UICanvas>(canvas); settings && settings->renderMode == UICanvas::RenderMode::WorldSpace) {
		ImGui::TextDisabled("On a world-space canvas: page pixels (%.0f px = 1 m). Display only.", std::max(settings->pixelsPerUnit, 1.0f));
	}
	return changed;
}

bool Crucible::Inspector::DrawComponentExtras(UICanvas& canvas) {
	bool changed = false;
	bool const world = canvas.renderMode == UICanvas::RenderMode::WorldSpace;

	// ---- Screen Space: greyed out (kept, not lost) on a world-space canvas.
	ImGui::SeparatorText("Screen Space");
	ImGui::BeginDisabled(world);
	changed |= ImGui::Checkbox("scaleWithScreenSize", &canvas.scaleWithScreenSize);
	changed |= ImGui::InputInt("sortOrder", &canvas.sortOrder);
	ImGui::SetItemTooltip("With several canvases, higher draws on top.");
	changed |= ImGui::Checkbox("toggleWithEscape", &canvas.toggleWithEscape);
	ImGui::SetItemTooltip("An in-game menu: hidden when Play starts, Escape shows / hides it and frees the cursor.");
	changed |= ImGui::Checkbox("showsCursor", &canvas.showsCursor);
	ImGui::SetItemTooltip("While shown, the cursor is free (a title screen, game over).");
	ImGui::EndDisabled();

	// ---- World Space: greyed out on a screen-space canvas.
	ImGui::SeparatorText("World Space");
	ImGui::BeginDisabled(!world);
	changed |= ImGui::DragFloat("pixelsPerUnit", &canvas.pixelsPerUnit, 1.0f, 1.0f, 100000.0f, "%.0f px / m", ImGuiSliderFlags_AlwaysClamp);
	float const ppu = std::max(canvas.pixelsPerUnit, 1.0f);
	ImGui::TextDisabled("Page: %.0f x %.0f px = %.2f x %.2f m (referenceResolution / pixelsPerUnit)",
		canvas.referenceResolution.x, canvas.referenceResolution.y, canvas.referenceResolution.x / ppu, canvas.referenceResolution.y / ppu);
	changed |= ImGui::DragFloat2("pivot", &canvas.pivot.x, 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("The page's point on this entity: (0, 0) top-left, (0.5, 0.5) centre, (1, 1) bottom-right.");

	static constexpr char const* kBillboards[] = { "Off", "Face Camera", "Upright" };
	int billboard = static_cast<int>(canvas.billboard);
	if (ImGui::Combo("billboard", &billboard, kBillboards, IM_ARRAYSIZE(kBillboards))) {
		canvas.billboard = static_cast<WorldBillboard>(billboard);
		changed = true;
	}
	ImGui::SetItemTooltip("Off: the Transform's rotation. Face Camera: always square on.\nUpright: turns around Y only (a nameplate that never tips back).");
	changed |= ImGui::Checkbox("alwaysOnTop", &canvas.alwaysOnTop);
	ImGui::SameLine();
	changed |= ImGui::Checkbox("doubleSided", &canvas.doubleSided);
	ImGui::SetItemTooltip("Off: not drawn when seen from behind.");
	changed |= ImGui::DragFloat("intensity", &canvas.intensity, 0.05f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Brightness in the scene: above 1 glows with bloom.");

	changed |= ImGui::Checkbox("fade", &canvas.fade);
	ImGui::SetItemTooltip("Fully visible up to fade start from the camera, gone by fade end.");
	ImGui::BeginDisabled(!canvas.fade);
	ImGui::Indent();
	changed |= ImGui::DragFloat("fade start", &canvas.fadeStart, 0.1f, 0.0f, 10000.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::DragFloat("fade end", &canvas.fadeEnd, 0.1f, 0.0f, 10000.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
	if (canvas.fade && canvas.fadeEnd <= canvas.fadeStart) {
		ImGui::TextColored(EditorTheme::GetPalette().warning, "Fade end should be past fade start (it vanishes at once).");
	}
	ImGui::Unindent();
	ImGui::EndDisabled();
	ImGui::EndDisabled();

	if (world) {
		ImGui::TextDisabled("Display only: nothing on a world-space canvas reacts to the mouse.");
	}
	return changed;
}

bool Crucible::Inspector::DrawComponentExtras(UIButton& button) {
	// Custom: the action's name (actionId is hidden from the generic fields so it only shows here).
	if (button.onClick == UIButton::ClickAction::Custom) {
		bool const edited = ImGui::InputText("actionId", &button.actionId);
		if (button.actionId.empty()) {
			ImGui::TextColored(EditorTheme::GetPalette().warning, "Name the action this button runs.");
		}
		ImGui::TextDisabled("Run by the game: GameApp::RegisterAction(\"%s\", ...)", button.actionId.empty() ? "..." : button.actionId.c_str());
		return edited;
	}
	if (button.onClick == UIButton::ClickAction::Quit) {
		ImGui::TextDisabled("Closes the game. In the editor it stops Play mode.");
		return false;
	}

	// sceneAsset is hidden from the generic fields (a raw id means nothing to a person): pick the
	// scene from the project's scene files instead. Only shown when the button loads a scene.
	if (button.onClick != UIButton::ClickAction::LoadScene) { return false; }

	AssetEntry const* current = button.sceneAsset != 0 ? m_assets.FindByGuid(xresource::instance_guid{ button.sceneAsset }) : nullptr;
	char const* preview = current ? current->virtualPath.c_str() : (button.sceneAsset != 0 ? "(missing scene)" : "None");

	bool changed = false;
	if (ImGui::BeginCombo("scene", preview)) {
		if (ImGui::Selectable("None", button.sceneAsset == 0)) { button.sceneAsset = 0; changed = true; }
		for (AssetEntry const* entry : m_assets.GetByType(AssetType::Scene)) {
			bool const selected = entry->guid.m_Value == button.sceneAsset;
			if (ImGui::Selectable(entry->virtualPath.c_str(), selected)) { button.sceneAsset = entry->guid.m_Value; changed = true; }
			if (selected) { ImGui::SetItemDefaultFocus(); }
		}
		ImGui::EndCombo();
	}
	if (button.sceneAsset == 0) { ImGui::TextDisabled("Pick the scene this button loads."); }
	return changed;
}

bool Crucible::Inspector::DrawComponentExtras(UISlider& slider) {
	// The direction, drawn here instead of with the generic fields because a change between
	// horizontal and vertical must also turn the slider's rect: a 240 x 24 slider switched to
	// Bottom-to-Top would otherwise run along its 24 px height, with almost no room to drag.
	// Unity's Slider does the same. Both changes land in the same edit, so one undo reverts both.
	struct Option { char const* name; UISlider::Direction value; };
	static constexpr Option kOptions[] = {
		{ "LeftToRight", UISlider::Direction::LeftToRight },
		{ "RightToLeft", UISlider::Direction::RightToLeft },
		{ "BottomToTop", UISlider::Direction::BottomToTop },
		{ "TopToBottom", UISlider::Direction::TopToBottom },
	};
	auto isVertical = [](UISlider::Direction d) { return d == UISlider::Direction::BottomToTop || d == UISlider::Direction::TopToBottom; };

	char const* current = kOptions[0].name;
	for (Option const& option : kOptions) { if (option.value == slider.direction) { current = option.name; } }

	bool changed = false;
	if (ImGui::BeginCombo("direction", current)) {
		for (Option const& option : kOptions) {
			bool const selected = option.value == slider.direction;
			if (ImGui::Selectable(option.name, selected) && !selected) {
				if (isVertical(option.value) != isVertical(slider.direction)) {
					// Turn the rect: width <-> height, and the pivot with it, so it keeps its shape
					// around the same point. Only touches UIRect's fields - no structural change, so
					// `slider` stays valid.
					if (UIRect* rect = m_world.GetComponent<UIRect>(m_selected)) {
						std::swap(rect->size.x, rect->size.y);
						std::swap(rect->pivot.x, rect->pivot.y);
					}
				}
				slider.direction = option.value;
				changed = true;
			}
			if (selected) { ImGui::SetItemDefaultFocus(); }
		}
		ImGui::EndCombo();
	}
	return changed;
}

// The text, alignment, font and effects fields UIText and WorldText share (their field names
// match). `world`: WorldText - sizes in metres instead of reference pixels, and a bitmap font is
// a warning rather than a note (it is blocky up close in the world).
template <typename T>
bool Crucible::Inspector::DrawTextExtras(T& text, bool world) {
	bool changed = false;
	char const* const widthFormat = world ? "%.3f m" : "%.1f px";
	float const widthSpeed = world ? 0.001f : 0.05f;
	float const widthMax   = world ? 1.0f : 64.0f;

	// ---- Text: a multi-line box (the generic string field is one line). Enter types a new line;
	// the edit is one undo step from the first key to clicking away, like every Inspector field.
	ImGui::TextUnformatted("text");
	float const lines  = std::clamp(static_cast<float>(std::count(text.text.begin(), text.text.end(), '\n') + 1), 3.0f, 10.0f);
	ImVec2 const size(-FLT_MIN, ImGui::GetTextLineHeight() * lines + ImGui::GetStyle().FramePadding.y * 2.0f);
	if (ImGui::InputTextMultiline("##text", &text.text, size, ImGuiInputTextFlags_AllowTabInput)) { changed = true; }

	// ---- Alignment: Unity's 3 x 3 grid. Each button sets both axes; the current one is lit.
	ImGui::TextUnformatted("alignment");
	ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.35f);
	ImGui::BeginGroup();
	static constexpr char const* kRowNames[] = { "Top", "Middle", "Bottom" };
	static constexpr char const* kColumnNames[] = { "Left", "Center", "Right" };
	static constexpr char const* kGlyphs[3][3] = { { "TL", "T", "TR" }, { "L", "C", "R" }, { "BL", "B", "BR" } };
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			TextAlignH const h = static_cast<TextAlignH>(column);
			TextAlignV const v = static_cast<TextAlignV>(row);
			bool const active = text.horizontalAlign == h && text.verticalAlign == v;
			ImGui::PushID(row * 3 + column);
			if (active) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
			if (column > 0) { ImGui::SameLine(0.0f, 2.0f); }
			if (ImGui::Button(kGlyphs[row][column], ImVec2(28.0f, 0.0f)) && !active) {
				text.horizontalAlign = h;
				text.verticalAlign = v;
				changed = true;
			}
			if (active) { ImGui::PopStyleColor(); }
			ImGui::SetItemTooltip("%s %s", kRowNames[row], kColumnNames[column]);
			ImGui::PopID();
		}
	}
	ImGui::EndGroup();

	// ---- Which font is actually used (a font that isn't compiled falls back to the default).
	if (ResourceManager* rm = EditorManager::GetInstance().GetResources()) {
		FontAsset const* font = text.fontAsset != 0 ? rm->GetFont(text.fontAsset) : nullptr;
		if (text.fontAsset != 0 && font == nullptr) {
			ImGui::TextColored(EditorTheme::GetPalette().warning, "Font not loaded - using the default font (see the Console)");
		}
		if (font == nullptr) { font = rm->GetDefaultFont(); }
		if (font == nullptr) {
			ImGui::TextColored(EditorTheme::GetPalette().error, "No font: %s is missing - compile its .ttf in the Font Importer", ResourceManager::m_defaultFontPath);
		} else if (font->data.m_outputType == FontOutputType::Bitmap) {
			std::string sizes;
			for (FontSizeGroup const& group : font->data.m_groups) {
				sizes += (sizes.empty() ? "" : ", ") + std::to_string(static_cast<int>(group.pixelSize));
			}
			if (world) {
				ImGui::TextColored(EditorTheme::GetPalette().warning, "Bitmap font: blocky up close, shimmers far away.");
				ImGui::TextDisabled("Use an SDF / MTSDF font for world text (Font Importer > Output Type).");
			} else {
				ImGui::TextDisabled("Bitmap font: sharp at %s px on screen", sizes.c_str());
			}
		} else {
			ImGui::TextDisabled("%s font: sharp at any size", ToString(font->data.m_outputType));
		}

		// ---- Effects: each with its checkbox; its settings greyed out while it is off.
		ImGui::SeparatorText("Effects");
		bool const bitmap = font == nullptr || font->data.m_outputType == FontOutputType::Bitmap;
		float const maxWidth = font ? UI::MaxTextEffectWidth(*font, text.bestFit ? text.maxFontSize : text.fontSize) : 0.0f;
		if (bitmap) {
			ImGui::TextColored(EditorTheme::GetPalette().warning, "Bitmap font: outline and glow need an SDF / MTSDF font.");
			ImGui::TextDisabled("Shadow still works (a flat copy, no softness).");
		} else {
			ImGui::TextDisabled(world ? "Outline + glow (+ bold) can reach %.3f m at this size." : "Outline + glow (+ bold) can reach %.1f px at this size.", maxWidth);
			ImGui::SetItemTooltip("The font's distance field ends half its Pixel Range past each edge;\n"
			                      "wider settings are clamped. Raise Pixel Range in the Font Importer for more.");
		}

		auto widthWarning = [&](float width) {
			if (!bitmap && width > maxWidth + 0.01f) {
				ImGui::SameLine();
				ImGui::TextColored(EditorTheme::GetPalette().warning, world ? "(max %.3f)" : "(max %.1f)", maxWidth);
			}
		};

		ImGui::BeginDisabled(bitmap);
		changed |= ImGui::Checkbox("outline", &text.outline);
		ImGui::BeginDisabled(!text.outline);
		ImGui::Indent();
		changed |= ImGui::ColorEdit4("outline colour", &text.outlineColour.x, ImGuiColorEditFlags_AlphaBar);
		changed |= ImGui::DragFloat("outline width", &text.outlineWidth, widthSpeed, 0.0f, widthMax, widthFormat, ImGuiSliderFlags_AlwaysClamp);
		widthWarning(text.outlineWidth);
		ImGui::Unindent();
		ImGui::EndDisabled();

		changed |= ImGui::Checkbox("glow", &text.glow);
		ImGui::BeginDisabled(!text.glow);
		ImGui::Indent();
		changed |= ImGui::ColorEdit4("glow colour", &text.glowColour.x, ImGuiColorEditFlags_AlphaBar);
		changed |= ImGui::DragFloat("glow width", &text.glowWidth, widthSpeed, 0.0f, widthMax, widthFormat, ImGuiSliderFlags_AlwaysClamp);
		widthWarning((text.outline ? text.outlineWidth : 0.0f) + text.glowWidth);
		changed |= ImGui::SliderFloat("glow softness", &text.glowSoftness, 0.0f, 1.0f, "%.2f");
		ImGui::Unindent();
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		changed |= ImGui::Checkbox("shadow", &text.shadow);
		ImGui::BeginDisabled(!text.shadow);
		ImGui::Indent();
		changed |= ImGui::ColorEdit4("shadow colour", &text.shadowColour.x, ImGuiColorEditFlags_AlphaBar);
		changed |= ImGui::DragFloat2("shadow offset", &text.shadowOffset.x, widthSpeed * 2.0f, -widthMax, widthMax, widthFormat);
		ImGui::BeginDisabled(bitmap);
		changed |= ImGui::SliderFloat("shadow softness", &text.shadowSoftness, 0.0f, 1.0f, "%.2f");
		ImGui::EndDisabled();
		ImGui::Unindent();
		ImGui::EndDisabled();
	}
	return changed;
}

// NetworkObjectAuthoring: its fields are drawn only here, as the networking policy table (they are
// NOT_ON_IMGUI in its reflection). Owned by networking: Editor/src/Network/NetworkAuthoringInspector.cpp.
bool Crucible::Inspector::DrawComponentExtras(NetworkObjectAuthoring& authoring) {
	Crucible::Core* core = EditorManager::GetInstance().GetCore();
	if (core == nullptr) return false;
	return NetworkAuthoringInspector::Draw(m_world, m_selected, authoring, m_world.Components(), core->GetNetContent().Serializers());
}

bool Crucible::Inspector::DrawComponentExtras(UIText& text) {
	return DrawTextExtras(text, false);
}

bool Crucible::Inspector::DrawComponentExtras(WorldText& text) {
	bool changed = DrawTextExtras(text, true);

	// ---- Distance fade: the range greyed out while it is off.
	ImGui::SeparatorText("Distance Fade");
	changed |= ImGui::Checkbox("fade", &text.fade);
	ImGui::SetItemTooltip("Fully visible up to fade start from the camera, gone by fade end.");
	ImGui::BeginDisabled(!text.fade);
	ImGui::Indent();
	changed |= ImGui::DragFloat("fade start", &text.fadeStart, 0.1f, 0.0f, 10000.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::DragFloat("fade end", &text.fadeEnd, 0.1f, 0.0f, 10000.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
	if (text.fade && text.fadeEnd <= text.fadeStart) {
		ImGui::TextColored(EditorTheme::GetPalette().warning, "Fade end should be past fade start (it vanishes at once).");
	}
	ImGui::Unindent();
	ImGui::EndDisabled();
	return changed;
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

void Crucible::Inspector::ApplyPendingName() {
	if (!m_nameDirty) { return; }
	m_nameDirty = false;
	if (!m_world.IsAlive(m_nameEntity)) { return; }

	EntityName const* current = m_world.Read<EntityName>(m_nameEntity);
	if (current && current->name == m_nameBuffer) { return; }   // typed, then changed back

	m_world.SetEntityName(m_nameEntity, m_nameBuffer);   // blank -> "Entity N" (World's naming rules)
	m_nameBuffer = m_world.Read<EntityName>(m_nameEntity)->name;
	m_scenes.MarkDirty();
}

void Crucible::Inspector::CommitPendingEdit() {
	ApplyPendingName();   // a name still being typed is part of this edit
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

void Crucible::Inspector::RefreshScriptClasses(const std::string& assemblyPath) {
    m_scriptAssembly = assemblyPath;
    m_scriptClasses.clear(); m_scriptPickerError.clear();
    auto* core = EditorManager::GetInstance().GetCore();
    if (!core || !core->GetScriptRuntime().IsRunning()) {
        m_scriptPickerError = "Class selection requires a Mono-enabled build. You can still create C# source files.";
        return;
    }
    auto& host = core->GetScriptRuntime();
    ScriptAssemblyHandle assembly;
    if (!host.LoadAssembly(assemblyPath, assembly) || !host.DiscoverEntityClasses(assembly, m_scriptClasses))
        m_scriptPickerError = host.LastError();
    std::sort(m_scriptClasses.begin(), m_scriptClasses.end(), [](const auto& a, const auto& b) {
        return std::tie(a.name, a.nameSpace) < std::tie(b.name, b.nameSpace);
    });
}

void Crucible::Inspector::RequestCreateScript(Entity entity, std::string name, std::string nameSpace) {
    m_scriptCreateTarget = entity;
    m_scriptNewName = std::move(name); m_scriptNewNamespace = std::move(nameSpace);
    m_scriptCreateError.clear(); m_scriptCreateRequested = true;
}

void Crucible::Inspector::AssignScript(Entity entity, const std::string& assembly, const std::string& nameSpace,
                                     const std::string& name) {
    CommitPendingEdit();
    m_editBefore = EntityState::Capture(m_world, entity); m_editEntity = entity;
    if (!m_world.HasComponent<ScriptComponent>(entity)) m_world.AddComponent<ScriptComponent>(entity, ScriptComponent{});
    auto* component = m_world.Write<ScriptComponent>(entity);
    // Added at the end of the list (a "Missing C# script" placeholder is filled in instead).
    const auto placeholder = std::find_if(component->scripts.begin(), component->scripts.end(),
                                          [](const ScriptEntry& entry) { return entry.className.empty(); });
    ScriptEntry& script = placeholder != component->scripts.end() ? *placeholder : component->scripts.emplace_back();
    script.enabled = true; script.assembly = assembly; script.nameSpace = nameSpace; script.className = name;
    script.fields.clear();
    if (m_scriptNoticeTarget == entity) m_scriptNotice.clear();
    NoteEdit("Attach " + name); m_scenes.MarkDirty(); CommitPendingEdit();
}

bool Crucible::Inspector::DrawScripts(Entity entity, std::function<void()>& deferredRemove, std::string& removeLabel) {
    auto* component = m_world.GetComponent<ScriptComponent>(entity);
    if (!component) return false;
    constexpr std::string_view componentName = ComponentTraits<ScriptComponent>::name;
    bool changed = false;

    // The whole component switched off (SetActive in Play, or a scene saved that way) pauses every
    // script whatever its own checkbox says; offer to switch it back on.
    const bool allOn = m_world.IsComponentEnabled<ScriptComponent>(entity);
    const bool running = IsComponentRunning<ScriptComponent>(m_world, entity);
    if (!allOn && !component->scripts.empty()) {
        ImGui::TextDisabled("All scripts on this entity are switched off.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Switch on")) {
            m_world.SetComponentEnabled<ScriptComponent>(entity, true);
            changed = true; NoteEdit("Enable scripts");
        }
    }

    // Applied after the loop: the list must not change while it is being drawn.
    enum class Change { None, Remove, Up, Down } change = Change::None;
    std::size_t target = 0;

    ImGui::PushID(componentName.data());
    for (std::size_t slot = 0; slot < component->scripts.size(); ++slot) {
        ScriptEntry& script = component->scripts[slot];
        const std::string header = ScriptHeader(script);
        ImGui::PushID(static_cast<int>(slot));

        const bool greyed = !running || !script.enabled;   // switched off: greyed header, like components
        if (greyed) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); }
        const bool open = ImGui::CollapsingHeader(HeaderLabelWithToggle(header, "Script").c_str(),
                                                  ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        if (greyed) { ImGui::PopStyleColor(); }
        const ImVec2 headerMin = ImGui::GetItemRectMin();
        PrefabAuthoring::MarkComponentHeader(m_world, entity, componentName);   // prefab override bar (all scripts)

        if (ImGui::BeginPopupContextItem("##ScriptMenu")) {
            if (ImGui::MenuItem("Remove Script")) { change = Change::Remove; target = slot; }
            if (ImGui::MenuItem("Move Up", nullptr, false, slot > 0)) { change = Change::Up; target = slot; }
            if (ImGui::MenuItem("Move Down", nullptr, false, slot + 1 < component->scripts.size())) { change = Change::Down; target = slot; }
            PrefabAuthoring::ComponentMenuItems(m_world, entity, componentName);   // Revert to Prefab (all scripts)
            ImGui::EndPopup();
        }

        // This script's own on/off checkbox, over the left end of its header (ScriptEntry::enabled,
        // the same flag as C# EntityScript.Enabled).
        const ImVec2 resume = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(headerMin.x + ImGui::GetTreeNodeToLabelSpacing(), headerMin.y));
        bool enabled = script.enabled;
        if (ImGui::Checkbox("##Enabled", &enabled)) {
            script.enabled = enabled;
            changed = true; NoteEdit((enabled ? "Enable " : "Disable ") + header);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            ImGui::SetTooltip(!running ? "All scripts on this entity are switched off (see above)"
                              : enabled ? "Enabled - untick to pause this script (its other scripts carry on)"
                                        : "Disabled - tick to run this script again");
        }
        ImGui::SetCursorScreenPos(resume);   // the fields continue under the header

        if (open) {
            if (script.className.empty()) {
                ImGui::TextDisabled("Remove this script (right-click its header) and attach one through Add Component.");
            } else if (DrawScriptFields(script, slot)) {
                changed = true; NoteEdit("Edit " + header);
            }
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    // "Created X.cs - compiling..." after Create C# script, once under the scripts.
    if (m_scriptNoticeTarget == entity && !m_scriptNotice.empty()) ImGui::TextWrapped("%s", m_scriptNotice.c_str());

    switch (change) {
    case Change::Remove:
        if (component->scripts.size() <= 1) {
            // The last one: the component goes too, with the other component removals.
            deferredRemove = MakeComponentRemover<ScriptComponent>(m_world, entity);
            removeLabel = "Remove " + ScriptHeader(component->scripts.front());
        } else {
            const std::string header = ScriptHeader(component->scripts[target]);
            component->scripts.erase(component->scripts.begin() + static_cast<std::ptrdiff_t>(target));
            changed = true; NoteEdit("Remove " + header);
        }
        break;
    case Change::Up:
    case Change::Down: {
        const std::size_t other = change == Change::Up ? target - 1 : target + 1;
        std::swap(component->scripts[target], component->scripts[other]);
        changed = true; NoteEdit("Reorder scripts");
        break;
    }
    case Change::None:
        break;
    }
    return changed;
}

bool Crucible::Inspector::DrawScriptFields(ScriptEntry& script, std::size_t slot) {
    auto* core = EditorManager::GetInstance().GetCore();
    const bool inSession = core && core->IsInPlaySession();
    auto* runtime = core && core->GetScriptRuntime().IsRunning() ? &core->GetScriptRuntime() : nullptr;
    // The class's public fields and their defaults, cached per class. Re-read after Scripts >
    // Reload Scripts too: fields may have been added, removed or re-defaulted.
    const uint64_t reload = runtime ? runtime->ReloadCount() : 0;
    const std::string cacheKey = script.assembly + "|" + script.nameSpace + "." + script.className;
    const auto cached = m_scriptFieldCache.find(cacheKey);
    if (cached == m_scriptFieldCache.end() || cached->second.reload != reload) {
        ScriptFieldCache entry;
        entry.reload = reload;
        ScriptAssemblyHandle assembly;
        if (!runtime) entry.error = "A Mono-enabled Editor is needed to inspect compiled public fields.";
        else if (!runtime->LoadAssembly(script.assembly, assembly) ||
                 !runtime->GetEntityFieldDefaults(assembly, script.nameSpace, script.className, entry.defaults))
            entry.error = runtime->LastError();
        m_scriptFieldCache[cacheKey] = std::move(entry);
    }
    const ScriptFieldCache& classFields = m_scriptFieldCache[cacheKey];
    auto fields = classFields.defaults;
    if (!classFields.error.empty()) fields = script.fields; // authored values survive unavailable assemblies
    else for (auto& field : fields) {
        const auto saved = std::find_if(script.fields.begin(), script.fields.end(), [&](const auto& value) { return SameScriptField(field, value); });
        if (saved != script.fields.end()) field = *saved;
    }
    EntityScriptStatus status;
    if (core) if (auto* system = core->Systems().FindSystem<EntityScriptSystem>()) status = system->Status(m_selected, slot);
    ScriptInstanceInfo info;
    bool live = inSession && runtime && status.instance && runtime->GetInstanceInfo(status.instance, info) && info.state != ScriptInstanceState::Faulted;
    if (live) {
        if (m_scriptFieldReadFailure.value == status.instance.value) live = false;
        else if (!runtime->ReadEntityFields(status.instance, fields)) {
            m_scriptFieldReadFailure = status.instance; m_scriptFieldReadError = runtime->LastError(); live = false;
        }
    }
    bool changed = false;
    ImGui::BeginDisabled(inSession && !live);
    for (auto field : fields) {
        std::string label;
        for (size_t i = 0; i < field.name.size(); ++i) {
            const char c = field.name[i];
            if (c == '_') { label += ' '; continue; }
            if (i && c >= 'A' && c <= 'Z' && field.name[i-1] >= 'a' && field.name[i-1] <= 'z') label += ' ';
            label += label.empty() && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        }
        if (std::count_if(fields.begin(), fields.end(), [&](const auto& other) { return other.name == field.name; }) > 1)
            label += " (" + field.declaringType + ")";
        label += "###" + field.name;
        ImGui::PushID(field.declaringType.c_str());
        bool edited = false;
        switch (field.type) {
        case ScriptFieldType::Boolean: edited = ImGui::Checkbox(label.c_str(), &field.boolean); break;
        case ScriptFieldType::Int32: edited = ImGui::InputScalar(label.c_str(), ImGuiDataType_S32, &field.integer); break;
        case ScriptFieldType::Single: edited = ImGui::InputFloat(label.c_str(), &field.single, 0, 0, "%.6g"); break;
        case ScriptFieldType::Double: edited = ImGui::InputDouble(label.c_str(), &field.real, 0, 0, "%.12g"); break;
        case ScriptFieldType::String:
            edited = ImGui::InputText(label.c_str(), &field.text);
            if (edited) field.stringIsNull = false;
            break;
        case ScriptFieldType::Entity: {
            // Same drag-from-Hierarchy picker as a PhysicsJoint's connected body; stores the scene id.
            const std::string shown = label.substr(0, label.find("###"));
            edited = EntityRefField(shown.c_str(), field.entityId);
            break;
        }
        case ScriptFieldType::Prefab:
        case ScriptFieldType::AudioClip:
        case ScriptFieldType::Texture:
        case ScriptFieldType::Font:
        case ScriptFieldType::Material:
        case ScriptFieldType::Model: {
            const std::string shown = label.substr(0, label.find("###"));
            edited = AssetRefField(shown.c_str(), field.assetId, field.type);
            break;
        }
        }
        if (edited && ((field.type == ScriptFieldType::Single && !std::isfinite(field.single)) ||
                       (field.type == ScriptFieldType::Double && !std::isfinite(field.real)))) edited = false;
        if (edited) {
            if (live) runtime->SetEntityFields(status.instance, {field}); // Play edits stay on the managed instance
            else {
                auto saved = std::find_if(script.fields.begin(), script.fields.end(), [&](const auto& value) { return SameScriptField(field, value); });
                if (saved == script.fields.end()) script.fields.push_back(std::move(field)); else *saved = std::move(field);
                changed = true;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (fields.empty() && classFields.error.empty()) ImGui::TextDisabled("No public fields.");
    std::string error = !status.error.empty() ? status.error : classFields.error;
    if (status.instance.value == m_scriptFieldReadFailure.value && status.instance) error = m_scriptFieldReadError;
    if (!error.empty()) { ImGui::TextDisabled("Script unavailable or failed (hover for details)"); ImGui::SetItemTooltip("%s", error.c_str()); }
    return changed;
}

void Crucible::Inspector::DrawCreateScriptModal() {
    if (m_scriptCreateRequested && !ImGui::IsPopupOpen("AddComponentPopup")) {
        ImGui::OpenPopup("Create C# script");
        m_scriptCreateRequested = false;
    }
    if (!ImGui::BeginPopupModal("Create C# script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::InputText("Class name", &m_scriptNewName);
    ImGui::InputText("Namespace", &m_scriptNewNamespace);
    ImGui::TextUnformatted("Creates C# source and attaches it to the selected entity.\nIt compiles and reloads by itself (see the menu bar).");
    auto* core = EditorManager::GetInstance().GetCore();
    // Added alongside any scripts the entity already has.
    const bool valid = m_scriptCreateTarget == m_selected && m_world.IsAlive(m_scriptCreateTarget) &&
        (!core || !core->IsInPlaySession());
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Create and assign")) {
        std::filesystem::path output;
        const auto projectRoot = std::filesystem::path(m_assets.GetAssetRoot()).parent_path();
        if (ScriptAuthoring::Create(projectRoot, m_scriptNewName, m_scriptNewNamespace, output, m_scriptCreateError)) {
            AssignScript(m_selected, ProjectScriptAssembly, m_scriptNewNamespace, m_scriptNewName);
            m_scriptNoticeTarget = m_selected;
            m_scriptNotice = "Source created. It compiles and reloads by itself; wait for the Console before Play.";
            CRU_USER_INFO("Created entity script: {}", output.string());
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    if (!valid) ImGui::TextUnformatted("Select the original entity, stop Play and check its script component before creating.");
    if (!m_scriptCreateError.empty()) ImGui::TextWrapped("%s", m_scriptCreateError.c_str());
    ImGui::EndPopup();
}
