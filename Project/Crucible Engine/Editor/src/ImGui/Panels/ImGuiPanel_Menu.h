#pragma once
/*!
* @file			ImGuiPanel_Menu.h
* @brief		The editor's main menu bar (File / Edit / View / Help).
*
* Sample panel: shows how an EditorPanel plugs into EditorManager. Most items are
* placeholders that just log a message until the real features exist.
*/

#include "ImGui/ImGuiManager.h"

namespace Crucible {

	class Menu : public EditorPanel {
	public:
		// REQUIRED Overrides
		void Init() override;
		void Update() override;

	private:
		// Add any required stuff here...
		bool m_showDemoWindow = false;	// View > ImGui Demo Window
		bool m_showFps = true;			// View > FPS Counter
		bool m_showAbout = false;		// Help > About (opened as a popup)
	};

}
