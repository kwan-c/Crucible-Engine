/*!
* @file			ImGuiPanel_GameView.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's Game view: what the player sees through the game camera.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_GameView.h"
#include "ImGui/EditorWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
	// Either a shape (ratio > 0: the largest image of that shape that fits the panel, rendered at
	// that on-screen size), a fixed resolution (width > 0: rendered at exactly width x height, then
	// shown scaled down to fit - like Unity's fixed-resolution entries), or neither (Free Aspect:
	// the whole panel).
	struct AspectOption { char const* name; float ratio; uint32_t width; uint32_t height; };

	constexpr AspectOption kAspects[] = {
		{ "Free Aspect",            0.0f,          0,    0 },
		{ "16:9",                   16.0f / 9.0f,  0,    0 },
		{ "16:10",                  16.0f / 10.0f, 0,    0 },
		{ "4:3",                    4.0f / 3.0f,   0,    0 },
		{ "1:1",                    1.0f,          0,    0 },
		{ "1920 x 1080 (Full HD)",  0.0f,          1920, 1080 },
		{ "1280 x 720 (HD)",        0.0f,          1280, 720 },
		{ "2560 x 1440 (QHD)",      0.0f,          2560, 1440 },
		{ "3840 x 2160 (4K)",       0.0f,          3840, 2160 },
		{ "1080 x 1920 (Portrait)", 0.0f,          1080, 1920 },
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
		m_renderer.SetGameScreenPlacement(glm::vec2(0.0f), 1.0f, false);   // hidden: the mouse is never on the game screen
		ImGui::End();
		return;
	}

	DrawToolbar();

	ImVec2 const avail = ImGui::GetContentRegionAvail();
	ImVec2 const regionMin = ImGui::GetCursorScreenPos();
	ImVec2 const regionMax(regionMin.x + avail.x, regionMin.y + avail.y);

	// Fit the selected aspect ratio inside the available area (letterbox / pillarbox).
	// renderSize: what the camera renders at (and what the game's UI lays out against).
	// size: how big that image is drawn in the panel. They differ only for fixed resolutions.
	AspectOption const& option = kAspects[m_aspectIndex];
	ImVec2 renderSize = avail;
	ImVec2 size = avail;
	m_displayScale = 1.0f;
	if (option.width > 0 && option.height > 0) {
		// Fixed resolution: exactly width x height, shown shrunk to fit the panel - never enlarged,
		// so when the panel is big enough it shows 1:1 with bars around it.
		renderSize = ImVec2(static_cast<float>(option.width), static_cast<float>(option.height));
		m_displayScale = std::min({ avail.x / renderSize.x, avail.y / renderSize.y, 1.0f });
		m_displayScale = std::max(m_displayScale, 0.0f);
		size = ImVec2(renderSize.x * m_displayScale, renderSize.y * m_displayScale);
	}
	else if (option.ratio > 0.0f && avail.x > 0.0f && avail.y > 0.0f) {
		// A shape: the largest image of that shape that fits (letterbox / pillarbox).
		size.x = avail.x;
		size.y = avail.x / option.ratio;
		if (size.y > avail.y) {
			size.y = avail.y;
			size.x = avail.y * option.ratio;
		}
		renderSize = size;
	}
	ImVec2 const min(std::floor(regionMin.x + (avail.x - size.x) * 0.5f), std::floor(regionMin.y + (avail.y - size.y) * 0.5f));
	ImVec2 const max(min.x + size.x, min.y + size.y);
	m_viewportSize = renderSize;
	m_imageMin = min;

	// The render target is the resolution the game camera renders at (see GetViewportSize()).
	uint32_t const width = static_cast<uint32_t>(std::max(renderSize.x, 0.0f));
	uint32_t const height = static_cast<uint32_t>(std::max(renderSize.y, 0.0f));
	m_viewport.Resize(width, height);

	// Not stood up yet this frame (panel collapsed, or its very first frame before the dock
	// layout has given it a real size) - there's nothing to draw yet.
	if (!m_viewport.IsValid()) {
		ImGui::GetWindowDrawList()->AddRectFilled(regionMin, regionMax, IM_COL32(0, 0, 0, 255));
		ImGui::SetCursorScreenPos(regionMin);
		ImGui::InvisibleButton("##GameViewport", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)));
		m_hovered = ImGui::IsItemHovered();
		PublishPlacement();
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
	PublishPlacement();

	ImGui::End();
}

void Crucible::GameView::PublishPlacement() {
	// Tells the renderer where the game screen is in the window, so the game's UI can turn the
	// mouse into game-screen pixels (Renderer::WindowToGameScreen). m_hovered comes from the
	// invisible button over the panel, so a window in front of the Game view correctly hides it.
	m_renderer.SetGameScreenPlacement(glm::vec2(m_imageMin.x, m_imageMin.y), m_displayScale, m_hovered);
}

void Crucible::GameView::DrawToolbar() {
	EditorWidgets::BeginToolbarRow();

	ImGui::SetNextItemWidth(110.0f);
	if (ImGui::BeginCombo("##Aspect", kAspects[m_aspectIndex].name)) {
		for (int i = 0; i < kAspectCount; ++i) {
			// A line between the shapes and the fixed resolutions.
			if (i > 0 && kAspects[i].width > 0 && kAspects[i - 1].width == 0) ImGui::Separator();
			bool const selected = (i == m_aspectIndex);
			if (ImGui::Selectable(kAspects[i].name, selected)) m_aspectIndex = i;
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	ImGui::TextDisabled("|  %d x %d", static_cast<int>(m_viewportSize.x), static_cast<int>(m_viewportSize.y));
	if (kAspects[m_aspectIndex].width > 0) {
		// Fixed resolution shown smaller than it renders: say so, like Unity's Scale readout.
		ImGui::SameLine();
		ImGui::TextDisabled("|  Scale %.2fx", m_displayScale);
	}

	EditorWidgets::EndToolbarRow();
}
