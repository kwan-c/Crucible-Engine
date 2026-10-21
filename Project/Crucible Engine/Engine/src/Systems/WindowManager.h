#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>

namespace Crucible {

    /*
        Manager for application window.
    */
    class WindowManager {

    public:
        WindowManager(int w, int h, const std::string& title);
        void ShutDown() { glfwDestroyWindow(m_pWindow); glfwTerminate(); }

        // Remove copy constructors.
        WindowManager(const WindowManager&) = delete;
        WindowManager& operator=(const WindowManager&) = delete;

        bool Init(const std::string& title, int width, int height);

        void PollEvents();
        bool ShouldClose() const;
        // Asks the window to close, exactly as if its X was clicked: Core::Run's loop ends after
        // this frame and shuts down normally.
        void RequestClose();

        GLFWwindow* GetNativeHandle() const { return m_pWindow; }
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }

        // Keeps the window's client area at `width`:`height` (16:9, 4:3, ...) however the user
        // drags its edges, and resizes it once now to fit: the width is kept, the height
        // follows. With the shape fixed, the game shows the same framing on every machine.
        // Ignored if either number is not positive. Windowed mode only: a fullscreen window
        // always has the monitor's shape.
        void LockAspectRatio(int width, int height);
        // Lets the window be resized to any shape again.
        void UnlockAspectRatio();
        bool  IsAspectRatioLocked() const { return m_aspectWidth > 0; }
        // Width / height of the locked shape, or 0 when not locked.
        float GetLockedAspectRatio() const { return IsAspectRatioLocked() ? static_cast<float>(m_aspectWidth) / static_cast<float>(m_aspectHeight) : 0.0f; }

        bool m_framebufferResized = false;

    private:
        GLFWwindow* m_pWindow = nullptr;

        int m_width = 1;
        int m_height = 1;

        int m_aspectWidth  = 0;   // 0 = not locked
        int m_aspectHeight = 0;

        static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
    };

}