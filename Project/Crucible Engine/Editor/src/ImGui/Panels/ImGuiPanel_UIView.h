#pragma once
/*!
* @file			ImGuiPanel_UIView.h
* @author		(your name)
* @date			18 October 2026
*
* @brief		The editor's UI view: a third tab next to Scene and Game for laying out the
*				game's screen-space UI on a fixed design resolution, with pan and zoom.
*
* Owns a ViewportPanel created with ViewportRole::UI, so the Renderer draws no scene into it -
* only its clear colour and the UI submitted for it (Renderer::SubmitViewportUI). The UI is built
* in canvas pixels at the chosen design resolution (1920 x 1080 by default), and pan / zoom is just
* Renderer::SetViewportUITransform: image pixel = canvas pixel * zoom + pan. Nothing is re-laid-out
* when you zoom, exactly like zooming Unity's UI in the Scene view.
*
* What works with no UI system yet: the tab, the design-resolution page, pan (middle / right drag),
* zoom at the cursor (wheel), Fit (F or the toolbar button), the cursor's canvas position.
* What is waiting on UIRect / UICanvas: drawing the real canvas (BuildCanvasContent), clicking to
* select (PickElement), dragging with undo (MoveElement). Each is one marked function in the .cpp;
* the selection, drag and undo plumbing around them is already done.
*
* SKELETON - not compiled yet. Move to Editor/src/ImGui/Panels/ and register it (see the README).
*/

#include "ImGui/ImGuiManager.h"
#include "Graphics/UITypes.h"
#include "UI/UISystem.h"   // UI::LayoutItem, the shared layout / drawing
#include <Graphics/ViewportPanel.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Crucible {

	class World;

	class UIView : public EditorPanel {
	public:
		UIView(Renderer& renderer, World& world) : m_renderer(renderer), m_world(world), m_viewport(renderer, ViewportRole::UI) {}

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		bool IsViewportHovered() const { return m_hovered; }

		/// The design resolution being edited, in canvas pixels (what a UICanvas lays out against here).
		glm::vec2 GetDesignResolution() const;

		/// ImGui screen position -> canvas pixels, and back. Valid after this frame's Update.
		glm::vec2 ScreenToCanvas(ImVec2 screen) const;
		ImVec2    CanvasToScreen(glm::vec2 canvas) const;

		/// Brings a UI element into view, centred and zoomed to a comfortable size (the Hierarchy's
		/// double-click and F). A canvas frames the whole page. Applied on the tab's next draw, so
		/// it also works while the tab is hidden behind Scene / Game.
		void FrameEntity(Entity entity) { m_frameRequest = entity; }

		/// The gizmo tool, like the Scene view's (W / E / R).
		enum class GizmoMode { Move, Rotate, Scale };

	private:
		/// Which part of the gizmo (or the element's body) is hovered / being dragged.
		enum class GizmoHandle { None, MoveX, MoveY, MoveFree, Rotate, ScaleX, ScaleY, ScaleUniform };

		/// The gizmo's origin (the pivot, in ImGui screen pixels) and the directions its X / Y
		/// handles point in for the current tool (unit length, screen pixels; yUp points up).
		struct GizmoFrame { glm::vec2 origin; glm::vec2 x; glm::vec2 yUp; };

		void DrawToolbar();
		void FitCanvas();                                  // zoom so the whole page fits, centred
		void HandlePanZoom(bool hovered, bool active);
		void HandleSelection(bool hovered, bool active);
		void DrawOverlay(ImDrawList* drawList);            // page border, selection boxes (ImGui, editor-only)
		void CountCanvases();                              // fills m_screenCanvases / m_worldCanvases
		void DrawNoCanvasMessage(ImDrawList* drawList, ImVec2 min, ImVec2 max) const;   // like the Game view's "No cameras rendering"

		void SubmitPreview();                              // page background + canvas content -> SubmitViewportUI
		void BuildCanvasContent();                         // lays out (m_layout) and draws every screen-space canvas
		void HandleFrameRequest();                         // applies FrameEntity() once m_layout is this frame's
		Entity PickElement(glm::vec2 canvasPos) const;     // topmost element under a canvas point
		UI::LayoutItem const* FindItem(Entity entity) const;   // the entity's layout this frame, or null

		// Gizmo
		GizmoFrame  MakeGizmoFrame(UI::LayoutItem const& item) const;
		GizmoHandle HitTestGizmo(UI::LayoutItem const& item, ImVec2 mouse) const;
		void        ApplyDrag(ImVec2 mouse);               // m_dragEntity's UIRect = start values + the drag so far
		void        DrawGizmo(ImDrawList* drawList, UI::LayoutItem const& item) const;

		// Little quad helper for the preview's own draw list.
		void Quad(glm::vec2 min, glm::vec2 max, uint32_t colour);

		Renderer&     m_renderer;
		World&        m_world;
		ViewportPanel m_viewport;

		// View
		int       m_resolutionIndex = 0;     // into kResolutions in the .cpp
		float     m_zoom = 1.0f;             // image pixels per canvas pixel
		glm::vec2 m_pan{ 0.0f };             // where canvas (0, 0) lands, in image pixels
		bool      m_fitPending = true;       // fit on the first frame with a real size, and after a resolution change
		bool      m_checkerboard = true;     // page background: checkerboard or flat grey
		ImVec2    m_imageMin{ 0.0f, 0.0f };  // the image's top-left, ImGui screen position
		ImVec2    m_imageSize{ 0.0f, 0.0f };
		bool      m_hovered = false;

		// This frame's canvases in the scene. Only screen-space ones are edited in this tab.
		int       m_screenCanvases = 0;
		int       m_worldCanvases = 0;

		// Gizmo tool, and the handle under the mouse (None while dragging something else).
		GizmoMode   m_gizmoMode = GizmoMode::Move;
		GizmoHandle m_hotHandle = GizmoHandle::None;

		// A drag in progress (left button): a gizmo handle, or the element's body (MoveFree).
		// Everything is computed from the values at the start of the drag plus the total mouse
		// movement, so Ctrl-snapping never drifts. The undo snapshot is taken on the first frame
		// of movement, so a plain click never pushes an empty undo step.
		Entity      m_dragEntity{};
		GizmoHandle m_dragHandle = GizmoHandle::None;
		bool        m_dragMoved = false;
		std::string m_dragBefore;
		Entity      m_contextEntity{};   // the element the right-click menu is about (null: empty page)
		ImVec2      m_dragStartMouse{ 0.0f, 0.0f };
		UIRect      m_dragStartRect{};

		Entity      m_frameRequest{};   // FrameEntity(): applied in HandleFrameRequest

		// This frame's layout of every screen-space canvas at the design resolution, in draw order.
		// The tab's own copy: it never writes UIRect::computed (that is the game screen's layout).
		std::vector<UI::LayoutItem> m_layout;

		// This frame's preview draw list (canvas pixels).
		std::vector<UIVertex>      m_vertices;
		std::vector<uint32_t>      m_indices;
		std::vector<UIDrawCommand> m_commands;
	};

}
