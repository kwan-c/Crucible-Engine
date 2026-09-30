#pragma once
/*!
* @file			ImGuiPanel_SceneView.h
* @brief		The editor's Scene view: the free-camera view of the level you are editing.
*
* Owns a ViewportPanel (see Graphics/ViewportPanel.h) that holds the offscreen render target,
* and drives its camera with a FlyCameraController while the view is right-mouse-dragged -
* the familiar Unity/Unreal "hold RMB over the viewport to fly around" scheme. This view's
* camera is entirely its own: independent of the Renderer's main camera (see ImGuiPanel_GameView
* for the view that mirrors that one instead).
*/

#include "ImGui/ImGuiManager.h"
#include <Graphics/ViewportPanel.h>
#include <Graphics/CameraController.h>

namespace Crucible {
	class SceneManager;

	class SceneView : public EditorPanel {
	public:
		enum class GizmoMode { Translate, Rotate, Scale };

		explicit SceneView(Renderer& renderer, SceneManager& scene) : m_viewport(renderer), m_scenes(scene) {}

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		// Size in pixels of the image area (what the scene render target renders at).
		ImVec2    GetViewportSize() const { return m_viewportSize; }
		// Mouse is over the image area: use this to decide when the editor camera takes input.
		bool      IsViewportHovered() const { return m_hovered; }
		bool      IsViewportFocused() const { return m_focused; }
		GizmoMode GetGizmoMode() const { return m_gizmoMode; }
		// True while RMB-drag flying is active (WASD/QE move, mouse look, tool hotkeys suppressed).
		bool      IsFlying() const { return m_flying; }

	private:
		void DrawToolbar();
		void UpdateEditorCamera(float dt);

		ViewportPanel       m_viewport;
		FlyCameraController m_flyController;

		SceneManager& m_scenes;

		ImVec2    m_viewportSize{ 0.0f, 0.0f };
		bool      m_hovered = false;
		bool      m_focused = false;
		bool      m_flying = false;
		GizmoMode m_gizmoMode = GizmoMode::Translate;
	};

}
