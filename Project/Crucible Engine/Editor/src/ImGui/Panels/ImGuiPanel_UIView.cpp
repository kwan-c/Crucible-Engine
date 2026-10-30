/*!
* @file			ImGuiPanel_UIView.cpp
* @author		(your name)
* @date			18 October 2026
*
* @brief		The editor's UI view. See the header.
*/

#include "ImGui/Panels/ImGuiPanel_UIView.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"
#include "ImGui/Panels/ImGuiPanel_Hierarchy.h"   // sibling order (Bring to Front, ...)
#include "ImGui/EditorWidgets.h"
#include "ECS/World.h"
#include "Undo/EntityCommands.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
	struct ResolutionOption { char const* name; float width; float height; };

	// The page you lay out on. Should match your UICanvas::referenceResolution; later the toolbar
	// could default to the selected canvas's reference resolution instead.
	constexpr ResolutionOption kResolutions[] = {
		{ "1920 x 1080 (16:9)",  1920.0f, 1080.0f },
		{ "1280 x 720 (16:9)",   1280.0f,  720.0f },
		{ "2560 x 1440 (16:9)",  2560.0f, 1440.0f },
		{ "1920 x 1200 (16:10)", 1920.0f, 1200.0f },
		{ "1024 x 768 (4:3)",    1024.0f,  768.0f },
		{ "1080 x 1920 (Portrait)", 1080.0f, 1920.0f },
	};
	constexpr int kResolutionCount = static_cast<int>(sizeof(kResolutions) / sizeof(kResolutions[0]));

	constexpr float kMinZoom = 0.05f;
	constexpr float kMaxZoom = 8.0f;
	constexpr float kCheckerCell = 32.0f;   // canvas pixels

	// Gizmo, in screen pixels (independent of zoom, like the Scene view's).
	constexpr float kGizmoLength   = 80.0f;   // move arrows and scale handles
	constexpr float kGizmoPick     = 6.0f;    // how close to a line counts as on it
	constexpr float kFreeHalf      = 8.0f;    // centre square (free move / uniform scale)
	constexpr float kScaleBoxHalf  = 6.0f;    // box at the end of a scale handle
	constexpr float kRingRadius    = 70.0f;   // rotate ring

	// Ctrl-snap steps, like the Scene view's (0.5 units / 15 degrees / 0.1).
	constexpr float kMoveSnap   = 10.0f;      // reference pixels
	constexpr float kRotateSnap = 15.0f;      // degrees
	constexpr float kScaleSnap  = 0.1f;       // factor

	constexpr ImU32 kGizmoX    = IM_COL32(230, 70, 70, 255);
	constexpr ImU32 kGizmoY    = IM_COL32(110, 200, 90, 255);
	constexpr ImU32 kGizmoFree = IM_COL32(80, 140, 255, 255);
	constexpr ImU32 kGizmoHot  = IM_COL32(255, 220, 0, 255);

	float DistanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
		glm::vec2 const ab = b - a;
		float const t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1.0e-6f), 0.0f, 1.0f);
		return glm::length(p - (a + ab * t));
	}

	// Screen angle in degrees, counter-clockwise from "right" (screen y points down, hence -y).
	float ScreenAngle(glm::vec2 v) { return glm::degrees(std::atan2(-v.y, v.x)); }

	float SnapTo(float value, float step) { return std::round(value / step) * step; }

	ImVec2 ToImVec(glm::vec2 v) { return ImVec2(v.x, v.y); }

	constexpr uint32_t kCheckerLight = Crucible::PackUIColour(92, 92, 92);
	constexpr uint32_t kCheckerDark  = Crucible::PackUIColour(72, 72, 72);
	constexpr uint32_t kPageFlat     = Crucible::PackUIColour(80, 80, 80);
}

void Crucible::UIView::Init() {
	m_panelName = "UI Editor";

	// Same dock node as the Scene and Game views, so it becomes their third tab.
	m_docked = true;
	m_dockArea = DockArea::Center;

	m_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	// Outside the page. Opaque on purpose: the page background is drawn INTO the UI image (see
	// SubmitPreview) instead of by ImGui behind a transparent image, so semi-transparent UI is
	// blended by UIPass exactly as in the game. (Drawing a transparent UI image over an ImGui
	// checkerboard would darken soft edges: the image's colour is already multiplied by alpha.)
	m_viewport.SetClearColour(glm::vec4(0.11f, 0.11f, 0.12f, 1.0f));
}

glm::vec2 Crucible::UIView::GetDesignResolution() const {
	return { kResolutions[m_resolutionIndex].width, kResolutions[m_resolutionIndex].height };
}

