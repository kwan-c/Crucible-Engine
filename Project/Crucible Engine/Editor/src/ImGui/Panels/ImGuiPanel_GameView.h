#pragma once
/*!
* @file			ImGuiPanel_GameView.h
* @brief		The editor's Game view: what the player sees through the game camera.
*
* Owns a ViewportPanel (see Graphics/ViewportPanel.h) like the Scene view does, but created with
* ViewportRole::Game, so it does NOT drive its own camera: the Renderer looks through its main
* camera - the scene's active Camera entity (CameraComponent, handed over by RenderExtractSystem) -
* and draws the game's UI (Renderer::SubmitUI) on top. With no camera in the scene the image is
* black plus the UI, and the panel says "No cameras rendering".
*
* The aspect-ratio dropdown already letterboxes the image area, so GetViewportSize() is the
* resolution the game camera renders at - and Renderer::GetGameScreenSize(), what UI lays out to.
*/

#include "ImGui/ImGuiManager.h"
#include <Graphics/ViewportPanel.h>

namespace Crucible {

	class GameView : public EditorPanel {
	public:
		explicit GameView(Renderer& renderer) : m_renderer(renderer), m_viewport(renderer, ViewportRole::Game) {}

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		// Letterboxed size in pixels of the game image (respects the selected aspect ratio).
		ImVec2 GetViewportSize() const { return m_viewportSize; }
		bool   IsViewportHovered() const { return m_hovered; }

	private:
		void DrawToolbar();

		Renderer&     m_renderer;
		ViewportPanel m_viewport;

		int    m_aspectIndex = 0;   // index into the aspect table in the .cpp (0 = free aspect)
		ImVec2 m_viewportSize{ 0.0f, 0.0f };
		bool   m_hovered = false;
	};

}
