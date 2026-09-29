/*!
* @file			ImGuiPanel_Hierarchy.cpp
* @brief		The editor's Hierarchy (list of entities in the open scene). Placeholder for now.
*/

#include "ImGui/Panels/ImGuiPanel_Hierarchy.h"

void Crucible::Hierarchy::Init() {
	m_panelName = "Hierarchy";

	// Left column above the bottom area (Unity default layout).
	m_docked = true;
	m_dockArea = DockArea::Left;
	m_dockRatio = 0.22f;
}

void Crucible::Hierarchy::Update(float) {
	if (ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {
		ImGui::TextDisabled("Scene entities will be listed here.");
	}
	ImGui::End();
}
