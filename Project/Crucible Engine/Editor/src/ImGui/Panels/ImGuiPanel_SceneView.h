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
#include "UI/UISystem.h"        // UI::LayoutItem (world canvas picking)

#include <array>
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
		// The same glide, around any set of entities.
		void      FrameEntities(std::vector<Entity> const& entities);

		// Where this view's camera is and which way it looks (Prefab Mode puts it back on Exit).
		struct CameraView {
			glm::vec3 position{ 0.0f };
			float     yaw = 0.0f;
			float     pitch = 0.0f;
		};
		// False if the view has no camera yet (its panel has never had a size).
		bool      GetCameraView(CameraView& out);
		void      SetCameraView(CameraView const& view);   // immediate: cancels any glide

	private:
		void DrawToolbar();
		void UpdateEditorCamera(float dt);

		// ---- Editor-only debug visuals (Scene view only; never in the Game view or Game build) ----
		// Gizmos popup: DebugDraw category toggles, icon toggle, velocity arrow scale.
		void DrawGizmoMenu();
		// Rendering popup: renderer-wide settings (bloom, anti-aliasing, sun shadows).
		void DrawRenderingMenu();
		// Camera popup: fly speed, Shift boost, look sensitivity and field of view of this view's camera.
		void DrawCameraMenu();
		// Timings in the corner of the view: frame time, CPU stages, GPU passes, draw counts.
		void DrawStatsOverlay(ImVec2 viewMin);
		// Screen-space icons for lights, the skybox and mesh-less entities, drawn with ImGui over
		// the viewport image; fills m_iconHits for click picking.
		void DrawIcons(ImVec2 viewMin, ImVec2 viewSize);
		// Left click (not a drag, not flying): an icon under the cursor wins; otherwise a ray from
		// the cursor picks the nearest mesh (Renderer::Raycast); empty space clears the selection.
		// Ctrl+click adds/removes the picked entity, Shift+click adds it (multi-selection).
		void HandleClickPicking(ImVec2 viewMin, ImVec2 viewSize);
		// World text: the selected texts' boxes, outlined over the image (they have no mesh for
		// the renderer's selection outline), and the nearest text box a ray hits (click picking;
		// returns kNullEntity and leaves `distance` alone when none is nearer than it).
		void DrawWorldTextBoxes(ImVec2 viewMin, ImVec2 viewSize);
		Entity PickWorldText(const glm::vec3& origin, const glm::vec3& direction, float& distance);
		// World-space canvases: the nearest page a ray hits (nearer than `distance`, which it then
		// becomes), and on it the topmost element under the hit point - or the canvas itself on bare
		// page. Editor selection only: nothing in the game reacts.
		Entity PickWorldCanvas(const glm::vec3& origin, const glm::vec3& direction, float& distance);
		// The world corners (top-left, top-right, bottom-right, bottom-left) of a world canvas's page,
		// or of an element's rect on one, for a camera. False for anything else.
		bool WorldUICorners(Entity entity, const glm::vec3& cameraPosition, const glm::mat4& view, std::array<glm::vec3, 4>& corners);
		// The ray from the camera through the mouse cursor, in world space (toFar spans the near
		// plane to the far plane). False if the view has no size yet.
		bool MouseRay(ImVec2 viewMin, ImVec2 viewSize, glm::vec3& origin, glm::vec3& toFar);
		// Where a prefab dropped at the cursor lands: on the surface under it, else on the y = 0
		// ground plane, else 10 units in front of the camera.
		glm::vec3 DropPoint(ImVec2 viewMin, ImVec2 viewSize);
		// Models (.glb) dragged in from the Asset Browser become a new entity at the drop point.
		bool IsModelAsset(std::string const& virtualPath) const;
		void PlaceModel(std::string const& virtualPath, glm::vec3 position);
		void Select(Entity entity);

		// ---- Move / Rotate / Scale gizmo (ImGuizmo) ----
		// Drawn on the active (last selected) entity. With several selected, the drag moves /
		// rotates / scales all of them by the same amount. One whole drag = one undo step
		// (EndGizmoDrag pushes it).
		void DrawGizmo(ImVec2 viewMin, ImVec2 viewSize);
		// The gizmo on an element of a world-space canvas: in its page, along its own (turned) axes.
		// Move changes UIRect::position, Rotate UIRect::rotation (around the page's normal), Scale
		// UIRect::size (on an axis whose anchors are together). One drag = one undo step, as usual.
		void DrawWorldUIGizmo(Entity entity, ImVec2 viewMin, ImVec2 viewSize);
		void EndGizmoDrag();
		// Mirrors the Inspector's selection into DebugDrawSystem (the active entity's selection
		// box and light gizmos) and the renderer's outline (every selected entity).
		void SyncSelection();

		std::vector<UI::LayoutItem> m_canvasLayout;   // reused by PickWorldCanvas / WorldUICorners

		struct IconHit { Entity entity; ImVec2 pos; float depth; };
		std::vector<IconHit> m_iconHits;
		bool                 m_showIcons = true;
		// Gizmos > "Show hidden objects": draw what was hidden with the Hierarchy's eye anyway
		// (SceneVisibility.h). The entities stay marked hidden; this only changes what the view shows.
		bool                 m_showHidden = false;
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
		glm::vec2   m_uiGizmoStartSize{ 0.0f };    // a world UI element's UIRect::size as a Scale drag began (ImGuizmo scales from the start)
	};

}
