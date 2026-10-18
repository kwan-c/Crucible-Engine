#pragma once
/*!
* @file			ImGuiPanel_GameView.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's Game view: what the player sees through the game camera.
*
* Owns a ViewportPanel (see Graphics/ViewportPanel.h) like the Scene view does, but created with
* ViewportRole::Game, so it does NOT drive its own camera: the Renderer looks through its main
* camera - the scene's active Camera entity (CameraComponent, handed over by RenderExtractSystem) -
* and draws the game's UI (Renderer::SubmitUI) on top. With no camera in the scene the image is
* black plus the UI, and the panel says "No cameras rendering".
*
* The aspect-ratio dropdown already letterboxes the image area, so GetViewportSize() is the
* resolution the game camera renders at.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights ReservedW
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

		// The resolution the game camera renders at: the letterboxed image size for Free Aspect and
		// the ratios, exactly width x height for a fixed resolution.
		ImVec2 GetViewportSize() const { return m_viewportSize; }
		bool   IsViewportHovered() const { return m_hovered; }

		// Where the game image is drawn (ImGui screen pixels) and how much it is shrunk to fit
		// (1 unless a fixed resolution is bigger than the panel). Together they turn a mouse
		// position into game-screen pixels: (mouse - GetImageMin()) / GetDisplayScale().
		ImVec2 GetImageMin() const     { return m_imageMin; }
		float  GetDisplayScale() const { return m_displayScale; }

	private:
		void DrawToolbar();
		void PublishPlacement();   // Renderer::SetGameScreenPlacement with this frame's image rect, scale and hover

		Renderer&     m_renderer;
		ViewportPanel m_viewport;

		int    m_aspectIndex = 0;   // index into the aspect table in the .cpp (0 = free aspect)
		ImVec2 m_viewportSize{ 0.0f, 0.0f };
		ImVec2 m_imageMin{ 0.0f, 0.0f };   // top-left of the drawn image, ImGui screen pixels
		float  m_displayScale = 1.0f;      // drawn size / rendered size
		bool   m_hovered = false;
	};

}
