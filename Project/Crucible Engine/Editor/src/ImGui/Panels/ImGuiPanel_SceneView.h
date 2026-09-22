#pragma once
/*!
* @file			ImGuiPanel_SceneView.h
* @brief		The editor's Scene view: the free-camera view of the level you are editing.
*
* For now this reserves the space in the layout and draws a placeholder. Once the
* renderer can draw into an offscreen image, replace the placeholder with
* ImGui::Image(textureId, GetViewportSize()) and resize the render target whenever
* GetViewportSize() changes.
*/

#include "ImGui/ImGuiManager.h"

namespace Crucible {

	class SceneView : public EditorPanel {
	public:
		enum class GizmoMode { Translate, Rotate, Scale };

		// REQUIRED Overrides
		void Init() override;
		void Update() override;

		// Size in pixels of the image area (what the scene render target should be).
		ImVec2    GetViewportSize() const { return m_viewportSize; }
		// Mouse is over the image area: use this to decide when the editor camera takes input.
		bool      IsViewportHovered() const { return m_hovered; }
		bool      IsViewportFocused() const { return m_focused; }
		GizmoMode GetGizmoMode() const { return m_gizmoMode; }

	private:
		void DrawToolbar();

		ImVec2    m_viewportSize{ 0.0f, 0.0f };
		bool      m_hovered = false;
		bool      m_focused = false;
		GizmoMode m_gizmoMode = GizmoMode::Translate;
	};

}
