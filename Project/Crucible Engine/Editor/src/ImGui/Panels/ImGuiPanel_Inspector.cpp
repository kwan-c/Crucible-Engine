/*!
* @file			ImGuiPanel_Inspector.cpp
* @brief		The editor's inspector for viewing and editing object properties.
*/

#include "ImGuiPanel_Inspector.h"

void Crucible::Inspector::Init() {
	m_panelName = "Inspector";

	// Start docked on the right side, taking a quarter of the width.
	m_docked = true;
	m_dockArea = DockArea::Right;
	m_dockRatio = 0.25f;
}

void Crucible::Inspector::Update(float) {
	if (ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {
		if (m_draw) m_draw();
		else ImGui::TextDisabled("Nothing selected");
	}

	ImGui::End();
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