glm::vec2 Crucible::UIView::ScreenToCanvas(ImVec2 screen) const {
	return (glm::vec2(screen.x - m_imageMin.x, screen.y - m_imageMin.y) - m_pan) / m_zoom;
}

ImVec2 Crucible::UIView::CanvasToScreen(glm::vec2 canvas) const {
	glm::vec2 const p = canvas * m_zoom + m_pan;
	return ImVec2(m_imageMin.x + p.x, m_imageMin.y + p.y);
}

void Crucible::UIView::Update(float) {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	bool const visible = ImGui::Begin(m_panelName.c_str(), nullptr, m_flags);
	ImGui::PopStyleVar();

	// Hidden tab: stop rendering into it (SetActive is cheap to call every frame).
	m_viewport.SetActive(visible);
	if (!visible) {
		m_hovered = false;
		ImGui::End();
		return;
	}

	DrawToolbar();

	ImVec2 const avail = ImGui::GetContentRegionAvail();
	ImVec2 const regionMin = ImGui::GetCursorScreenPos();
	ImVec2 const regionMax(regionMin.x + avail.x, regionMin.y + avail.y);
	ImVec2 const buttonSize(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f));
	m_imageMin = regionMin;
	m_imageSize = avail;

	// Unlike the Game view, the image fills the whole panel: the page's size on screen is set by
	// the zoom, not by letterboxing. One viewport pixel = one ImGui unit, like the other views.
	m_viewport.Resize(static_cast<uint32_t>(std::max(avail.x, 0.0f)), static_cast<uint32_t>(std::max(avail.y, 0.0f)));

	if (!m_viewport.IsValid()) {
		ImGui::GetWindowDrawList()->AddRectFilled(regionMin, regionMax, IM_COL32(0, 0, 0, 255));
		ImGui::SetCursorScreenPos(regionMin);
		ImGui::InvisibleButton("##UIViewport", buttonSize);
		m_hovered = ImGui::IsItemHovered();
		ImGui::End();
		return;
	}

	if (m_fitPending) { FitCanvas(); }

	CountCanvases();

	// This frame's canvas, and how canvas pixels map onto the image.
	SubmitPreview();
	HandleFrameRequest();   // needs this frame's layout; changes zoom / pan before they are used
	m_viewport.SetUITransform(glm::vec2(m_zoom), m_pan);

	ImGui::SetCursorScreenPos(regionMin);
	m_viewport.DrawImage(avail);

	// One invisible item over the whole image takes every button, so panning and dragging keep
	// working when the mouse leaves the panel mid-drag (IsItemActive stays true until release).
	ImGui::SetCursorScreenPos(regionMin);
	ImGui::InvisibleButton("##UIViewport", buttonSize,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	m_hovered = ImGui::IsItemHovered();
	bool const active = ImGui::IsItemActive();

	HandlePanZoom(m_hovered, active);
	HandleSelection(m_hovered, active);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(regionMin, regionMax, true);
	DrawOverlay(drawList);
	if (m_screenCanvases == 0) { DrawNoCanvasMessage(drawList, regionMin, regionMax); }
	drawList->PopClipRect();

	ImGui::End();
}

