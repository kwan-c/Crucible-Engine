/*!
* @file			ImGuiPanel_Menu.cpp
* @brief		The editor's main menu bar (File / Edit / View / Help).
*/

#include "ImGui/Panels/ImGuiPanel_Menu.h"
#include "Diagnostics/Logger.h"

#include <GLFW/glfw3.h>
#include <cstdio>

void Crucible::Menu::Init() {
	m_panelName = "Menu";
}

void Crucible::Menu::Update() {

	if (ImGui::BeginMainMenuBar())
	{
		// ---------------- File ----------------
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene", "Ctrl+N"))
				CRU_INFO("[Menu] File > New Scene (not implemented yet)");
			if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
				CRU_INFO("[Menu] File > Open Scene (not implemented yet)");
			if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
				CRU_INFO("[Menu] File > Save Scene (not implemented yet)");

			ImGui::Separator();

			if (ImGui::MenuItem("Exit", "Alt+F4"))
			{
				// The ImGui GLFW backend stores the GLFWwindow* in the main viewport,
				// so the Editor can close the window without any Engine-side hook.
				auto* window = static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);
				if (window)
					glfwSetWindowShouldClose(window, GLFW_TRUE);
			}
			ImGui::EndMenu();
		}

		// ---------------- Edit ----------------
		if (ImGui::BeginMenu("Edit"))
		{
			// Greyed-out placeholders (enabled = false).
			ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
			ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
			ImGui::EndMenu();
		}

		// ---------------- View ----------------
		if (ImGui::BeginMenu("View"))
		{
			// The third argument is a bool* that MenuItem toggles for us (shows a tick).
			ImGui::MenuItem("ImGui Demo Window", nullptr, &m_showDemoWindow);
			ImGui::MenuItem("FPS Counter", nullptr, &m_showFps);
			ImGui::EndMenu();
		}

		// ---------------- Help ----------------
		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About Crucible Editor"))
				m_showAbout = true;
			ImGui::EndMenu();
		}

		// FPS readout, right-aligned inside the menu bar.
		if (m_showFps)
		{
			char fps[32];
			std::snprintf(fps, sizeof(fps), "%.1f FPS", static_cast<double>(ImGui::GetIO().Framerate));
			float const textWidth = ImGui::CalcTextSize(fps).x;
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - textWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f);
			ImGui::TextUnformatted(fps);
		}

		ImGui::EndMainMenuBar();
	}

	// Popups must be opened/begun outside the menu's ID scope, hence the flag above.
	if (m_showAbout)
	{
		ImGui::OpenPopup("About Crucible Editor");
		m_showAbout = false;
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("About Crucible Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Crucible Engine Editor");
		ImGui::TextUnformatted("Team Medium Well - GAM300");
		ImGui::Separator();
		ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
		ImGui::Spacing();
		if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Handy reference window for every ImGui widget; toggled from View.
	if (m_showDemoWindow)
		ImGui::ShowDemoWindow(&m_showDemoWindow);
}
