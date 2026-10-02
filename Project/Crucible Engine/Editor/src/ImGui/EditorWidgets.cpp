/*!
* @file			EditorWidgets.cpp
* @brief		Infinite mouse-drag helpers declared in EditorWidgets.h.
*/

#include "ImGui/EditorWidgets.h"
#include <imgui_internal.h>   // ImGui::TeleportMousePos
#include <GLFW/glfw3.h>

namespace {
	bool        s_wantLock = false, s_locked = false;
	double      s_restoreX = 0.0, s_restoreY = 0.0;
	GLFWwindow* s_window = nullptr;
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
}
