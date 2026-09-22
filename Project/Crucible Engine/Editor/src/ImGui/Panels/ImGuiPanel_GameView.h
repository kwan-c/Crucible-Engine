#pragma once
/*!
* @file			ImGuiPanel_GameView.h
* @brief		The editor's Game view: what the player sees through the game camera.
*
* Like the Scene view, this is a placeholder until the renderer can output to a
* texture. The aspect-ratio dropdown already letterboxes the image area, so
* GetViewportSize() is the resolution the game camera should render at.
*/

#include "ImGui/ImGuiManager.h"

namespace Crucible {

	class GameView : public EditorPanel {
	public:
		// REQUIRED Overrides
		void Init() override;
		void Update() override;

		// Letterboxed size in pixels of the game image (respects the selected aspect ratio).
		ImVec2 GetViewportSize() const { return m_viewportSize; }
		bool   IsViewportHovered() const { return m_hovered; }

	private:
		void DrawToolbar();

		int    m_aspectIndex = 0;   // index into the aspect table in the .cpp (0 = free aspect)
		ImVec2 m_viewportSize{ 0.0f, 0.0f };
		bool   m_hovered = false;
	};

}