void Crucible::UIView::DrawToolbar() {
	EditorWidgets::BeginToolbarRow();

	// The gizmo tool, same buttons and keys as the Scene view.
	using EditorWidgets::ToolbarToggle;
	if (ToolbarToggle("Move", m_gizmoMode == GizmoMode::Move))     m_gizmoMode = GizmoMode::Move;
	ImGui::SameLine();
	if (ToolbarToggle("Rotate", m_gizmoMode == GizmoMode::Rotate)) m_gizmoMode = GizmoMode::Rotate;
	ImGui::SameLine();
	if (ToolbarToggle("Scale", m_gizmoMode == GizmoMode::Scale))   m_gizmoMode = GizmoMode::Scale;
	if (ImGui::IsItemHovered()) { ImGui::SetTooltip("W / E / R. Hold Ctrl while dragging to snap (10 px, 15 degrees, 0.1)."); }

	// W / E / R / F while this tab is focused and no text box is active (the Scene view's rule).
	// Not mid-drag, so a tool can't change under the mouse.
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !m_dragEntity.IsValid()) {
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_gizmoMode = GizmoMode::Move;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_gizmoMode = GizmoMode::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_gizmoMode = GizmoMode::Scale;
		if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			// The Scene view's F: frame the selection - here the selected UI element, else the page.
			auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
			Entity const selected = inspector ? inspector->GetSelectedEntity() : kNullEntity;
			if (selected.IsValid()) { FrameEntity(selected); } else { m_fitPending = true; }
		}
		// Ctrl+] / Ctrl+[ (+Shift): draw order, the Hierarchy's keys (later sibling = on top).
		Hierarchy::OrderMove move{};
		if (Hierarchy::OrderShortcutPressed(move)) {
			if (auto hierarchy = EditorManager::GetInstance().GetPanel<Hierarchy>(ImguiPanels::HIERARCHY)) { hierarchy->ReorderSelection(move); }
		}
	}

	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(170.0f);
	if (ImGui::BeginCombo("##Resolution", kResolutions[m_resolutionIndex].name)) {
		for (int i = 0; i < kResolutionCount; ++i) {
			bool const selected = (i == m_resolutionIndex);
			if (ImGui::Selectable(kResolutions[i].name, selected) && !selected) {
				m_resolutionIndex = i;
				m_fitPending = true;
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Button("Fit")) { m_fitPending = true; }
	ImGui::SameLine();
	if (ImGui::Button("1:1")) {
		// Actual pixels, keeping the page's centre where it is.
		glm::vec2 const centre = ScreenToCanvas(ImVec2(m_imageMin.x + m_imageSize.x * 0.5f, m_imageMin.y + m_imageSize.y * 0.5f));
		m_zoom = 1.0f;
		m_pan = glm::vec2(m_imageSize.x, m_imageSize.y) * 0.5f - centre;
	}
	ImGui::SameLine();
	ImGui::Checkbox("Checkerboard", &m_checkerboard);

	ImGui::SameLine();
	ImGui::TextDisabled("|  %.0f%%", m_zoom * 100.0f);
	if (m_hovered) {
		glm::vec2 const c = ScreenToCanvas(ImGui::GetIO().MousePos);
		ImGui::SameLine();
		ImGui::TextDisabled("|  %.0f, %.0f", c.x, c.y);
	}

	EditorWidgets::EndToolbarRow();
}

void Crucible::UIView::FitCanvas() {
	if (m_imageSize.x <= 0.0f || m_imageSize.y <= 0.0f) { return; }   // no size yet: try again next frame
	glm::vec2 const page = GetDesignResolution();
	glm::vec2 const area(m_imageSize.x, m_imageSize.y);
	m_zoom = std::clamp(std::min(area.x / page.x, area.y / page.y) * 0.9f, kMinZoom, kMaxZoom);   // 10% margin
	m_pan = (area - page * m_zoom) * 0.5f;
	m_fitPending = false;
}

void Crucible::UIView::HandlePanZoom(bool hovered, bool active) {
	ImGuiIO& io = ImGui::GetIO();

	// Wheel: zoom about the cursor, so the canvas point under it stays under it.
	if (hovered && io.MouseWheel != 0.0f) {
		glm::vec2 const local(io.MousePos.x - m_imageMin.x, io.MousePos.y - m_imageMin.y);
		glm::vec2 const canvasPoint = (local - m_pan) / m_zoom;
		m_zoom = std::clamp(m_zoom * std::pow(1.1f, io.MouseWheel), kMinZoom, kMaxZoom);
		m_pan = local - canvasPoint * m_zoom;
	}

	// Middle or right drag: pan.
	if (active && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right))) {
		m_pan += glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
	}
}

