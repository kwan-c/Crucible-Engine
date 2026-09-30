/*!
* @file			ImGuiPanel_Inspector.cpp
* @brief		The editor's inspector for viewing and editing object properties.
*/

#include "ImGuiPanel_Inspector.h"
#include "ImGui/EditorWidgets.h"
#include "Diagnostics/Logger.h"
#include "Scene/SceneManager.h"

#include <cstdio>
#include <string>

namespace {

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

}

void Crucible::Inspector::Init() {
	m_panelName = "Inspector";

	// Start docked on the right side, taking a quarter of the width.
	m_docked = true;
	m_dockArea = DockArea::Right;
	m_dockRatio = 0.25f;

	// Own index for resolving a dropped texture's virtual path to a filesystem path (see
	// m_assets' doc comment in the header).
	if (m_assets.Init()) { m_assets.Refresh(); }
}

void Crucible::Inspector::Update(float) {
	if (ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {
		if (m_selected.IsValid() && m_world.IsAlive(m_selected)) {
			DrawEntityInspector(m_selected);
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

	// One collapsible section per component the entity has. Skips the two the World manages
	// itself (same rule EntityIO uses for saving). PushID keeps same-named fields in different
	// components (Collider::radius vs CharacterController::radius) from sharing a widget ID.
#define CRU_INSPECT_COMPONENT(T)                                                        \
		if constexpr (!WorldManaged<T>::value && !std::is_same_v<T, WorldTransform>)        \
		{                                                                                   \
			if (T* component = m_world.GetComponent<T>(entity))                             \
			{                                                                               \
				std::string const header(ComponentTraits<T>::name);                         \
				ImGui::PushID(header.c_str());                                              \
				if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))\
				{                                                                           \
					changed |= DrawInstance(*component);                                    \
					changed |= DrawComponentExtras(*component);                             \
				}                                                                           \
				ImGui::PopID();                                                             \
			}                                                                               \
		}

	CRUCIBLE_COMPONENT_LIST(CRU_INSPECT_COMPONENT)
	#undef CRU_INSPECT_COMPONENT

		if (changed) {
			m_scenes.MarkDirty();
		}
}

//bool Crucible::Inspector::DrawTransformSection(Transform& transform) {
//	bool changed = false;
//
//	changed |= ImGui::DragFloat3("Position", &transform.positions.x, 0.05f);
//
//	// Edited as Euler degrees, stored as a quaternion - matches this component's own
//	// default-construction pattern (Math::quat(Math::vec3(0,0,0))). Re-derived from the
//	// quaternion every frame rather than cached, so an external change (e.g. physics) is
//	// always reflected; this does mean two very different Euler triples can produce the same
//	// orientation (gimbal ambiguity) and the displayed angles can jump between equivalent
//	// representations frame to frame while not being dragged - a known, accepted simplification.
//	Math::vec3 euler = glm::degrees(glm::eulerAngles(transform.rotation));
//	if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f)) {
//		transform.rotation = Math::quat(glm::radians(euler));
//		changed = true;
//	}
//
//	ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f);
//
//	return changed;
//}
//
//bool Crucible::Inspector::DrawMeshRendererSection(MeshRenderer& renderer) {
//	bool changed = false;
//
//	changed |= ImGui::Checkbox("Visible", &renderer.visible);
//	changed |= ImGui::ColorEdit4("Colour", &renderer.colour.x);
//
//	// Meshes are procedural (Renderer::CreateMesh from a primitive generator, or a .glb load),
//	// not asset-indexed like textures, so there's no drag-drop swap for this slot yet - just a
//	// read-only display of which GPU mesh is bound. The Renderer owns the id->name mapping
//	// (GpuMesh::debugName, set when the mesh was created - see Renderer::CreateMesh's doc
//	// comment) rather than this panel guessing or keeping its own map, so this always reflects
//	// whatever the Renderer actually has, however the mesh got there.
//	if (renderer.mesh.IsValid()) {
//		ImGui::Text("Mesh: %s (id %u)", m_renderer.GetMeshName(renderer.mesh).c_str(), renderer.mesh.id);
//	} else {
//		ImGui::TextDisabled("Mesh: none");
//	}
//
//	return changed;
//}

bool Crucible::Inspector::DrawMaterialSection(MeshRenderer& renderer) {
	// No material assigned yet: offer to create one from the Renderer's neutral defaults rather
	// than drawing an empty section. CreateMaterial() writes the new handle straight onto the
	// component; every widget below then edits it.
	if (!renderer.material.IsValid()) {
		if (ImGui::Button("Add Material")) {
			renderer.material = m_renderer.CreateMaterial(MaterialData{});
			return true;
		}
		return false;
	}

	// GetMaterialData returns the live GPU-side copy; edit a local copy and only push it back
	// (UpdateMaterial) when something actually changed this frame - see UpdateMaterial's own
	// comment on why that distinction matters (a full descriptor rewrite + wait-idle when a
	// texture binding changes, versus free when only a push-constant factor changed).
	MaterialData data = m_renderer.GetMaterialData(renderer.material);
	bool changed = false;

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
			if (AssetEntry const* entry = m_assets.Find(dropped)) {
				if (entry->type == AssetType::Texture) {
					handle = m_renderer.CreateTexture(m_assets.Resolve(dropped).string(), slot.isSRGB);
					changed = true;
				} else {
					CRU_WARN("[Inspector] Dropped asset '{}' onto {} is not a texture - ignored", dropped, slot.label);
				}
			}
		}

		ImGui::PopID();
	}

	if (changed) {
		m_renderer.UpdateMaterial(renderer.material, data);
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

		if (type == rttr::type::get<bool>()) {
			bool v = value.get_value<bool>();
			if (ImGui::Checkbox(name, &v)) { newValue = v; edited = true; }
		}
		else if (type == rttr::type::get<int>()) {
			int v = value.get_value<int>();
			if (ImGui::DragInt(name, &v)) { newValue = v; edited = true; }
		}
		else if (type == rttr::type::get<std::uint32_t>()) {
			std::uint32_t v = value.get_value<std::uint32_t>();
			if (ImGui::DragScalar(name, ImGuiDataType_U32, &v)) { newValue = v; edited = true; }
		}
		else if (type == rttr::type::get<float>()) {
			float v = value.get_value<float>();
			rttr::variant const range = prop.get_metadata(MetaDataTypes::SLIDER);
			edited = range.is_type<Math::vec2>()
				? ImGui::SliderFloat(name, &v, range.get_value<Math::vec2>().x, range.get_value<Math::vec2>().y)
				: ImGui::DragFloat(name, &v, 0.05f);
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
		}
		else if (type == rttr::type::get<Math::vec3>()) {
			Math::vec3 v = value.get_value<Math::vec3>();
			edited = prop.get_metadata(MetaDataTypes::COLOUR).to_bool()
				? ImGui::ColorEdit3(name, &v.x)
				: ImGui::DragFloat3(name, &v.x, 0.05f);
			if (edited) newValue = v;
		}
		else if (type == rttr::type::get<Math::vec4>()) {
			Math::vec4 v = value.get_value<Math::vec4>();
			edited = prop.get_metadata(MetaDataTypes::COLOUR).to_bool()
				? ImGui::ColorEdit4(name, &v.x)
				: ImGui::DragFloat4(name, &v.x, 0.05f);
			if (edited) newValue = v;
		}
		else if (type == rttr::type::get<Math::quat>()) {
			// Edited as Euler degrees, stored as a quaternion (same trade-off as before).
			Math::vec3 euler = glm::degrees(glm::eulerAngles(value.get_value<Math::quat>()));
			if (ImGui::DragFloat3(name, &euler.x, 0.5f)) { newValue = Math::quat(glm::radians(euler)); edited = true; }
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
			prop.set_value(obj, newValue);
			changed = true;
		}
		ImGui::EndDisabled();
	}
	return changed;
}

bool Crucible::Inspector::DrawComponentExtras(MeshRenderer& renderer) {
	if (renderer.mesh.IsValid()) {
		ImGui::Text("Mesh: %s (id %u)", m_renderer.GetMeshName(renderer.mesh).c_str(), renderer.mesh.id);
	}
	else {
		ImGui::TextDisabled("Mesh: none");
	}

	ImGui::SeparatorText("Material");
	return DrawMaterialSection(renderer);   // your existing function, returning bool
}