#pragma once
/*!
* @file			EditorWidgets.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		Small drawing helpers shared by several editor panels.
*
* Header-only on purpose: these are a few lines each and are used by the
* Scene / Game views and the Asset Browser.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include <imgui.h>

namespace Crucible::EditorWidgets {

	// Drag-and-drop payload type used when an asset is dragged out of the Asset Browser.
	// The payload data is the asset's virtual path as a null-terminated string,
	// e.g. "Textures/player.png". Any panel can accept it with:
	//		if (ImGui::BeginDragDropTarget()) {
	//			if (auto* p = ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload)) {
	//				char const* virtualPath = static_cast<char const*>(p->Data);
	//			}
	//			ImGui::EndDragDropTarget();
	//		}
	inline constexpr char const* kAssetPayload = "CRUCIBLE_ASSET";

	// Drag-and-drop payload type used when an entity row is dragged out of the Hierarchy.
	// The payload data is the dragged Entity, by value (copy it out with std::memcpy). When the
	// dragged row is part of a multi-selection, the drag stands for the whole selection.
	// The Asset Browser accepts it to create prefabs (Prefabs/PrefabAuthoring.h).
	inline constexpr char const* kEntityPayload = "CRUCIBLE_ENTITY";

	// A small toolbar button that stays highlighted while 'active' is true.
	inline bool ToolbarToggle(char const* label, bool active)
	{
		if (active)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		bool const pressed = ImGui::SmallButton(label);
		if (active)
			ImGui::PopStyleColor();
		return pressed;
	}

	// Fills [min, max] with a dark background, a faint grid and two centred lines of text.
	// Stand-in for a render target until the renderer can draw the scene into a texture.
	inline void DrawViewportPlaceholder(ImVec2 min, ImVec2 max, char const* title, char const* subtitle)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(min, max, IM_COL32(30, 30, 32, 255));

		constexpr float step = 32.0f;
		ImU32 const gridColor = IM_COL32(255, 255, 255, 12);
		for (float x = min.x; x < max.x; x += step) dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), gridColor);
		for (float y = min.y; y < max.y; y += step) dl->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), gridColor);

		ImVec2 const centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
		ImVec2 const titleSize = ImGui::CalcTextSize(title);
		ImVec2 const subSize = ImGui::CalcTextSize(subtitle);
		float const line = ImGui::GetTextLineHeightWithSpacing();

		dl->AddText(ImVec2(centre.x - titleSize.x * 0.5f, centre.y - line), ImGui::GetColorU32(ImGuiCol_Text), title);
		dl->AddText(ImVec2(centre.x - subSize.x * 0.5f, centre.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), subtitle);
	}

	// Moves the cursor in by a small margin so a toolbar row does not touch the window edge
	// (the viewport panels use zero window padding so the image can fill the window).
	inline void BeginToolbarRow()
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
	}

	inline void EndToolbarRow()
	{
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
	}



	// Infinite mouse drag (cursor hidden and unbounded). Defined in EditorWidgets.cpp so the
	// lock state is shared by every panel rather than copied into each one.

	// Call while something wants a hidden, unbounded cursor this frame (fly camera).
	void RequestMouseLock();

	// Call right after any DragFloat / DragFloat3 / DragInt etc. While dragging, the cursor
	// wraps from one edge of the window to the opposite one so the drag never runs out.
	void InfiniteDrag();

	// Call once per frame, after all panels have drawn (ImGuiManager::Render does this).
	void EndFrameMouseLock();
}
