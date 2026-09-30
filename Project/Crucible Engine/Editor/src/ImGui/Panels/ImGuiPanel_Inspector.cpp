/*!
* @file			ImGuiPanel_Inspector.cpp
* @brief		The editor's inspector for viewing and editing object properties.
*/

#include "ImGuiPanel_Inspector.h"
#include "ImGui/EditorWidgets.h"
#include "Diagnostics/Logger.h"
#include "Scene/SceneManager.h"

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

	// Re-fetched fresh every frame rather than cached: see the header comment on why
	// SetSelectedEntity stores an Entity instead of a component reference.
	if (Transform* transform = m_world.GetComponent<Transform>(entity)) {
		if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= DrawTransformSection(*transform);
		}
	}

	if (MeshRenderer* renderer = m_world.GetComponent<MeshRenderer>(entity)) {
		if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= DrawMeshRendererSection(*renderer);
		}
		if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= DrawMaterialSection(*renderer);
		}
	}

	if (changed) {
		m_scenes.MarkDirty();
	}
}

bool Crucible::Inspector::DrawTransformSection(Transform& transform) {
	bool changed = false;

	changed |= ImGui::DragFloat3("Position", &transform.positions.x, 0.05f);

	// Edited as Euler degrees, stored as a quaternion - matches this component's own
	// default-construction pattern (Math::quat(Math::vec3(0,0,0))). Re-derived from the
	// quaternion every frame rather than cached, so an external change (e.g. physics) is
	// always reflected; this does mean two very different Euler triples can produce the same
	// orientation (gimbal ambiguity) and the displayed angles can jump between equivalent
	// representations frame to frame while not being dragged - a known, accepted simplification.
	Math::vec3 euler = glm::degrees(glm::eulerAngles(transform.rotation));
	if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f)) {
		transform.rotation = Math::quat(glm::radians(euler));
		changed = true;
	}

	ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f);

	return changed;
}

bool Crucible::Inspector::DrawMeshRendererSection(MeshRenderer& renderer) {
	bool changed = false;

	changed |= ImGui::Checkbox("Visible", &renderer.visible);
	changed |= ImGui::ColorEdit4("Colour", &renderer.colour.x);

	// Meshes are procedural (Renderer::CreateMesh from a primitive generator, or a .glb load),
	// not asset-indexed like textures, so there's no drag-drop swap for this slot yet - just a
	// read-only display of which GPU mesh is bound. The Renderer owns the id->name mapping
	// (GpuMesh::debugName, set when the mesh was created - see Renderer::CreateMesh's doc
	// comment) rather than this panel guessing or keeping its own map, so this always reflects
	// whatever the Renderer actually has, however the mesh got there.
	if (renderer.mesh.IsValid()) {
		ImGui::Text("Mesh: %s (id %u)", m_renderer.GetMeshName(renderer.mesh).c_str(), renderer.mesh.id);
	} else {
		ImGui::TextDisabled("Mesh: none");
	}

	return changed;
}

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
	for (auto& prop : obj.get_derived_type().get_properties()) {

		if (prop.get_metadata(MetaDataTypes::NOT_ON_IMGUI).to_bool()) continue;

		rttr::type type = prop.get_type();
		rttr::variant value = prop.get_value(obj);
		char const* name = prop.get_name().data();

		ImGui::BeginDisabled(prop.is_readonly() ||
			prop.get_metadata(MetaDataTypes::DISABLED_ON_IMGUI).to_bool());

		if (type == rttr::type::get<float>()) {
			float v = value.get_value<float>();
			if (ImGui::DragFloat(name, &v, 0.1f)) { prop.set_value(obj, v); changed = true; }
		}
		else if (type == rttr::type::get<int>()) {
			int v = value.get_value<int>();
			if (ImGui::DragInt(name, &v)) { prop.set_value(obj, v); changed = true; }
		}
		else if (type == rttr::type::get<bool>()) {
			bool v = value.get_value<bool>();
			if (ImGui::Checkbox(name, &v)) { prop.set_value(obj, v); changed = true; }
		}
		else if (type.is_class()) {   // vec3, nested structs, etc: recurse
			if (ImGui::TreeNode(name)) {
				if (DrawInstance(value)) { prop.set_value(obj, value); changed = true; }
				ImGui::TreePop();
			}
		}
		ImGui::EndDisabled();
	}
	return changed;
}
