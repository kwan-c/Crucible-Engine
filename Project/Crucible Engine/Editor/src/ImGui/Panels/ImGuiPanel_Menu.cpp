/*!
* @file			ImGuiPanel_Menu.cpp
* @brief		The editor's main menu bar (File / Edit / View / Help).
*/

#include "ImGui/Panels/ImGuiPanel_Menu.h"
#include "Scene/SceneManager.h"
#include "Diagnostics/Logger.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <filesystem>

#include "Core.h"

namespace {
	GLFWwindow* MainWindow() {
		// The ImGui GLFW backend stores the GLFWwindow* in the main viewport.
		return static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);
	}
}

void Crucible::Menu::Init() {
	m_panelName = "Menu";
}

void Crucible::Menu::Update(float) {

	// X button / Alt+F4 / Esc set GLFW's close flag during PollEvents, which runs before this
	// panel. Core::Run only checks it again at the top of the next loop, so clearing it here
	// cancels the close in time to ask first.
	if (GLFWwindow* window = MainWindow(); window && !m_quitConfirmed && glfwWindowShouldClose(window))
	{
		glfwSetWindowShouldClose(window, GLFW_FALSE);
		RequestGuarded(GuardedAction::Exit);   // not dirty -> quits straight away
	}

	// Exit asked for during play mode: Stop has run since (it restores the edited scene and
	// its unsaved-changes flag), so now ask about saving as usual.
	if (m_exitAfterStop && !m_scenes.IsPlaySessionActive())
	{
		m_exitAfterStop = false;
		RequestGuarded(GuardedAction::Exit);
	}

	// Scene files are edit-mode only: the world holds runtime state until Stop.
	bool const playing = m_scenes.IsPlaySessionActive();

	// Keyboard shortcuts. Checked here every frame, NOT inside BeginMenu("File"),
	// which only runs while the File menu is open. Skipped while any popup is up,
	// so pressing Ctrl+S while typing a name in "Save Scene As" doesn't save behind it.
	if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
	{
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
			SaveScene();
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal))
			OpenSceneFunc();
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal))
			RequestGuarded(GuardedAction::New);

		// Undo / Redo. Not while a widget is held (mid-drag). While typing in a text field,
		// ImGui's InputText keeps Ctrl+Z for its own text undo: its focused route outranks
		// this global one.
		if (!ImGui::IsAnyItemActive())
		{
			EditorManager& editor = EditorManager::GetInstance();
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
				editor.Undo();
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
				ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
				editor.Redo();
		}
	}

	if (ImGui::BeginMainMenuBar())
	{
		// ---------------- File ----------------
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene", "Ctrl+N", false, !playing)) {
				RequestGuarded(GuardedAction::New);
			}

			if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, !playing)) {
				OpenSceneFunc();
			}


			if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !playing)) {
				if (!m_scenes.HasPath()) m_openSaveAsPopup = true;   // never saved -> ask for a name
				else                     { SaveScene(); }
			}

			if (ImGui::MenuItem("Save Scene As...", nullptr, false, !playing))
				m_openSaveAsPopup = true;

			if (playing)
				ImGui::TextDisabled("Stop play mode to open or save scenes");

			ImGui::Separator();

			if (ImGui::MenuItem("Exit", "Alt+F4"))
			{
				// The ImGui GLFW backend stores the GLFWwindow* in the main viewport,
				// so the Editor can close the window without any Engine-side hook.
				RequestGuarded(GuardedAction::Exit);
			}
			ImGui::EndMenu();
		}

		// ---------------- Edit ----------------
		if (ImGui::BeginMenu("Edit"))
		{
			// Names what would be undone ("Undo Edit Transform"); greyed out when there is nothing.
			EditorManager& editor = EditorManager::GetInstance();
			UndoHistory const& history = editor.GetHistory();
			std::string const undoText = history.CanUndo() ? "Undo " + history.UndoLabel() : std::string("Undo");
			std::string const redoText = history.CanRedo() ? "Redo " + history.RedoLabel() : std::string("Redo");
			if (ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false, history.CanUndo())) { editor.Undo(); }
			if (ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false, history.CanRedo())) { editor.Redo(); }
			ImGui::EndMenu();
		}

		// ---------------- View ----------------
		if (ImGui::BeginMenu("View"))
		{
			// The third argument is a bool* that MenuItem toggles for us (shows a tick).
			ImGui::MenuItem("ImGui Demo Window", nullptr, &m_showDemoWindow);
			ImGui::MenuItem("FPS Counter", nullptr, &m_showFps);
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout"))
				ImGuiManager::ResetLayout();
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

	if (m_openScenePopup) { 
		ImGui::OpenPopup("Open Scene");    
		m_openScenePopup = false; 
	}

	if (m_openSaveAsPopup) { 
		ImGui::OpenPopup("Save Scene As"); 
		m_openSaveAsPopup = false; 
	}

	if (m_openUnsavedPopup) {
		ImGui::OpenPopup("Unsaved Changes");
		m_openUnsavedPopup = false;
	}

	DrawOpenScenePopup();
	DrawSaveAsPopup();
	DrawUnsavedChangesPopup();

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

bool Crucible::Menu::BlockedByPlayMode(const char* what) const
{
	if (!m_scenes.IsPlaySessionActive()) { return false; }
	CRU_USER_WARN("Cannot {} while in play mode - press Stop first", what);
	return true;
}

void Crucible::Menu::RequestGuarded(GuardedAction action, std::filesystem::path path)
{
	// Leaving during play mode: Stop first, then ask about the EDITED scene's changes
	// (see Update). The play-mode world itself is never saved.
	if (action == GuardedAction::Exit && m_scenes.IsPlaySessionActive())
	{
		Core* core = EditorManager::GetInstance().GetCore();
		if (core != nullptr)
		{
			core->RequestStop();
			m_exitAfterStop = true;
			return;
		}
	}
	if (action == GuardedAction::New && BlockedByPlayMode("create a new scene")) { return; }
	if (action == GuardedAction::Open && BlockedByPlayMode("open a scene")) { return; }

	if (!m_scenes.IsDirty()) { RunAction(action, path); return; }

	// Dirty: remember what was asked for and ask the user first.
	m_guardedAction = action;
	m_guardedPath = std::move(path);
	m_openUnsavedPopup = true;
}

void Crucible::Menu::RunAction(GuardedAction action, const std::filesystem::path& path)
{
	switch (action)
	{
	case GuardedAction::New:  m_scenes.RequestNew();      break;
	case GuardedAction::Open: m_scenes.RequestLoad(path); break;
	case GuardedAction::Exit: m_quitConfirmed = true; if (GLFWwindow* window = MainWindow()) glfwSetWindowShouldClose(window, GLFW_TRUE);    break;
	case GuardedAction::None: break;
	}
}

void Crucible::Menu::DrawOpenScenePopup()
{
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	if (m_sceneFiles.empty())
		ImGui::TextDisabled("No .scene files in Assets/Scenes");

	for (const std::string& name : m_sceneFiles)
	{
		if (ImGui::Selectable(name.c_str()))
		{
			RequestGuarded(GuardedAction::Open, m_assets.Resolve("Scenes/" + name));
			ImGui::CloseCurrentPopup();
		}
	}

	ImGui::Separator();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
		ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

// A scene file only stores which material each entity uses (an asset id), not the material's
// values - those live in the .mat files. So saving the scene also writes any material edited in
// the Inspector since its last save, otherwise "saved" scenes would reload with old materials.
// Runs before the scene save and independently of it: a failed material is logged and left
// marked unsaved, but does not block the scene from saving.
void Crucible::Menu::SaveEditedMaterials()
{
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (!inspector) {
		CRU_WARN("[Menu] No Inspector panel - edited materials were not saved with the scene");
		return;
	}
	if (std::size_t const failed = inspector->SaveUnsavedMaterials())
		CRU_USER_WARN("[Menu] {} edited material(s) could not be saved - see the errors above", failed);
}

void Crucible::Menu::DrawSaveAsPopup()
{
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::InputText("Name", m_saveAsName, sizeof(m_saveAsName));

	if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)) && m_saveAsName[0] != '\0')
	{
		SaveEditedMaterials();
		bool const ok = m_scenes.SaveAs(m_assets.Resolve(std::string("Scenes/") + m_saveAsName + ".scene"));
		m_assets.Refresh();

		if (ok && m_continueAfterSaveAs)
			RunAction(m_guardedAction, m_guardedPath);
		m_continueAfterSaveAs = false;
		m_guardedAction = GuardedAction::None;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
	{
		m_continueAfterSaveAs = false;
		m_guardedAction = GuardedAction::None;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Crucible::Menu::DrawUnsavedChangesPopup()
{
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::Text("'%s' has unsaved changes.", m_scenes.CurrentName().c_str());
	ImGui::TextUnformatted("Save before continuing?");
	ImGui::Separator();

	if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)))
	{
		if (m_scenes.HasPath())
		{
			SaveEditedMaterials();
			if (m_scenes.Save())                        // on failure: stay put, error already logged
				RunAction(m_guardedAction, m_guardedPath);
			m_guardedAction = GuardedAction::None;
		}
		else
		{
			// Never saved: ask for a name, then carry on (see DrawSaveAsPopup).
			m_continueAfterSaveAs = true;
			m_openSaveAsPopup = true;
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Don't Save", ImVec2(120.0f, 0.0f)))
	{
		RunAction(m_guardedAction, m_guardedPath);
		m_guardedAction = GuardedAction::None;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
	{
		m_guardedAction = GuardedAction::None;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Crucible::Menu::SaveScene()
{
	if (BlockedByPlayMode("save the scene")) { return; }
	if (!m_scenes.HasPath()) { m_openSaveAsPopup = true; return; }   // never saved -> ask for a name
	SaveEditedMaterials();
	m_scenes.Save();
}

void Crucible::Menu::OpenSceneFunc() {
	if (BlockedByPlayMode("open a scene")) { return; }
	m_sceneFiles.clear();
	std::error_code ec;
	for (const auto& e : std::filesystem::directory_iterator(m_assets.Resolve("Scenes"), ec))
		if (e.is_regular_file() && e.path().extension() == ".scene")
			m_sceneFiles.push_back(e.path().filename().string());
	m_openScenePopup = true;
}
