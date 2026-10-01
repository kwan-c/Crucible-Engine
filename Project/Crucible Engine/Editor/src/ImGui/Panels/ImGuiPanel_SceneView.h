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
#include <ECS/Core/ECSCore.h>   // Entity

#include <vector>

namespace Crucible {
	class SceneManager;
	class World;

	class SceneView : public EditorPanel {
	public:
		enum class GizmoMode { Translate, Rotate, Scale };

		SceneView(Renderer& renderer, SceneManager& scene, World& world) : m_viewport(renderer), m_scenes(scene), m_world(world), m_renderer(renderer) {}

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

		// ---- Editor-only debug visuals (Scene view only; never in the Game view or Game build) ----
		// Gizmos popup: DebugDraw category toggles, icon toggle, velocity arrow scale.
		void DrawGizmoMenu();
		// Screen-space icons for lights, the skybox and mesh-less entities, drawn with ImGui over
		// the viewport image; fills m_iconHits for click picking.
		void DrawIcons(ImVec2 viewMin, ImVec2 viewSize);
		// Left click (not a drag, not flying): an icon under the cursor wins; otherwise a ray from
		// the cursor picks the nearest mesh (Renderer::Raycast); empty space clears the selection.
		void HandleClickPicking(ImVec2 viewMin, ImVec2 viewSize);
		void Select(Entity entity);
		// Mirrors the Inspector's selection into DebugDrawSystem (selection box, light gizmos).
		void SyncSelection();

		struct IconHit { Entity entity; ImVec2 pos; float depth; };
		std::vector<IconHit> m_iconHits;
		bool                 m_showIcons = true;

		ViewportPanel       m_viewport;
		FlyCameraController m_flyController;

		SceneManager& m_scenes;
		World&        m_world;
		Renderer&     m_renderer;

		ImVec2    m_viewportSize{ 0.0f, 0.0f };
		bool      m_hovered = false;
		bool      m_focused = false;
		bool      m_flying = false;
		GizmoMode m_gizmoMode = GizmoMode::Translate;
	};

}
