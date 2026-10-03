#pragma once
/*!
* @file			ImGuiPanel_Menu.h
* @brief		The editor's main menu bar (File / Edit / View / Help).
*
* Sample panel: shows how an EditorPanel plugs into EditorManager. Most items are
* placeholders that just log a message until the real features exist.
*/

#include <string>
#include <vector>
#include <filesystem>

#include "ImGui/ImGuiManager.h"
#include "AssetPipe/Assetmanager.h"

namespace Crucible {
	class SceneManager;

	class Menu : public EditorPanel {
	public:
		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		Menu(SceneManager& scenes, AssetManager& assets) : m_scenes(scenes), m_assets(assets) {}

		// Opens a scene exactly the way File > Open does: refused during play mode, and asks
		// first when the current scene has unsaved changes. The Asset Browser's double-click.
		void RequestOpenScene(const std::filesystem::path& file) { RequestGuarded(GuardedAction::Open, file); }

	private:
		// Helps to protect actions that the user may not want to do (e.g. New/Open when the current scene has unsaved changes).
		enum class GuardedAction { None, New, Open, Exit };

		void RequestGuarded(GuardedAction action, std::filesystem::path path = {});
		void RunAction(GuardedAction action, const std::filesystem::path& path);

		GuardedAction         m_guardedAction = GuardedAction::None;
		std::filesystem::path m_guardedPath;
		bool                  m_openUnsavedPopup = false;
		bool                  m_continueAfterSaveAs = false;   // Save As was opened from the unsaved prompt
		bool				  m_quitConfirmed = false;
		bool                  m_exitAfterStop = false;         // Exit during play mode: Stop first, then ask as usual

		// Scene files are edit-mode only. True (with a user warning) during a play session.
		bool BlockedByPlayMode(const char* what) const;

		// Save if the scene has a file, otherwise open Save As
		void SaveScene();
		void OpenSceneFunc();
		void DrawOpenScenePopup();
		void DrawSaveAsPopup();
		void SaveEditedMaterials();   // writes Inspector-edited .mat files; called on every scene save
		void DrawUnsavedChangesPopup();

		SceneManager& m_scenes;
		AssetManager& m_assets;           // Core's shared index (core.GetAssets())

		bool m_openScenePopup = false;
		bool m_openSaveAsPopup = false;
		std::vector<std::string> m_sceneFiles;
		char m_saveAsName[64] = "Untitled";

		// Add any required stuff here...
		bool m_showDemoWindow = false;	// View > ImGui Demo Window
		bool m_showFps = true;			// View > FPS Counter
		bool m_showAbout = false;		// Help > About (opened as a popup)
	};

}
