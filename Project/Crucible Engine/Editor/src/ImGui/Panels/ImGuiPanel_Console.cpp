/*!
* @file			ImGuiPanel_Console.cpp
* @brief		The editor's Console (log output). Placeholder for now.
*/

#include "ImGui/Panels/ImGuiPanel_Console.h"

void Crucible::Console::Init() {
	m_panelName = "Console";

	// Bottom area, tabbed behind the Asset Browser.
	m_docked = true;
	m_dockArea = DockArea::Bottom;
}

void Crucible::Console::Update(float) {
	if (ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {
		ImGui::TextDisabled("Log output will appear here.");
		ImGui::TextDisabled("Needs an Engine hook to register an editor log sink (see LogSink.h).");
	}
	ImGui::End();
}
