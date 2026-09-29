/*!
* @file			ImGuiPanel_SceneView.cpp
* @brief		The editor's Scene view: the free-camera view of the level you are editing.
*/

#include "ImGui/Panels/ImGuiPanel_SceneView.h"
#include "ImGui/EditorWidgets.h"
#include "Diagnostics/Logger.h"

#include <algorithm>

void Crucible::SceneView::Init() {
	m_panelName = "Scene";

	// Centre of the layout, tabbed with the Game view. Scene starts as the visible tab.
	m_docked = true;
	m_dockArea = DockArea::Center;
	m_makeFocus = true;

	m_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void Crucible::SceneView::Update(float dt) {
	// Zero padding so the scene image fills the window edge to edge.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	bool const visible = ImGui::Begin(m_panelName.c_str(), nullptr, m_flags);
	ImGui::PopStyleVar();

	if (!visible) {
		// Tab hidden behind another tab, or window collapsed: nothing to render this frame.
		m_hovered = m_focused = false;
		m_flying = false;
		ImGui::End();
		return;
	}

	DrawToolbar();

	ImVec2 const avail = ImGui::GetContentRegionAvail();
	ImVec2 const min = ImGui::GetCursorScreenPos();
	ImVec2 const max(min.x + avail.x, min.y + avail.y);
	m_viewportSize = avail;

	uint32_t const width = static_cast<uint32_t>(std::max(avail.x, 0.0f));
	uint32_t const height = static_cast<uint32_t>(std::max(avail.y, 0.0f));
	m_viewport.Resize(width, height);

	// Placeholder first (also covers the gap on the very first frame or two, before the
	// Renderer has actually created the offscreen image), then the real image on top of it.
	EditorWidgets::DrawViewportPlaceholder(min, max, "Scene View", "Waiting for the renderer...");
	ImGui::SetCursorScreenPos(min);
	m_viewport.DrawImage(avail);

	// One invisible item covering the image area, so hover / click / drop queries work on it.
	ImGui::SetCursorScreenPos(min);
	ImGui::InvisibleButton("##SceneViewport", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)),
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	m_hovered = ImGui::IsItemHovered();
	m_focused = ImGui::IsWindowFocused();

	// Fly-camera latch: starts the moment RMB goes down while the cursor is over the viewport,
	// and keeps going - even once the drag carries the cursor outside the panel's rect, which
	// happens constantly with a big mouse-look swing - until RMB is released. Deliberately not
	// ImGui::IsItemActive(): that goes true for whichever registered button pressed the item
	// first, so it can't tell a right-drag apart from a left-drag on its own.
	bool const rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	if (!rightMouseDown) {
		m_flying = false;
	} else if (m_hovered && !m_flying) {
		m_flying = true;
	}

	// Accept assets dragged in from the Asset Browser.
	if (ImGui::BeginDragDropTarget()) {
		if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload)) {
			char const* virtualPath = static_cast<char const*>(payload->Data);
			CRU_INFO("[Scene] Asset dropped into scene: {} (spawning not implemented yet)", virtualPath);
		}
		ImGui::EndDragDropTarget();
	}

	UpdateEditorCamera(dt);

	ImGui::End();
}

void Crucible::SceneView::UpdateEditorCamera(float dt) {
	if (!m_viewport.IsValid()) { return; }   // not stood up yet (panel was 0-sized this frame)

	CameraInput input{};
	input.active = m_flying;

	if (m_flying) {
		// While flying, hide the OS cursor and read raw mouse delta / wheel / WASDQE + Shift
		// straight from ImGui - the engine's own Input system only tracks keys today (see
		// Engine/src/Input/InputSystem.h) and isn't attached/updated by Core yet, and this is
		// editor-only UI input anyway, so there's no reason to route it through the gameplay
		// input system.
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);

		ImGuiIO const& io = ImGui::GetIO();
		input.look = { io.MouseDelta.x, io.MouseDelta.y };
		input.zoom = io.MouseWheel;
		input.boost = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);

		glm::vec3 move{ 0.0f };
		if (ImGui::IsKeyDown(ImGuiKey_W)) move.z += 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_S)) move.z -= 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_D)) move.x += 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_A)) move.x -= 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_E)) move.y += 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1.0f;
		input.move = move;
	}

	m_flyController.Update(m_viewport.GetCamera(), input, dt);
}

void Crucible::SceneView::DrawToolbar() {
	EditorWidgets::BeginToolbarRow();

	using EditorWidgets::ToolbarToggle;
	if (ToolbarToggle("Move", m_gizmoMode == GizmoMode::Translate))  m_gizmoMode = GizmoMode::Translate;
	ImGui::SameLine();
	if (ToolbarToggle("Rotate", m_gizmoMode == GizmoMode::Rotate))   m_gizmoMode = GizmoMode::Rotate;
	ImGui::SameLine();
	if (ToolbarToggle("Scale", m_gizmoMode == GizmoMode::Scale))     m_gizmoMode = GizmoMode::Scale;

	// Unity-style W / E / R shortcuts while the Scene view is focused (and no text box is active).
	// Suppressed while flying: W/E are also the fly-camera's forward/up-move keys, and R would be
	// a very unwelcome surprise mid-flight.
	if (!m_flying && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_gizmoMode = GizmoMode::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_gizmoMode = GizmoMode::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_gizmoMode = GizmoMode::Scale;
	}

	ImGui::SameLine();
	ImGui::TextDisabled("|  %d x %d", static_cast<int>(m_viewportSize.x), static_cast<int>(m_viewportSize.y));

	if (m_flying) {
		ImGui::SameLine();
		ImGui::TextDisabled("|  Flying (RMB + WASD/QE, Shift to boost)");
	}

	EditorWidgets::EndToolbarRow();
}