void Crucible::UIView::HandleSelection(bool hovered, bool active) {
	ImGuiIO& io = ImGui::GetIO();
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	Entity const selected = inspector ? inspector->GetSelectedEntity() : kNullEntity;
	UI::LayoutItem const* selectedItem = selected.IsValid() ? FindItem(selected) : nullptr;

	// Which gizmo handle is under the mouse (frozen while a drag is running).
	if (!m_dragEntity.IsValid()) {
		m_hotHandle = (hovered && selectedItem) ? HitTestGizmo(*selectedItem, io.MousePos) : GizmoHandle::None;
	}

	auto beginDrag = [&](Entity entity, GizmoHandle handle) {
		UIRect const* rect = m_world.Read<UIRect>(entity);
		if (!rect) { return; }
		m_dragEntity = entity;
		m_dragHandle = handle;
		m_dragMoved = false;
		m_dragStartMouse = io.MousePos;
		m_dragStartRect = *rect;
	};

	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		if (m_hotHandle != GizmoHandle::None) {
			// On a gizmo handle: drag it. The selection stays as it is.
			beginDrag(selected, m_hotHandle);
		}
		else {
			// Same rules as SceneView::Select - Ctrl toggles, Shift adds, and with either held a
			// click on empty page keeps the selection. A plain click selects just what was hit.
			Entity const hit = PickElement(ScreenToCanvas(io.MousePos));
			if (inspector) {
				if (io.KeyCtrl)       { if (hit.IsValid()) { inspector->ToggleSelected(hit); } }
				else if (io.KeyShift) { if (hit.IsValid()) { inspector->AddToSelection(hit); } }
				else                  { inspector->SetSelectedEntity(hit); }
			}
			// A plain click on an element can also drag it by its body (free move, any tool).
			if (hit.IsValid() && !io.KeyCtrl && !io.KeyShift) { beginDrag(hit, GizmoHandle::MoveFree); }
		}
	}

	// Right-click (without dragging - a right drag pans): select the element under the mouse and
	// open its menu - draw order for now, the same as the Hierarchy's Order menu.
	if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !m_dragEntity.IsValid()) {
		bool const dragged = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] > io.MouseDragThreshold * io.MouseDragThreshold;
		if (!dragged) {
			m_contextEntity = PickElement(ScreenToCanvas(io.MousePos));
			if (m_contextEntity.IsValid() && inspector && !inspector->IsSelected(m_contextEntity)) { inspector->SetSelectedEntity(m_contextEntity); }
			ImGui::OpenPopup("##UIElementMenu");
		}
	}
	if (ImGui::BeginPopup("##UIElementMenu")) {
		if (!m_contextEntity.IsValid() || !m_world.IsAlive(m_contextEntity)) {
			ImGui::TextDisabled("No UI element here");
		} else {
			ImGui::TextDisabled("Draw order");
			bool const orderable = m_world.GetParent(m_contextEntity).IsValid();
			Hierarchy::OrderMove move{};
			if (Hierarchy::DrawOrderMenuItems(move, orderable)) {
				if (auto hierarchy = EditorManager::GetInstance().GetPanel<Hierarchy>(ImguiPanels::HIERARCHY)) { hierarchy->ReorderSelection(move); }
			}
		}
		ImGui::EndPopup();
	}

	// Dragging. Snapshot before the first movement, so a click alone pushes no undo step.
	if (m_dragEntity.IsValid() && active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_world.IsAlive(m_dragEntity)) {
		bool const moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
		// Pressing / releasing Ctrl mid-drag re-applies at once, so snapping shows without a wiggle.
		bool const ctrlChanged = ImGui::IsKeyPressed(ImGuiKey_LeftCtrl, false) || ImGui::IsKeyReleased(ImGuiKey_LeftCtrl) ||
		                         ImGui::IsKeyPressed(ImGuiKey_RightCtrl, false) || ImGui::IsKeyReleased(ImGuiKey_RightCtrl);
		if (moved || (m_dragMoved && ctrlChanged)) {
			if (!m_dragMoved) {
				m_dragBefore = EntityState::Capture(m_world, m_dragEntity);
				m_dragMoved = true;
			}
			ApplyDrag(io.MousePos);
		}
		// What the drag is doing, next to the mouse.
		if (m_dragMoved) {
			if (UIRect const* rect = m_world.Read<UIRect>(m_dragEntity)) {
				switch (m_dragHandle) {
					case GizmoHandle::Rotate:
						ImGui::SetTooltip("Rotation %.1f", rect->rotation); break;
					case GizmoHandle::ScaleX: case GizmoHandle::ScaleY: case GizmoHandle::ScaleUniform:
						ImGui::SetTooltip("Size %.0f x %.0f", rect->size.x, rect->size.y); break;
					default:
						ImGui::SetTooltip("Position %.0f, %.0f", rect->position.x,   // y as the Inspector shows it (+y up, display only)
							EditorManager::GetInstance().GetUIPositionYUp() ? -rect->position.y : rect->position.y); break;
				}
			}
		}
	}

	// Release: one undo step for the whole drag (the Scene view's gizmo does the same).
	if (m_dragEntity.IsValid() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		if (m_dragMoved && m_world.IsAlive(m_dragEntity)) {
			char const* verb = "Move UI Element";
			if (m_dragHandle == GizmoHandle::Rotate) { verb = "Rotate UI Element"; }
			if (m_dragHandle == GizmoHandle::ScaleX || m_dragHandle == GizmoHandle::ScaleY || m_dragHandle == GizmoHandle::ScaleUniform) { verb = "Scale UI Element"; }

			UndoHistory& history = EditorManager::GetInstance().GetHistory();
			std::string after = EntityState::Capture(m_world, m_dragEntity);
			if (after != m_dragBefore) {
				history.Push(std::make_unique<EntityStateCommand>(history.KeyOf(m_dragEntity), std::move(m_dragBefore), std::move(after), verb));
			}
		}
		m_dragEntity = {};
		m_dragHandle = GizmoHandle::None;
		m_dragMoved = false;
		m_dragBefore.clear();
	}
}

