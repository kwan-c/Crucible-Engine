/*!
* @file			ImGuiPanel_Menu.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's main menu bar (File / Edit / View / Help).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_Menu.h"
#include "Scene/SceneManager.h"
#include "Diagnostics/Logger.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"
#include "ImGui/Panels/ImGuiPanel_Animator.h"
#include "ImGui/EditorTheme.h"
#include "ImGui/Panels/ImGuiPanel_GameView.h"
#include "Configuration/EngineConfigLoader.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <exception>
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
	auto animator = EditorManager::GetInstance().GetPanel<AnimatorPanel>(ImguiPanels::ANIMATOR);
	if ((!animator || !animator->HasKeyboardFocus()) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
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
			if (ImGui::MenuItem("Project Settings...", nullptr, false, !playing)) {
				EditConfigSettings();
			}

			if (ImGui::MenuItem("Exit", "Alt+F4"))
			{
				// The ImGui GLFW backend stores the GLFWwindow* in the main viewport,
				// so the Editor can close the window without any Engine-side hook.
				RequestGuarded(GuardedAction::Exit);
			}
			ImGui::EndMenu();
		}

		// ---------------- Scene ----------------
		if (ImGui::BeginMenu("Scene"))
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
			if (ImGui::MenuItem("Animator")) { if (animator) animator->Show(); }
			// The third argument is a bool* that MenuItem toggles for us (shows a tick).
			ImGui::MenuItem("ImGui Demo Window", nullptr, &m_showDemoWindow);
			ImGui::MenuItem("FPS Counter", nullptr, &m_showFps);
			ImGui::Separator();
			if (ImGui::BeginMenu("Theme"))
			{
				EditorTheme::DrawMenu(&m_showThemeEditor);
				ImGui::EndMenu();
			}
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

	if (m_openProjectSettingsPopup) {
		ImGui::OpenPopup("Project Settings");
		m_openProjectSettingsPopup = false;
	}

	DrawOpenScenePopup();
	DrawSaveAsPopup();
	DrawUnsavedChangesPopup();
	DrawProjectSettingsPopup();

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

	// Colour / size editor with Save as Custom Theme; opened from View > Theme.
	EditorTheme::DrawEditorWindow(&m_showThemeEditor);
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
	case GuardedAction::Exit: {
		auto finish = [this] { m_quitConfirmed = true; if (GLFWwindow* window = MainWindow()) glfwSetWindowShouldClose(window, GLFW_TRUE); };
		auto animator = EditorManager::GetInstance().GetPanel<AnimatorPanel>(ImguiPanels::ANIMATOR);
		if (!animator || animator->GuardExit(finish)) finish();
		break;
	}
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
	m_sceneFiles = ScanSceneFiles();
	m_openScenePopup = true;
}

std::vector<std::string> Crucible::Menu::ScanSceneFiles() const {
	std::vector<std::string> files;
	std::error_code ec;
	for (const auto& e : std::filesystem::directory_iterator(m_assets.Resolve("Scenes"), ec))
		if (e.is_regular_file() && e.path().extension() == ".scene")
			files.push_back(e.path().filename().string());
	std::sort(files.begin(), files.end());
	return files;
}

// ---------------------------------------------------------------------------------------------
//  File > Project Settings
// ---------------------------------------------------------------------------------------------

void Crucible::Menu::EditConfigSettings()
{
	if (BlockedByPlayMode("change project settings")) { return; }

	m_projectSettingsPath = EngineConfigLoader::FindConfigFile();
	if (m_projectSettingsPath.empty()) {
		CRU_USER_WARN("[Menu] No Assets folder found - cannot open the project settings");
		return;
	}

	// Load() reads the shared values only - no "editor" overrides - so saving can't bake the
	// editor's overrides into what the Game reads. A missing file just means defaults (Save
	// creates it); a broken one is left for a human to fix rather than overwritten.
	m_projectSettings = EngineConfig{};
	m_gameOverrides   = AppOverrides{};
	std::error_code ec;
	if (std::filesystem::exists(m_projectSettingsPath, ec)) {
		try {
			m_projectSettings = EngineConfigLoader::Load(m_projectSettingsPath);
			m_gameOverrides   = EngineConfigLoader::LoadOverrides(AppKind::Game, m_projectSettingsPath);
		}
		catch (const std::exception& e) {
			CRU_USER_ERROR("[Menu] Could not read '{}': {} - fix the file by hand first", m_projectSettingsPath.string(), e.what());
			return;
		}
	}

	std::snprintf(m_projectWindowTitle, sizeof(m_projectWindowTitle), "%s", m_projectSettings.graphics.windowTitle.c_str());
	std::snprintf(m_gameWindowTitle, sizeof(m_gameWindowTitle), "%s", m_gameOverrides.windowTitle.value_or(m_projectSettings.graphics.windowTitle).c_str());
	m_projectSceneFiles = ScanSceneFiles();
	m_openProjectSettingsPopup = true;
}

namespace {
	// Startup-scene picker shared by the Shared and Game tabs: "(none)" plus every scene in
	// Assets/Scenes, and a warning when the chosen file no longer exists.
	void StartupSceneCombo(char const* label, std::string& scene, std::vector<std::string> const& files, Crucible::AssetManager& assets)
	{
		ImGui::SetNextItemWidth(260.0f);
		if (ImGui::BeginCombo(label, scene.empty() ? "(none)" : scene.c_str())) {
			if (ImGui::Selectable("(none)", scene.empty()))
				scene.clear();
			for (std::string const& name : files) {
				std::string const relative = "Scenes/" + name;
				bool const selected = (relative == scene);
				if (ImGui::Selectable(relative.c_str(), selected))
					scene = relative;
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (!scene.empty()) {
			std::error_code ec;
			if (!std::filesystem::exists(assets.Resolve(scene), ec))
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "'%s' does not exist - the engine will start with no scene", scene.c_str());
		}
	}

	// Width x height boxes, each side kept >= 1.
	bool WindowSizeInput(char const* label, uint32_t& width, uint32_t& height)
	{
		int size[2] = { static_cast<int>(width), static_cast<int>(height) };
		ImGui::SetNextItemWidth(260.0f);
		if (!ImGui::InputInt2(label, size)) return false;
		width  = static_cast<uint32_t>(std::max(size[0], 1));
		height = static_cast<uint32_t>(std::max(size[1], 1));
		return true;
	}

	// The tick box in front of a Game-tab field. Ticking it starts the override from the shared
	// value; the field after it is greyed out (showing the shared value) while it is unticked.
	bool OverrideToggle(char const* id, bool overridden)
	{
		ImGui::Checkbox(id, &overridden);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(overridden ? "Overridden for the Game - untick to use the shared value" : "Tick to override this for the Game");
		ImGui::SameLine();
		return overridden;
	}
}

void Crucible::Menu::DrawProjectSettingsPopup()
{
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ProjectConfig&  project  = m_projectSettings.project;
	GrahpicsConfig& graphics = m_projectSettings.graphics;

	ImGui::TextDisabled("%s", m_projectSettingsPath.string().c_str());

	if (ImGui::BeginTabBar("##ProjectSettingsTabs"))
	{
		// ================= Shared: what both the Editor and the Game start from =================
		if (ImGui::BeginTabItem("Shared"))
		{
			ImGui::TextDisabled("Used by both the Editor and the Game, unless the Game tab overrides it.");

			ImGui::SeparatorText("Project");
			StartupSceneCombo("Startup Scene", project.startupScene, m_projectSceneFiles, m_assets);

			int aspect[2] = { static_cast<int>(project.aspectWidth), static_cast<int>(project.aspectHeight) };
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputInt2("##AspectRatio", aspect)) {
				project.aspectWidth  = static_cast<uint32_t>(std::max(aspect[0], 0));
				project.aspectHeight = static_cast<uint32_t>(std::max(aspect[1], 0));
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(130.0f);
			if (ImGui::BeginCombo("Aspect Ratio", "Presets")) {
				struct Preset { char const* name; uint32_t w; uint32_t h; };
				constexpr Preset kPresets[] = { { "Free", 0, 0 }, { "16:9", 16, 9 }, { "16:10", 16, 10 }, { "4:3", 4, 3 }, { "1:1", 1, 1 } };
				for (Preset const& p : kPresets) {
					if (ImGui::Selectable(p.name, project.aspectWidth == p.w && project.aspectHeight == p.h)) {
						project.aspectWidth  = p.w;
						project.aspectHeight = p.h;
					}
				}
				ImGui::EndCombo();
			}
			if (project.AspectRatio() > 0.0f)
				ImGui::TextDisabled("The Game locks its window to %u:%u; the Game view switches to it when you save.", project.aspectWidth, project.aspectHeight);
			else
				ImGui::TextDisabled("Free aspect (either side is 0): the Game window can be any shape.");

			ImGui::SeparatorText("Window  (applies next launch)");
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputText("Title", m_projectWindowTitle, sizeof(m_projectWindowTitle));
			WindowSizeInput("Size", graphics.windowWidth, graphics.windowHeight);

			ImGui::EndTabItem();
		}

		// ================= Game: the standalone game's own values ===============================
		if (ImGui::BeginTabItem("Game"))
		{
			AppOverrides& game = m_gameOverrides;

			ImGui::TextDisabled("Only the Game app uses these. Unticked fields use the Shared value.");

			// ---- Startup scene ----
			ImGui::SeparatorText("Project  (applies next launch)");
			bool const sceneOn = OverrideToggle("##OverrideScene", game.startupScene.has_value());
			if (sceneOn && !game.startupScene) game.startupScene = project.startupScene;
			if (!sceneOn) game.startupScene.reset();
			{
				std::string shown = sceneOn ? *game.startupScene : project.startupScene;
				ImGui::BeginDisabled(!sceneOn);
				StartupSceneCombo("Startup Scene##Game", shown, m_projectSceneFiles, m_assets);
				ImGui::EndDisabled();
				if (sceneOn) game.startupScene = shown;
			}
			ImGui::TextDisabled("Aspect ratio is not overridable: it stays shared so the Editor's Game view frames the game the same way.");

			// ---- Window ----
			ImGui::SeparatorText("Window  (applies next launch)");

			bool const titleOn = OverrideToggle("##OverrideTitle", game.windowTitle.has_value());
			if (titleOn && !game.windowTitle) {
				game.windowTitle = std::string(m_projectWindowTitle);
				std::snprintf(m_gameWindowTitle, sizeof(m_gameWindowTitle), "%s", m_projectWindowTitle);
			}
			if (!titleOn) game.windowTitle.reset();
			ImGui::SetNextItemWidth(260.0f);
			if (titleOn) {
				if (ImGui::InputText("Title##Game", m_gameWindowTitle, sizeof(m_gameWindowTitle)))
					game.windowTitle = std::string(m_gameWindowTitle);
			}
			else {
				ImGui::BeginDisabled();
				char shared[sizeof(m_projectWindowTitle)];
				std::snprintf(shared, sizeof(shared), "%s", m_projectWindowTitle);
				ImGui::InputText("Title##Game", shared, sizeof(shared));
				ImGui::EndDisabled();
			}

			// Width and height are overridden together - a half-overridden size is never what you want.
			bool const sizeOn = OverrideToggle("##OverrideSize", game.windowWidth.has_value() || game.windowHeight.has_value());
			if (sizeOn) {
				if (!game.windowWidth)  game.windowWidth  = graphics.windowWidth;
				if (!game.windowHeight) game.windowHeight = graphics.windowHeight;
				WindowSizeInput("Size##Game", *game.windowWidth, *game.windowHeight);
			}
			else {
				game.windowWidth.reset();
				game.windowHeight.reset();
				uint32_t w = graphics.windowWidth, h = graphics.windowHeight;
				ImGui::BeginDisabled();
				WindowSizeInput("Size##Game", w, h);
				ImGui::EndDisabled();
			}

			// What the game will actually start with, all overrides applied.
			EngineConfig shared = m_projectSettings;
			shared.graphics.windowTitle = m_projectWindowTitle;
			EngineConfig const result = game.ApplyTo(shared);
			ImGui::Spacing();
			ImGui::TextDisabled("The Game will start as \"%s\", %u x %u, opening %s.",
				result.graphics.windowTitle.c_str(), result.graphics.windowWidth, result.graphics.windowHeight,
				result.project.startupScene.empty() ? "no scene" : result.project.startupScene.c_str());

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	// ---- Save / Cancel (both tabs) ----
	ImGui::Spacing();
	ImGui::Separator();
	if (ImGui::Button("Save", ImVec2(120.0f, 0.0f))) {
		graphics.windowTitle = m_projectWindowTitle;
		bool const ok = EngineConfigLoader::Save(m_projectSettings, m_projectSettingsPath)
		             && EngineConfigLoader::SaveOverrides(AppKind::Game, m_gameOverrides, m_projectSettingsPath);
		if (ok) {
			// The aspect ratio is the one setting the open editor can show straight away.
			if (auto gameView = EditorManager::GetInstance().GetPanel<GameView>(ImguiPanels::GAMESCENE))
				gameView->SetProjectAspect(project.aspectWidth, project.aspectHeight);
			CRU_USER_INFO("[Menu] Project settings saved - startup scene and window changes apply on next launch");
			ImGui::CloseCurrentPopup();
		}
		else {
			CRU_USER_ERROR("[Menu] Could not save the project settings - see the log for why");
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}
