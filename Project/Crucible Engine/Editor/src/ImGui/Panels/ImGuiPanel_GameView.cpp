/*!
* @file			ImGuiPanel_GameView.cpp
* @brief		The editor's Game view: what the player sees through the game camera.
*/

#include "ImGui/Panels/ImGuiPanel_GameView.h"
#include "ImGui/EditorWidgets.h"

#include <algorithm>

namespace {
	struct AspectOption { char const* name; float ratio; };   // ratio 0 = use the whole window

	constexpr AspectOption kAspects[] = {
		{ "Free Aspect", 0.0f },
		{ "16:9",        16.0f / 9.0f },
		{ "16:10",       16.0f / 10.0f },
		{ "4:3",         4.0f / 3.0f },
		{ "1:1",         1.0f },
	};
	constexpr int kAspectCount = static_cast<int>(sizeof(kAspects) / sizeof(kAspects[0]));
}

void Crucible::GameView::Init() {
	m_panelName = "Game";

	// Same dock node as the Scene view, so the two appear as tabs.
	m_docked = true;
	m_dockArea = DockArea::Center;

	m_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void Crucible::GameView::Update() {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	bool const visible = ImGui::Begin(m_panelName.c_str(), nullptr, m_flags);
	ImGui::PopStyleVar();

	if (!visible) {
		m_hovered = false;
		ImGui::End();
		return;
	}

	DrawToolbar();

	ImVec2 const avail = ImGui::GetContentRegionAvail();
	ImVec2 const regionMin = ImGui::GetCursorScreenPos();
	ImVec2 const regionMax(regionMin.x + avail.x, regionMin.y + avail.y);

	// Fit the selected aspect ratio inside the available area (letterbox / pillarbox).
	ImVec2 size = avail;
	float const ratio = kAspects[m_aspectIndex].ratio;
	if (ratio > 0.0f && avail.x > 0.0f && avail.y > 0.0f) {
		size.x = avail.x;
		size.y = avail.x / ratio;
		if (size.y > avail.y) {
			size.y = avail.y;
			size.x = avail.y * ratio;
		}
	}
	ImVec2 const min(regionMin.x + (avail.x - size.x) * 0.5f, regionMin.y + (avail.y - size.y) * 0.5f);
	ImVec2 const max(min.x + size.x, min.y + size.y);
	m_viewportSize = size;

	ImGui::InvisibleButton("##GameViewport", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)));
	m_hovered = ImGui::IsItemHovered();

	// Black bars, then the game image (placeholder for now) in the fitted rectangle.
	// Later: ImGui::SetCursorScreenPos(min); ImGui::Image(gameTexture, size);
	ImGui::GetWindowDrawList()->AddRectFilled(regionMin, regionMax, IM_COL32(0, 0, 0, 255));
	EditorWidgets::DrawViewportPlaceholder(min, max, "Game View", "Game camera output goes here");

	ImGui::End();
}

void Crucible::GameView::DrawToolbar() {
	EditorWidgets::BeginToolbarRow();

	ImGui::SetNextItemWidth(110.0f);
	if (ImGui::BeginCombo("##Aspect", kAspects[m_aspectIndex].name)) {
		for (int i = 0; i < kAspectCount; ++i) {
			bool const selected = (i == m_aspectIndex);
			if (ImGui::Selectable(kAspects[i].name, selected)) m_aspectIndex = i;
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	ImGui::TextDisabled("|  %d x %d", static_cast<int>(m_viewportSize.x), static_cast<int>(m_viewportSize.y));

	EditorWidgets::EndToolbarRow();
}
