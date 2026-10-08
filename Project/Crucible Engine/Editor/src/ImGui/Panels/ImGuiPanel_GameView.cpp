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

void Crucible::GameView::Update(float) {
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

	// The render target is sized to the letterboxed image, not the full panel: that's the exact
	// resolution the game camera renders at (matches the doc comment on GetViewportSize()).
	uint32_t const width = static_cast<uint32_t>(std::max(size.x, 0.0f));
	uint32_t const height = static_cast<uint32_t>(std::max(size.y, 0.0f));
	m_viewport.Resize(width, height);

	// Not stood up yet this frame (panel collapsed, or its very first frame before the dock
	// layout has given it a real size) - there's nothing to draw yet.
	if (!m_viewport.IsValid()) {
		ImGui::GetWindowDrawList()->AddRectFilled(regionMin, regionMax, IM_COL32(0, 0, 0, 255));
		ImGui::SetCursorScreenPos(regionMin);
		ImGui::InvisibleButton("##GameViewport", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)));
		m_hovered = ImGui::IsItemHovered();
		ImGui::End();
		return;
	}

	// ---- The scene's camera ----------------------------------------------------------------
	// The Game view shows what the scene's active Camera entity sees: its viewport has the Game
	// role, so the Renderer looks through the main camera (Renderer::SetMainCamera, set every frame
	// by RenderExtractSystem) and draws the game's UI on top. With no enabled Camera component it
	// keeps rendering - black plus the UI, so menu scenes without a camera still show their UI -
	// and, like Unity, says so.
	bool const hasCamera = m_renderer.HasMainCamera();

	// Placeholder first (covers the gap before the offscreen image exists, e.g. the first frame),
	// then the real image on top of it, inside the fitted/letterboxed rectangle.
	ImGui::GetWindowDrawList()->AddRectFilled(regionMin, regionMax, IM_COL32(0, 0, 0, 255));
	EditorWidgets::DrawViewportPlaceholder(min, max, "Game View", "Waiting for the renderer...");
	ImGui::SetCursorScreenPos(min);
	m_viewport.DrawImage(size);

	if (!hasCamera) {
		char const* title = "No cameras rendering";
		char const* hint  = "Add an entity with a Camera component (Hierarchy > + > Create Camera).";
		ImVec2 const titleSize = ImGui::CalcTextSize(title);
		ImVec2 const hintSize  = ImGui::CalcTextSize(hint);
		float const line = ImGui::GetTextLineHeightWithSpacing();
		ImVec2 const centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddText(ImVec2(centre.x - titleSize.x * 0.5f, centre.y - line), ImGui::GetColorU32(ImGuiCol_Text), title);
		dl->AddText(ImVec2(centre.x - hintSize.x * 0.5f, centre.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
	}

	// Invisible item over the whole panel (not just the letterboxed rect) so hover queries work
	// the same way the Scene view's do.
	ImGui::SetCursorScreenPos(regionMin);
	ImGui::InvisibleButton("##GameViewport", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)));
	m_hovered = ImGui::IsItemHovered();

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