// ------------------------------------------------------------------------------------------------ //
//  GIZMO
// ------------------------------------------------------------------------------------------------ //

Crucible::UIView::GizmoFrame Crucible::UIView::MakeGizmoFrame(UI::LayoutItem const& item) const {
	ImVec2 const origin = CanvasToScreen(item.ToScreen(item.pivot));
	GizmoFrame frame{ glm::vec2(origin.x, origin.y), glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, -1.0f) };

	// Move uses the screen's axes; Scale uses the element's own (it resizes along them).
	// Rotate has no axes, only the ring.
	if (m_gizmoMode == GizmoMode::Scale) {
		glm::mat2 const linear(item.toScreen);
		glm::vec2 const x = linear * glm::vec2(1.0f, 0.0f);
		glm::vec2 const down = linear * glm::vec2(0.0f, 1.0f);
		if (glm::dot(x, x) > 1.0e-12f)       { frame.x = glm::normalize(x); }
		if (glm::dot(down, down) > 1.0e-12f) { frame.yUp = -glm::normalize(down); }
	}
	return frame;
}

Crucible::UIView::GizmoHandle Crucible::UIView::HitTestGizmo(UI::LayoutItem const& item, ImVec2 mouse) const {
	GizmoFrame const f = MakeGizmoFrame(item);
	glm::vec2 const m(mouse.x, mouse.y);
	glm::vec2 const fromOrigin = m - f.origin;

	switch (m_gizmoMode) {
		case GizmoMode::Move:
			if (std::abs(fromOrigin.x) <= kFreeHalf + 2.0f && std::abs(fromOrigin.y) <= kFreeHalf + 2.0f) { return GizmoHandle::MoveFree; }
			if (DistanceToSegment(m, f.origin, f.origin + f.x * (kGizmoLength + 12.0f)) <= kGizmoPick)   { return GizmoHandle::MoveX; }
			if (DistanceToSegment(m, f.origin, f.origin + f.yUp * (kGizmoLength + 12.0f)) <= kGizmoPick) { return GizmoHandle::MoveY; }
			break;
		case GizmoMode::Rotate:
			if (std::abs(glm::length(fromOrigin) - kRingRadius) <= kGizmoPick) { return GizmoHandle::Rotate; }
			break;
		case GizmoMode::Scale:
			if (std::abs(fromOrigin.x) <= kFreeHalf + 2.0f && std::abs(fromOrigin.y) <= kFreeHalf + 2.0f) { return GizmoHandle::ScaleUniform; }
			if (glm::length(m - (f.origin + f.x * kGizmoLength)) <= kScaleBoxHalf + 4.0f ||
			    DistanceToSegment(m, f.origin, f.origin + f.x * kGizmoLength) <= kGizmoPick)   { return GizmoHandle::ScaleX; }
			if (glm::length(m - (f.origin + f.yUp * kGizmoLength)) <= kScaleBoxHalf + 4.0f ||
			    DistanceToSegment(m, f.origin, f.origin + f.yUp * kGizmoLength) <= kGizmoPick) { return GizmoHandle::ScaleY; }
			break;
	}
	return GizmoHandle::None;
}

