/*!
* @file			EditorWidgets.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		Infinite mouse-drag helpers declared in EditorWidgets.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/EditorWidgets.h"
#include <imgui_internal.h>   // ImGui::TeleportMousePos, ImGui::FindRenderedTextEnd
#include <ImGuizmo.h>         // AxisDragFloats: the gizmo's axis colours
#include <GLFW/glfw3.h>
#include <algorithm>          // std::max

namespace {
	bool        s_wantLock = false, s_locked = false;
	double      s_restoreX = 0.0, s_restoreY = 0.0;
	GLFWwindow* s_window = nullptr;

	// A gizmo axis colour made readable as text on the dark theme. The gizmo's own blue is
	// (0, 0, 0.67) - fine as a thick handle, nearly invisible as a thin letter. Same hue, pushed
	// to full strength, then a third of the way to white.
	ImVec4 ReadableAxisColour(ImVec4 colour) {
		float const strongest = std::max({ colour.x, colour.y, colour.z });
		if (strongest > 0.0f) { colour.x /= strongest; colour.y /= strongest; colour.z /= strongest; }
		constexpr float kTowardWhite = 0.35f;
		colour.x += (1.0f - colour.x) * kTowardWhite;
		colour.y += (1.0f - colour.y) * kTowardWhite;
		colour.z += (1.0f - colour.z) * kTowardWhite;
		colour.w = 1.0f;
		return colour;
	}
}

namespace Crucible::EditorWidgets {

	void RequestMouseLock() {
		s_wantLock = true;
		if (!s_window)
			s_window = static_cast<GLFWwindow*>(ImGui::GetWindowViewport()->PlatformHandle);
	}

	// Blender-style wrap: the cursor stays visible, and when it reaches an edge of the window it
	// is moved to the opposite edge. ImGui is told about the jump through TeleportMousePos, so it
	// does not count the jump as mouse movement and the dragged value carries on smoothly.
	void InfiniteDrag() {
		if (!ImGui::IsItemActive() || !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) { return; }

		GLFWwindow* const window = static_cast<GLFWwindow*>(ImGui::GetWindowViewport()->PlatformHandle);
		if (window == nullptr) { return; }

		double x = 0.0, y = 0.0;
		int    w = 0,   h = 0;
		glfwGetCursorPos(window, &x, &y);
		glfwGetWindowSize(window, &w, &h);

		constexpr double kMargin = 2.0;   // wrap just before the edge so the cursor never gets stuck on it
		double newX = x, newY = y;
		if      (x >= w - kMargin) { newX = kMargin + 1.0; }
		else if (x <= kMargin)     { newX = w - kMargin - 1.0; }
		if      (y >= h - kMargin) { newY = kMargin + 1.0; }
		else if (y <= kMargin)     { newY = h - kMargin - 1.0; }

		if (newX == x && newY == y) { return; }

		glfwSetCursorPos(window, newX, newY);

		ImVec2 const pos = ImGui::GetIO().MousePos;
		ImGui::TeleportMousePos(ImVec2(pos.x + static_cast<float>(newX - x),
		                               pos.y + static_cast<float>(newY - y)));
	}

	void EndFrameMouseLock() {
		if (s_window && s_wantLock && !s_locked) {
			glfwGetCursorPos(s_window, &s_restoreX, &s_restoreY);
			glfwSetInputMode(s_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			if (glfwRawMouseMotionSupported())
				glfwSetInputMode(s_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
			s_locked = true;
		}
		else if (s_window && !s_wantLock && s_locked) {
			glfwSetInputMode(s_window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
			glfwSetInputMode(s_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			glfwSetCursorPos(s_window, s_restoreX, s_restoreY); // cursor reappears where the drag began
			s_locked = false;
			s_window = nullptr;
		}
		s_wantLock = false;
	}

	bool AxisDragFloats(char const* label, float* values, int count, float speed, char const* format) {
		count = std::clamp(count, 1, 4);

		static constexpr char const* kLetters[4] = { "X", "Y", "Z", "W" };
		static constexpr ImGuizmo::COLOR kAxes[3] = { ImGuizmo::DIRECTION_X, ImGuizmo::DIRECTION_Y, ImGuizmo::DIRECTION_Z };
		ImGuizmo::Style const& gizmo = ImGuizmo::GetStyle();   // read every time, so a gizmo recolour shows here too

		// The row is as wide as one ordinary ImGui item; the letters and gaps come out of it
		// and the boxes share what is left, so a row lines up with every other field.
		float const gap     = ImGui::GetStyle().ItemInnerSpacing.x;
		float const letter  = ImGui::CalcTextSize("W").x;   // the widest letter, so all boxes match
		float const boxes   = ImGui::CalcItemWidth() - static_cast<float>(count) * (letter + gap) - static_cast<float>(count - 1) * gap;
		float const boxWidth = std::max(1.0f, boxes / static_cast<float>(count));

		bool changed = false;
		ImGui::PushID(label);
		for (int i = 0; i < count; ++i) {
			if (i > 0) ImGui::SameLine(0.0f, gap);

			ImGui::AlignTextToFramePadding();   // letter on the same baseline as the number
			if (i < 3) ImGui::TextColored(ReadableAxisColour(gizmo.Colors[kAxes[i]]), "%s", kLetters[i]);
			else       ImGui::TextUnformatted(kLetters[i]);

			ImGui::SameLine(0.0f, gap);
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(boxWidth);
			changed |= ImGui::DragFloat("##axis", &values[i], speed, 0.0f, 0.0f, format);
			InfiniteDrag();                     // per box: InfiniteDrag looks at the item just drawn
			ImGui::PopID();
		}

		// The label after the boxes, as DragFloat3 does; "##" hides the rest, as in ImGui.
		char const* const labelEnd = ImGui::FindRenderedTextEnd(label);
		if (labelEnd != label) {
			ImGui::SameLine(0.0f, gap);
			ImGui::TextUnformatted(label, labelEnd);
		}
		ImGui::PopID();
		return changed;
	}
}
