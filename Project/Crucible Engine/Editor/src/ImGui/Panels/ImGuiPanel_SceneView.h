#pragma once
/*!
* @file			ImGuiPanel_SceneView.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's Scene view: the free-camera view of the level you are editing.
*
* Owns a ViewportPanel (see Graphics/ViewportPanel.h) that holds the offscreen render target,
* and drives its camera with a FlyCameraController while the view is right-mouse-dragged -
* the familiar Unity/Unreal "hold RMB over the viewport to fly around" scheme. This view's
* camera is entirely its own: independent of the Renderer's main camera (see ImGuiPanel_GameView
* for the view that mirrors that one instead).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/ImGuiManager.h"
#include <Graphics/ViewportPanel.h>
#include <Graphics/CameraController.h>
#include <ECS/Core/ECSCore.h>   // Entity

#include <string>
#include <utility>
#include <vector>

namespace Crucible {
	class SceneManager;
	class World;
	class ResourceManager;

	class SceneView : public EditorPanel {
	public:
		enum class GizmoMode { Translate, Rotate, Scale };
		enum class GizmoSpace { World, Local };   // axes for Move / Rotate (Scale is always Local)

		SceneView(Renderer& renderer, ResourceManager& resources, SceneManager& scene, World& world)
			: m_viewport(renderer), m_scenes(scene), m_world(world), m_renderer(renderer), m_resources(resources) {}

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		// Size in pixels of the image area (what the scene render target renders at).
		ImVec2    GetViewportSize() const { return m_viewportSize; }
		// Mouse is over the image area: use this to decide when the editor camera takes input.
		bool      IsViewportHovered() const { return m_hovered; }
		bool      IsViewportFocused() const { return m_focused; }
		GizmoMode GetGizmoMode() const { return m_gizmoMode; }
		GizmoSpace GetGizmoSpace() const { return m_gizmoSpace; }
		// True while RMB-drag flying is active (WASD/QE move, mouse look, tool hotkeys suppressed).
		bool      IsFlying() const { return m_flying; }

		// Unity's "Frame Selected" (F): glide the camera so the whole selection fills the view,
		// keeping the direction it is looking in. Bound to F here and in the Hierarchy, and to
		// double-clicking a Hierarchy row. Does nothing with no selection.
		void      FrameSelected();

	private:
		void DrawToolbar();
		void UpdateEditorCamera(float dt);

		// ---- Editor-only debug visuals (Scene view only; never in the Game view or Game build) ----
		// Gizmos popup: DebugDraw category toggles, icon toggle, velocity arrow scale.
		void DrawGizmoMenu();
		// Rendering popup: renderer-wide settings (bloom, anti-aliasing, sun shadows).
		void DrawRenderingMenu();
		// Timings in the corner of the view: frame time, CPU stages, GPU passes, draw counts.
		void DrawStatsOverlay(ImVec2 viewMin);
		// Screen-space icons for lights, the skybox and mesh-less entities, drawn with ImGui over
		// the viewport image; fills m_iconHits for click picking.
		void DrawIcons(ImVec2 viewMin, ImVec2 viewSize);
		// Left click (not a drag, not flying): an icon under the cursor wins; otherwise a ray from
		// the cursor picks the nearest mesh (Renderer::Raycast); empty space clears the selection.
		// Ctrl+click adds/removes the picked entity, Shift+click adds it (multi-selection).
		void HandleClickPicking(ImVec2 viewMin, ImVec2 viewSize);
		void Select(Entity entity);

		// ---- Move / Rotate / Scale gizmo (ImGuizmo) ----
		// Drawn on the active (last selected) entity. With several selected, the drag moves /
		// rotates / scales all of them by the same amount. One whole drag = one undo step
		// (EndGizmoDrag pushes it).
		void DrawGizmo(ImVec2 viewMin, ImVec2 viewSize);
		void EndGizmoDrag();
		// Mirrors the Inspector's selection into DebugDrawSystem (the active entity's selection
		// box and light gizmos) and the renderer's outline (every selected entity).
		void SyncSelection();

		struct IconHit { Entity entity; ImVec2 pos; float depth; };
		std::vector<IconHit> m_iconHits;
		bool                 m_showIcons = true;
		bool                 m_showStats = false;

		ViewportPanel       m_viewport;
		FlyCameraController m_flyController;

		SceneManager& m_scenes;
		World&        m_world;
		Renderer&     m_renderer;
		ResourceManager& m_resources;   // meshAsset -> mesh handle, for FrameSelected's bounds

		// FrameSelected's glide (eased over kFocusDuration). m_focusT < 0 = not gliding.
		glm::vec3 m_focusFrom{ 0.0f };
		glm::vec3 m_focusTo{ 0.0f };
		float     m_focusT = -1.0f;

		ImVec2    m_viewportSize{ 0.0f, 0.0f };
		bool      m_hovered = false;
		bool      m_focused = false;
		bool      m_flying = false;
		GizmoMode m_gizmoMode = GizmoMode::Translate;
		GizmoSpace m_gizmoSpace = GizmoSpace::World;

		bool        m_gizmoDrawn = false;          // DrawGizmo called Manipulate last frame
		bool        m_gizmoWasUsing = false;
		bool        m_gizmoClaimedClick = false;   // LMB went down on the gizmo: that click is a drag, not a pick
		Entity      m_gizmoEntity = kNullEntity;  // the entity the gizmo is on (the active one)
		// Every entity the drag moves, with its snapshot from when the drag started (for undo).
		// Only top-level ones: a selected child already moves with its selected parent.
		std::vector<std::pair<Entity, std::string>> m_gizmoBefore;
		std::string m_gizmoVerb;                  // "Move" / "Rotate" / "Scale", the Edit menu label
	};

}