void Crucible::UIView::ApplyDrag(ImVec2 mouse) {
	UI::LayoutItem const* item = FindItem(m_dragEntity);
	UIRect* rect = m_world.Write<UIRect>(m_dragEntity);
	if (!item || !rect) { return; }

	bool const snap = ImGui::GetIO().KeyCtrl;
	glm::vec2 const drag(mouse.x - m_dragStartMouse.x, mouse.y - m_dragStartMouse.y);   // screen pixels
	UIRect const& start = m_dragStartRect;

	switch (m_dragHandle) {
		case GizmoHandle::MoveX:
		case GizmoHandle::MoveY:
		case GizmoHandle::MoveFree: {
			glm::vec2 screenDelta = drag;
			if (m_dragHandle == GizmoHandle::MoveX) { screenDelta.y = 0.0f; }
			if (m_dragHandle == GizmoHandle::MoveY) { screenDelta.x = 0.0f; }
			// Screen pixels -> canvas pixels -> the parent's (possibly turned) space -> reference pixels.
			glm::vec2 const canvasDelta = screenDelta / m_zoom;
			glm::vec2 const local = glm::inverse(glm::mat2(item->parentToScreen)) * canvasDelta / std::max(item->scale, 1.0e-6f);
			glm::vec2 position = start.position + local;
			if (snap) {
				// Only the coordinates this drag changes, so an X drag can't nudge Y onto the grid.
				if (local.x != 0.0f) { position.x = SnapTo(position.x, kMoveSnap); }
				if (local.y != 0.0f) { position.y = SnapTo(position.y, kMoveSnap); }
			}
			rect->position = position;
			break;
		}
		case GizmoHandle::Rotate: {
			glm::vec2 const origin = MakeGizmoFrame(*item).origin;   // the pivot doesn't move while turning
			glm::vec2 const startVec(m_dragStartMouse.x - origin.x, m_dragStartMouse.y - origin.y);
			glm::vec2 const nowVec(mouse.x - origin.x, mouse.y - origin.y);
			if (glm::dot(startVec, startVec) < 1.0f || glm::dot(nowVec, nowVec) < 1.0f) { break; }   // on the pivot: no angle
			float delta = ScreenAngle(nowVec) - ScreenAngle(startVec);
			delta = std::remainder(delta, 360.0f);   // -180..180: crossing "left" doesn't jump a full turn
			float rotation = start.rotation + delta;
			if (snap) { rotation = SnapTo(rotation, kRotateSnap); }
			rect->rotation = rotation;
			break;
		}
		case GizmoHandle::ScaleX:
		case GizmoHandle::ScaleY:
		case GizmoHandle::ScaleUniform: {
			// Dragging a handle by its own length doubles the size; back to the centre shrinks it to 0.
			GizmoFrame const f = MakeGizmoFrame(*item);
			float along = drag.x;   // uniform: horizontal drag, like the Scene view's centre box
			if (m_dragHandle == GizmoHandle::ScaleX) { along = glm::dot(drag, f.x); }
			if (m_dragHandle == GizmoHandle::ScaleY) { along = glm::dot(drag, f.yUp); }
			float factor = 1.0f + along / kGizmoLength;
			if (snap) { factor = SnapTo(factor, kScaleSnap); }
			factor = std::max(factor, 0.0f);
			glm::vec2 size = start.size;
			if (m_dragHandle != GizmoHandle::ScaleY) { size.x = start.size.x * factor; }
			if (m_dragHandle != GizmoHandle::ScaleX) { size.y = start.size.y * factor; }
			rect->size = size;
			break;
		}
		case GizmoHandle::None:
			break;
	}
}

void Crucible::UIView::DrawGizmo(ImDrawList* drawList, UI::LayoutItem const& item) const {
	GizmoFrame const f = MakeGizmoFrame(item);
	auto colour = [&](GizmoHandle handle, ImU32 base) {
		bool const lit = m_dragEntity.IsValid() ? m_dragHandle == handle : m_hotHandle == handle;
		return lit ? kGizmoHot : base;
	};
	auto arrow = [&](glm::vec2 dir, ImU32 col) {
		glm::vec2 const end = f.origin + dir * kGizmoLength;
		glm::vec2 const tip = end + dir * 12.0f;
		glm::vec2 const side(-dir.y, dir.x);
		drawList->AddLine(ToImVec(f.origin), ToImVec(end), col, 3.0f);
		drawList->AddTriangleFilled(ToImVec(tip), ToImVec(end + side * 6.0f), ToImVec(end - side * 6.0f), col);
	};
	auto box = [&](glm::vec2 centre, float half, ImU32 col) {
		drawList->AddRectFilled(ImVec2(centre.x - half, centre.y - half), ImVec2(centre.x + half, centre.y + half), col);
	};

	switch (m_gizmoMode) {
		case GizmoMode::Move: {
			arrow(f.x, colour(GizmoHandle::MoveX, kGizmoX));
			arrow(f.yUp, colour(GizmoHandle::MoveY, kGizmoY));
			ImU32 const free = colour(GizmoHandle::MoveFree, kGizmoFree);
			box(f.origin, kFreeHalf, (free & 0x00FFFFFFu) | 0x60000000u);
			drawList->AddRect(ImVec2(f.origin.x - kFreeHalf, f.origin.y - kFreeHalf), ImVec2(f.origin.x + kFreeHalf, f.origin.y + kFreeHalf), free, 0.0f, 0, 1.5f);
			break;
		}
		case GizmoMode::Rotate: {
			ImU32 const ring = colour(GizmoHandle::Rotate, kGizmoFree);
			drawList->AddCircle(ToImVec(f.origin), kRingRadius, ring, 64, ring == kGizmoHot ? 3.0f : 2.0f);
			// Which way the element faces: its own X axis, out to the ring.
			glm::mat2 const linear(item.toScreen);
			glm::vec2 facing = linear * glm::vec2(1.0f, 0.0f);
			if (glm::dot(facing, facing) > 1.0e-12f) {
				facing = glm::normalize(facing);
				drawList->AddLine(ToImVec(f.origin), ToImVec(f.origin + facing * kRingRadius), kGizmoX, 2.0f);
			}
			drawList->AddCircleFilled(ToImVec(f.origin), 3.0f, ring);
			break;
		}
		case GizmoMode::Scale: {
			ImU32 const cx = colour(GizmoHandle::ScaleX, kGizmoX);
			ImU32 const cy = colour(GizmoHandle::ScaleY, kGizmoY);
			drawList->AddLine(ToImVec(f.origin), ToImVec(f.origin + f.x * kGizmoLength), cx, 3.0f);
			drawList->AddLine(ToImVec(f.origin), ToImVec(f.origin + f.yUp * kGizmoLength), cy, 3.0f);
			box(f.origin + f.x * kGizmoLength, kScaleBoxHalf, cx);
			box(f.origin + f.yUp * kGizmoLength, kScaleBoxHalf, cy);
			box(f.origin, kFreeHalf, colour(GizmoHandle::ScaleUniform, kGizmoFree));
			break;
		}
	}
}

