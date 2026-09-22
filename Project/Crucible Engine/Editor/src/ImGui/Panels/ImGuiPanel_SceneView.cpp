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

void Crucible::SceneView::Update() {
	// Zero padding so the (future) scene image fills the window edge to edge.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	bool const visible = ImGui::Begin(m_panelName.c_str(), nullptr, m_flags);
	ImGui::PopStyleVar();

	if (!visible) {
		// Tab hidden behind another tab, or window collapsed: nothing to render this frame.
		m_hovered = m_focused = false;
		ImGui::End();
		return;
	}

	DrawToolbar();

	ImVec2 const avail = ImGui::GetContentRegionAvail();
	ImVec2 const min = ImGui::GetCursorScreenPos();
	ImVec2 const max(min.x + avail.x, min.y + avail.y);
	m_viewportSize = avail;

	// One invisible item covering the image area, so hover / click / drop queries work on it.
	// Later: ImGui::Image(sceneTexture, avail) goes here instead of the placeholder draw below.
	ImGui::InvisibleButton("##SceneViewport", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)),
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	m_hovered = ImGui::IsItemHovered();
	m_focused = ImGui::IsWindowFocused();

	// Accept assets dragged in from the Asset Browser.
	if (ImGui::BeginDragDropTarget()) {
		if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload)) {
			char const* virtualPath = static_cast<char const*>(payload->Data);
			CRU_INFO("[Scene] Asset dropped into scene: {} (spawning not implemented yet)", virtualPath);
		}
		ImGui::EndDragDropTarget();
	}

	EditorWidgets::DrawViewportPlaceholder(min, max, "Scene View",
		"Renderer does not draw into a texture yet");

	ImGui::End();
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
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_gizmoMode = GizmoMode::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_gizmoMode = GizmoMode::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_gizmoMode = GizmoMode::Scale;
	}

	ImGui::SameLine();
	ImGui::TextDisabled("|  %d x %d", static_cast<int>(m_viewportSize.x), static_cast<int>(m_viewportSize.y));

	EditorWidgets::EndToolbarRow();
}
