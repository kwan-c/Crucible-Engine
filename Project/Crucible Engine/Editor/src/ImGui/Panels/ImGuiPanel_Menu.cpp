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
#include "ImGui/Panels/ImGuiPanel_BehaviorGraph.h"
#include "ImGui/Panels/ImGuiPanel_StateGraph.h"
#include "ImGui/Panels/ImGuiPanel_ShaderGraph.h"
#include "ImGui/EditorTheme.h"
#include "ImGui/Panels/ImGuiPanel_GameView.h"
#include "ImGui/Panels/ImGuiPanel_FontImporter.h"
#include "ImGui/Panels/ImGuiPanel_TextureImporter.h"
#include "ImGui/Panels/ImGuiPanel_PackageGame.h"
#include "Configuration/EngineConfigLoader.h"
#include "Cook.h"   // Tools/Cook
#include "Assets/BuildProblems.h"
#include "Prefabs/PrefabStage.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>

#include "Core.h"
#include "Scripting/ScriptRuntime.h"

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
	// The same for Prefab Mode, which has restored the scene by then - unless its Save / Discard
	// box was cancelled, which cancels closing the editor too.
	if (PrefabStage::ConsumeExitCancelled()) { m_exitAfterStop = false; }
	if (m_exitAfterStop && !m_scenes.IsPlaySessionActive() && !PrefabStage::IsActive())
	{
		m_exitAfterStop = false;
		RequestGuarded(GuardedAction::Exit);
	}

	// Scene files are edit-mode only: the world holds runtime state until Stop.
	bool const playing = m_scenes.IsPlaySessionActive();
	// ...and in Prefab Mode the world doesn't hold the scene at all.
	bool const sceneBusy = playing || m_scenes.IsSceneLocked();

	// C# hot reload: watch Assets/Scripts, compile on save, reload (after Stop if playing).
	if (Core* core = EditorManager::GetInstance().GetCore(); core && core->GetScriptRuntime().IsRunning()) {
		if (!m_scriptHotReload.IsConfigured()) {
			ScriptCompiler::Paths paths;
			paths.projectRoot = std::filesystem::path(core->GetAssets().GetAssetRoot()).parent_path();
			paths.managedDir = ScriptCompiler::ExecutableDirectory() / "Managed";
#ifdef CRUCIBLE_MONO_SDK
			paths.monoSdk = std::filesystem::path(CRUCIBLE_MONO_SDK);   // set by Editor/CMakeLists.txt
#else
			paths.monoSdk = paths.projectRoot / "Extern/mono";
#endif
#ifdef _DEBUG
			paths.debug = true;
#else
			paths.debug = false;
#endif
			m_scriptHotReload.Configure(paths);
		}
		m_scriptHotReload.Tick(core->GetScriptRuntime(), playing);

		// Play is refused while scripts have errors or are still compiling (BuildProblems).
		ScriptHotReload::Status const scripts = m_scriptHotReload.GetStatus();
		BuildProblems::SetScriptState(scripts == ScriptHotReload::Status::Compiling,
			scripts == ScriptHotReload::Status::Errors ? std::max(m_scriptHotReload.ErrorCount(), 1) : 0);
	}

	// Keyboard shortcuts. Checked here every frame, NOT inside BeginMenu("File"),
	// which only runs while the File menu is open. Skipped while any popup is up,
	// so pressing Ctrl+S while typing a name in "Save Scene As" doesn't save behind it.
	auto animator = EditorManager::GetInstance().GetPanel<AnimatorPanel>(ImguiPanels::ANIMATOR);
	if ((!animator || !animator->HasKeyboardFocus()) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
	{
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
		{
			if (PrefabStage::IsActive()) { PrefabStage::Save(); }
			else                         { SaveScene(); }
		}
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

			// Fills Resources with everything the shipped game reads (Tools/Cook/Cook.h).
			// Copies the files as saved on disk: save the scene first to include unsaved edits.
			if (ImGui::MenuItem("Cook Resources", nullptr, false, !playing)) {
				if (m_assets.GetResourceRoot().empty()) {
					CRU_USER_ERROR("Cook: there is no Resources folder next to Assets");
				}
				else {
					Cook::Report const report = Cook::Run(std::filesystem::path{ m_assets.GetAssetRoot() },
					                                      std::filesystem::path{ m_assets.GetResourceRoot() });
					for (std::string const& line : report.warnings) CRU_USER_WARN("Cook: {}", line);
					for (std::string const& line : report.missing)  CRU_USER_ERROR("Cook: missing {}", line);
					for (std::string const& line : report.errors)   CRU_USER_ERROR("Cook: {}", line);
					CRU_USER_INFO("Cook {}: {} copied, {} already up to date, {} removed", report.Ok() ? "done" : "INCOMPLETE",
					              report.copied, report.unchanged, report.removed);
				}
			}

			// Builds the Release game, cooks and zips it, in the background (ImGuiPanel_PackageGame.h).
			if (auto packager = EditorManager::GetInstance().GetPanel<PackageGamePanel>(ImguiPanels::PACKAGEGAME)) {
				if (ImGui::MenuItem(packager->IsPackaging() ? "Package Game... (packaging)" : "Package Game...", nullptr, false, !playing))
					packager->Open();
			}

			ImGui::Separator();
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
			if (ImGui::MenuItem("New Scene", "Ctrl+N", false, !sceneBusy)) {
				RequestGuarded(GuardedAction::New);
			}

			if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, !sceneBusy)) {
				OpenSceneFunc();
			}


			if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !sceneBusy)) {
				if (!m_scenes.HasPath()) m_openSaveAsPopup = true;   // never saved -> ask for a name
				else                     { SaveScene(); }
			}

			if (ImGui::MenuItem("Save Scene As...", nullptr, false, !sceneBusy))
				m_openSaveAsPopup = true;

			if (playing)
				ImGui::TextDisabled("Stop play mode to open or save scenes");
			else if (sceneBusy)
				ImGui::TextDisabled("Exit Prefab Mode to open or save scenes");

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

		// ---------------- Scripts ----------------
		if (ImGui::BeginMenu("Scripts"))
		{
			// Saved .cs files compile and reload by themselves (ScriptHotReload); these
			// items do it by hand, or pick up a DLL built in Visual Studio.
			Core* core = EditorManager::GetInstance().GetCore();
			ScriptRuntime* runtime = core && core->GetScriptRuntime().IsRunning() ? &core->GetScriptRuntime() : nullptr;
			bool const compiling = m_scriptHotReload.GetStatus() == ScriptHotReload::Status::Compiling;
			// Compile Assets/Scripts now, then reload (after Stop if playing).
			if (ImGui::MenuItem("Recompile Scripts", nullptr, false, runtime != nullptr && m_scriptHotReload.IsConfigured() && !compiling))
				m_scriptHotReload.RequestRecompile();
			// Reload whatever Crucible.Scripts.dll is in Managed/ now, e.g. after a Visual Studio build.
			if (ImGui::MenuItem("Reload Scripts", nullptr, false, runtime != nullptr && !playing && !compiling)) {
				if (runtime->ReloadScripts())
					CRU_USER_INFO("Scripts reloaded. New and changed scripts are available in Add Component.");
				else
					CRU_USER_WARN("Script reload: {}", runtime->LastError());
			}
			ImGui::Separator();
			ImGui::MenuItem("Auto Recompile on Save", nullptr, &m_scriptHotReload.autoRecompile, runtime != nullptr);
			if (!runtime)
				ImGui::TextDisabled("Needs a Mono-enabled Editor build");
			else if (std::string const status = m_scriptHotReload.StatusText(); !status.empty())
				ImGui::TextDisabled("%s", status.c_str());
			ImGui::EndMenu();
		}

		// ---------------- View ----------------
		if (ImGui::BeginMenu("View"))
		{
			if (ImGui::MenuItem("Animator")) { if (animator) animator->Show(); }
			if (auto behavior = EditorManager::GetInstance().GetPanel<BehaviorGraphPanel>(ImguiPanels::BEHAVIORGRAPH)) {
				if (ImGui::MenuItem("Behavior Graph", nullptr, behavior->IsOpen())) {
					if (behavior->IsOpen()) { behavior->SetOpen(false); } else { behavior->Show(); }
				}
			}
			if (auto states = EditorManager::GetInstance().GetPanel<StateGraphPanel>(ImguiPanels::STATEGRAPH)) {
				if (ImGui::MenuItem("State Graph", nullptr, states->IsOpen())) {
					if (states->IsOpen()) { states->SetOpen(false); } else { states->Show(); }
				}
			}
			if (auto shaders = EditorManager::GetInstance().GetPanel<ShaderGraphPanel>(ImguiPanels::SHADERGRAPH)) {
				if (ImGui::MenuItem("Shader Graph", nullptr, shaders->IsOpen())) {
					if (shaders->IsOpen()) { shaders->SetOpen(false); } else { shaders->Show(); }
				}
			}
			// The third argument is a bool* that MenuItem toggles for us (shows a tick).
			ImGui::MenuItem("ImGui Demo Window", nullptr, &m_showDemoWindow);
			ImGui::MenuItem("FPS Counter", nullptr, &m_showFps);
			if (auto fonts = EditorManager::GetInstance().GetPanel<FontImporter>(ImguiPanels::FONTIMPORTER)) {
				if (ImGui::MenuItem("Font Importer", nullptr, fonts->IsOpen())) {
					if (fonts->IsOpen()) { fonts->SetOpen(false); } else { fonts->Open(""); }
				}
			}
			if (auto textures = EditorManager::GetInstance().GetPanel<TextureImporter>(ImguiPanels::TEXTUREIMPORTER)) {
				if (ImGui::MenuItem("Texture Importer", nullptr, textures->IsOpen())) {
					if (textures->IsOpen()) { textures->SetOpen(false); } else { textures->Open(""); }
				}
			}
			ImGui::Separator();
			if (ImGui::BeginMenu("Theme"))
			{
				EditorTheme::DrawMenu(&m_showThemeEditor);
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Reset Layout"))
				ImGuiManager::ResetLayout();
			ImGui::Separator();
			// Window mode. Remembered for next launch in UserSettings.json (Core::SetFullscreen).
			if (Core* core = EditorManager::GetInstance().GetCore()) {
				if (ImGui::MenuItem("Fullscreen", "F11", core->IsFullscreen()))
					core->ToggleFullscreen();
				if (ImGui::MenuItem("Maximize Window", nullptr, core->IsWindowMaximized(), !core->IsFullscreen())) {
					if (core->IsWindowMaximized()) core->RestoreWindow(); else core->MaximizeWindow();
				}
			}
			ImGui::EndMenu();
		}

		// ---------------- Help ----------------
		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About Crucible Editor"))
				m_showAbout = true;
			ImGui::EndMenu();
		}

		// Script compile status, after the menus. Nothing is shown while up to date.
		switch (m_scriptHotReload.GetStatus()) {
		case ScriptHotReload::Status::Compiling:
		case ScriptHotReload::Status::Waiting:
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", m_scriptHotReload.StatusText().c_str());
			break;
		case ScriptHotReload::Status::ReloadPending:
			ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.3f, 1.0f), "%s", m_scriptHotReload.StatusText().c_str());
			break;
		case ScriptHotReload::Status::Errors:
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_scriptHotReload.StatusText().c_str());
			ImGui::SetItemTooltip("%s\n(all errors are in the Console)", m_scriptHotReload.FirstError().c_str());
			break;
		default:
			break;
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

	if (m_openAnimatorQuitPopup) {
		ImGui::OpenPopup("Unsaved Animator Controller");
		m_openAnimatorQuitPopup = false;
	}

	DrawOpenScenePopup();
	DrawSaveAsPopup();
	DrawUnsavedChangesPopup();
	DrawAnimatorQuitPopup();
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
	if (m_scenes.IsSceneLocked()) {
		CRU_USER_WARN("Cannot {} while in Prefab Mode - exit it first", what);
		return true;
	}
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
	// Leaving from Prefab Mode: back to the scene first, then ask about ITS changes (see Update).
	if (action == GuardedAction::Exit && PrefabStage::IsActive())
	{
		PrefabStage::RequestExit();
		m_exitAfterStop = true;
		return;
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
		// The scene is settled by now (saved, discarded, or never dirty). Unsaved Animator
		// changes get their own prompt next; otherwise close straight away.
		auto animator = EditorManager::GetInstance().GetPanel<AnimatorPanel>(ImguiPanels::ANIMATOR);
		if (animator && animator->IsUnsaved()) { m_animatorQuitError.clear(); m_openAnimatorQuitPopup = true; }
		else QuitNow();
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
		ImGui::TextDisabled("No .scene files in Assets/Scenes or its subfolders");

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
// marked unsaved, but does not block the scene from saving. Commits the Inspector's open edit
// first, so a name still being typed is part of what is saved.
void Crucible::Menu::SaveEditedMaterials()
{
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (!inspector) {
		CRU_WARN("[Menu] No Inspector panel - edited materials were not saved with the scene");
		return;
	}
	inspector->CommitPendingEdit();   // a name or value still being typed is saved too
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

// Sets GLFW's close flag for real. m_quitConfirmed first, so the check at the top of Update
// doesn't catch this flag as a new close request and cancel it.
void Crucible::Menu::QuitNow()
{
	m_quitConfirmed = true;
	if (GLFWwindow* window = MainWindow())
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}

// The second quit prompt (see RunAction): only opened on Exit, after the scene's prompt.
void Crucible::Menu::DrawAnimatorQuitPopup()
{
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Unsaved Animator Controller", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	auto animator = EditorManager::GetInstance().GetPanel<AnimatorPanel>(ImguiPanels::ANIMATOR);
	Core* core = EditorManager::GetInstance().GetCore();

	// Saved some other way meanwhile (or the panel is gone): nothing left to ask, just close.
	if (!animator || !core || !animator->IsUnsaved())
	{
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		QuitNow();
		return;
	}

	// Asset() is 0 for a controller that was never saved: it has no name and no file yet.
	uint64_t const asset = animator->Asset();
	std::string const name = asset ? core->GetResources().GetName(asset) : "New controller";
	ImGui::Text("Animator controller '%s' has unsaved changes.", name.c_str());
	ImGui::TextUnformatted("Save before closing?");

	// No file yet: Save needs a path, so ask for it here (relative to Assets, like Save As).
	if (!asset)
	{
		ImGui::SetNextItemWidth(420.0f);
		ImGui::InputText("Path", m_animatorSavePath, sizeof(m_animatorSavePath));
	}

	// A failed save (e.g. the controller changed on disk) stays here with the reason, so the
	// user can fix it, choose Don't Save, or Cancel - it never closes with the work lost.
	if (!m_animatorQuitError.empty())
		ImGui::TextColored(EditorTheme::GetPalette().warning, "%s", m_animatorQuitError.c_str());
	ImGui::Separator();

	if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)))
	{
		// SaveAs for a new controller: plain Save() would open the Animator's own Save As instead.
		bool const saved = asset ? animator->Save() : animator->SaveAs(m_animatorSavePath);
		if (saved)
		{
			ImGui::CloseCurrentPopup();
			QuitNow();
		}
		else
		{
			m_animatorQuitError = animator->LastError();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Don't Save", ImVec2(120.0f, 0.0f)))
	{
		// Nothing to discard: the Editor is closing, so the edits simply aren't written.
		ImGui::CloseCurrentPopup();
		QuitNow();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
	{
		m_animatorQuitError.clear();
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

// Every .scene under Assets/Scenes, subfolders included, as paths relative to Scenes with '/'
// (e.g. "Game/Main Menu Scene.scene"): callers put "Scenes/" in front.
std::vector<std::string> Crucible::Menu::ScanSceneFiles() const {
	std::vector<std::string> files;
	std::filesystem::path const root = m_assets.Resolve("Scenes");
	std::error_code ec;
	for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
	     !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
		if (!it->is_regular_file(ec) || it->path().extension() != ".scene")
			continue;
		std::u8string const relative = it->path().lexically_relative(root).generic_u8string();
		files.emplace_back(reinterpret_cast<char const*>(relative.data()), relative.size());
	}
	// Scenes directly in Scenes first, then each subfolder's, each group alphabetical.
	std::sort(files.begin(), files.end(), [](std::string const& a, std::string const& b) {
		bool const aNested = a.find('/') != std::string::npos;
		bool const bNested = b.find('/') != std::string::npos;
		return aNested != bNested ? !aNested : a < b;
	});
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

	// Why a release key might be a poor choice, or nullptr. The key still works; this is advice.
	char const* ReleaseKeyWarning(Crucible::KeyChord const& chord)
	{
		using Crucible::KeyCode;
		bool const bare = !chord.ctrl && !chord.shift && !chord.alt;
		if (chord.key == KeyCode::Escape)
			return "Escape is the game's pause-menu key: in Play both the menu and this would answer it.";
		if ((bare && chord.key == KeyCode::F11) || (chord.alt && !chord.ctrl && !chord.shift && chord.key == KeyCode::Enter))
			return "F11 and Alt+Enter toggle fullscreen.";
		std::uint16_t const k = static_cast<std::uint16_t>(chord.key);
		bool const typingKey = k < static_cast<std::uint16_t>(KeyCode::F1)                 // letters, number row
			|| (k >= static_cast<std::uint16_t>(KeyCode::Left) && k <= static_cast<std::uint16_t>(KeyCode::GraveAccent))   // arrows ... punctuation
			|| (k >= static_cast<std::uint16_t>(KeyCode::KP0) && k <= static_cast<std::uint16_t>(KeyCode::KPEqual));
		if (bare && typingKey)
			return "The game may use this key too (both would react). Add Shift, Ctrl or Alt.";
		return nullptr;
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
			ImGui::Checkbox("Start Fullscreen", &graphics.fullScreen);
			ImGui::SameLine();
			ImGui::Checkbox("Start Maximized", &graphics.maximized);
			ImGui::TextDisabled("Defaults only: once someone toggles F11 or maximizes, their own choice is\n"
			                    "remembered in UserSettings.json next to the .exe (delete it to reset).");

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

			// Fullscreen / maximized: one tick box overrides both, like the size.
			bool const modeOn = OverrideToggle("##OverrideMode", game.fullScreen.has_value() || game.maximized.has_value());
			if (modeOn) {
				if (!game.fullScreen) game.fullScreen = graphics.fullScreen;
				if (!game.maximized)  game.maximized  = graphics.maximized;
				ImGui::Checkbox("Start Fullscreen##Game", &*game.fullScreen);
				ImGui::SameLine();
				ImGui::Checkbox("Start Maximized##Game", &*game.maximized);
			}
			else {
				game.fullScreen.reset();
				game.maximized.reset();
				bool fs = graphics.fullScreen, mx = graphics.maximized;
				ImGui::BeginDisabled();
				ImGui::Checkbox("Start Fullscreen##Game", &fs);
				ImGui::SameLine();
				ImGui::Checkbox("Start Maximized##Game", &mx);
				ImGui::EndDisabled();
			}

			// What the game will actually start with, all overrides applied.
			EngineConfig shared = m_projectSettings;
			shared.graphics.windowTitle = m_projectWindowTitle;
			EngineConfig const result = game.ApplyTo(shared);
			ImGui::Spacing();
			ImGui::TextDisabled("The Game will start as \"%s\", %u x %u%s, opening %s.",
				result.graphics.windowTitle.c_str(), result.graphics.windowWidth, result.graphics.windowHeight,
				result.graphics.fullScreen ? " (fullscreen)" : (result.graphics.maximized ? " (maximized)" : ""),
				result.project.startupScene.empty() ? "no scene" : result.project.startupScene.c_str());

			ImGui::EndTabItem();
		}

		// ================= Editor: the editor's own settings ====================================
		if (ImGui::BeginTabItem("Editor"))
		{
			EditorSettings& editor = m_projectSettings.editor;

			ImGui::TextDisabled("Only the Editor uses these. They apply as soon as you save.");

			ImGui::SeparatorText("Play Mode");
			KeyChord chord;
			bool const valid = ParseKeyChord(editor.releaseCursorKey, chord);
			if (!valid) {
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "'%s' in the file is not a key - the editor uses %s.",
					editor.releaseCursorKey.c_str(), EditorSettings{}.releaseCursorKey.c_str());
				ParseKeyChord(EditorSettings{}.releaseCursorKey, chord);
			}

			KeyChord edited = chord;
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Release Mouse");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Frees the mouse while the game has it captured in Play.\nClick the Game view to give it back.");
			ImGui::SameLine(130.0f);
			ImGui::Checkbox("Ctrl##ReleaseKey", &edited.ctrl);
			ImGui::SameLine();
			ImGui::Checkbox("Shift##ReleaseKey", &edited.shift);
			ImGui::SameLine();
			ImGui::Checkbox("Alt##ReleaseKey", &edited.alt);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::BeginCombo("##ReleaseKeyKey", KeyCodeToString(edited.key), ImGuiComboFlags_HeightLarge)) {
				for (std::uint16_t i = 1; i < static_cast<std::uint16_t>(KeyCode::Count); ++i) {
					KeyCode const key = static_cast<KeyCode>(i);
					if (!IsChordKey(key)) continue;
					bool const selected = (key == edited.key);
					if (ImGui::Selectable(KeyCodeToString(key), selected))
						edited.key = key;
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::Button("Default##ReleaseKey"))
				ParseKeyChord(EditorSettings{}.releaseCursorKey, edited);

			if (edited != chord)
				editor.releaseCursorKey = KeyChordToString(edited);

			if (char const* warning = ReleaseKeyWarning(edited))
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", warning);
			ImGui::TextDisabled("Escape stays the game's, as in the built game. Pause and Stop always free the mouse.");

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
			// ...and the Editor tab, all of which is live.
			KeyChord releaseKey;
			if (ParseKeyChord(m_projectSettings.editor.releaseCursorKey, releaseKey))
				EditorManager::GetInstance().SetReleaseCursorKey(releaseKey);
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