void Crucible::UIView::DrawOverlay(ImDrawList* drawList) {
	// The page's edge: everything outside it is off-screen in the game.
	ImVec2 const pageMin = CanvasToScreen(glm::vec2(0.0f));
	ImVec2 const pageMax = CanvasToScreen(GetDesignResolution());
	drawList->AddRect(pageMin, pageMax, IM_COL32(255, 255, 255, 110));

	// Selected elements. Editor-only lines, so plain ImGui on top of the image is enough.
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (!inspector) { return; }
	Entity const active = inspector->GetSelectedEntity();
	UI::LayoutItem const* item = active.IsValid() ? FindItem(active) : nullptr;
	if (item) {
		// The element's real (possibly turned) outline, then the tool's gizmo on its pivot.
		auto const corners = item->Corners();
		ImVec2 points[4];
		for (int i = 0; i < 4; ++i) { points[i] = CanvasToScreen(corners[i]); }
		drawList->AddPolyline(points, 4, IM_COL32(255, 140, 0, 255), ImDrawFlags_Closed, 2.0f);
		DrawGizmo(drawList, *item);
		// FUTURE: anchor markers (Unity's little triangles) at the parent rect positions given by
		// anchorMin / anchorMax.
	}
}

void Crucible::UIView::CountCanvases() {
	m_screenCanvases = 0;
	m_worldCanvases = 0;
	m_world.CreateQuery<const UICanvas>().ForEach([&](Entity, const UICanvas& canvas) {
		if (canvas.renderMode == UICanvas::RenderMode::ScreenSpace) { ++m_screenCanvases; }
		else                                                         { ++m_worldCanvases; }
	});
}

void Crucible::UIView::DrawNoCanvasMessage(ImDrawList* drawList, ImVec2 min, ImVec2 max) const {
	// Same wording and layout as the Game view's "No cameras rendering", plus a backdrop: the Game
	// view's message sits on black, this one sits on the checkerboard.
	bool const onlyWorld = m_worldCanvases > 0;
	char const* title = onlyWorld ? "No Screen Space Canvas found" : "No Canvas found";
	char const* hint  = onlyWorld
		? "This tab edits Screen Space canvases. Set a UICanvas's Render Mode to ScreenSpace to edit it here."
		: "Add one with Hierarchy > + > UI > Create Canvas, then put UI elements under it.";

	ImVec2 const titleSize = ImGui::CalcTextSize(title);
	ImVec2 const hintSize  = ImGui::CalcTextSize(hint);
	float const line = ImGui::GetTextLineHeightWithSpacing();
	ImVec2 const centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

	float const padding = 12.0f;
	float const width = std::max(titleSize.x, hintSize.x);
	drawList->AddRectFilled(ImVec2(centre.x - width * 0.5f - padding, centre.y - line - padding),
	                        ImVec2(centre.x + width * 0.5f + padding, centre.y + hintSize.y + padding),
	                        IM_COL32(20, 20, 22, 220), 6.0f);
	drawList->AddText(ImVec2(centre.x - titleSize.x * 0.5f, centre.y - line), ImGui::GetColorU32(ImGuiCol_Text), title);
	drawList->AddText(ImVec2(centre.x - hintSize.x * 0.5f, centre.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
}

// ------------------------------------------------------------------------------------------------ //
//  PREVIEW GEOMETRY
// ------------------------------------------------------------------------------------------------ //

void Crucible::UIView::Quad(glm::vec2 min, glm::vec2 max, uint32_t colour) {
	uint32_t const base = static_cast<uint32_t>(m_vertices.size());
	m_vertices.push_back({ { min.x, min.y }, { 0.0f, 0.0f }, colour });
	m_vertices.push_back({ { max.x, min.y }, { 1.0f, 0.0f }, colour });
	m_vertices.push_back({ { max.x, max.y }, { 1.0f, 1.0f }, colour });
	m_vertices.push_back({ { min.x, max.y }, { 0.0f, 1.0f }, colour });

	uint32_t const first = static_cast<uint32_t>(m_indices.size());
	for (uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u }) { m_indices.push_back(base + i); }

	// Untextured and unclipped, so every quad joins one draw command.
	if (!m_commands.empty() && m_commands.back().firstIndex + m_commands.back().indexCount == first) {
		m_commands.back().indexCount += 6;
		return;
	}
	m_commands.push_back({ .texture = {}, .mode = UIDrawMode::Image, .firstIndex = first, .indexCount = 6 });
}

void Crucible::UIView::SubmitPreview() {
	m_vertices.clear();
	m_indices.clear();
	m_commands.clear();

	// The page background, in canvas pixels (zoom and pan are applied by the renderer).
	glm::vec2 const page = GetDesignResolution();
	if (m_checkerboard) {
		bool rowDark = false;
		for (float y = 0.0f; y < page.y; y += kCheckerCell, rowDark = !rowDark) {
			bool dark = rowDark;
			for (float x = 0.0f; x < page.x; x += kCheckerCell, dark = !dark) {
				Quad({ x, y }, { std::min(x + kCheckerCell, page.x), std::min(y + kCheckerCell, page.y) }, dark ? kCheckerDark : kCheckerLight);
			}
		}
	}
	else {
		Quad(glm::vec2(0.0f), page, kPageFlat);
	}

	BuildCanvasContent();

	m_renderer.SubmitViewportUI(m_viewport.GetHandle(), UIDrawList{ .vertices = m_vertices, .indices = m_indices, .commands = m_commands });
}

// ------------------------------------------------------------------------------------------------ //
//  THE CANVAS: drawing it, and finding / moving its elements (layout from UI/UISystem.h)
// ------------------------------------------------------------------------------------------------ //

void Crucible::UIView::BuildCanvasContent() {
	// The same layout and drawing UIRenderSystem uses for the Game view, against the design
	// resolution instead of the game screen - which is what keeps this tab and the Game view alike.
	// writeComputed = false: UIRect::computed stays the game screen's numbers.
	UI::LayoutScreenCanvases(m_world, GetDesignResolution(), m_layout, false);
	UI::AppendDrawList(m_world, m_layout, EditorManager::GetInstance().GetResources(), m_vertices, m_indices, m_commands);
}

void Crucible::UIView::HandleFrameRequest() {
	if (!m_frameRequest.IsValid()) { return; }
	Entity const entity = m_frameRequest;
	m_frameRequest = {};

	// A canvas (or anything not on the page): show the whole page.
	UI::LayoutItem const* item = FindItem(entity);
	if (!item || m_imageSize.x <= 0.0f || m_imageSize.y <= 0.0f) {
		m_fitPending = true;
		FitCanvas();
		return;
	}

	// The element's upright box fills about 40% of the view, centred.
	glm::vec4 const b = item->Bounds();
	glm::vec2 const size(std::max(b.z - b.x, 1.0f), std::max(b.w - b.y, 1.0f));
	glm::vec2 const area(m_imageSize.x, m_imageSize.y);
	m_zoom = std::clamp(std::min(area.x / (size.x * 2.5f), area.y / (size.y * 2.5f)), kMinZoom, kMaxZoom);
	glm::vec2 const centre((b.x + b.z) * 0.5f, (b.y + b.w) * 0.5f);
	m_pan = area * 0.5f - centre * m_zoom;
}

Crucible::Entity Crucible::UIView::PickElement(glm::vec2 canvasPos) const {
	// Topmost element under the point: last drawn = on top, so walk the draw order backwards.
	// Turned elements are tested in their own space. Unlike the game's hit test, raycastTarget is
	// ignored: designers need to select purely decorative images too.
	for (auto it = m_layout.rbegin(); it != m_layout.rend(); ++it) {
		if (it->Contains(canvasPos)) { return it->entity; }
	}
	return kNullEntity;
}

Crucible::UI::LayoutItem const* Crucible::UIView::FindItem(Entity entity) const {
	for (UI::LayoutItem const& item : m_layout) {
		if (item.entity == entity) { return &item; }
	}
	return nullptr;
}
