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
        // `fullscreen` / `maximized` pick the mode the window opens in (EngineConfig.json
        // "graphics": { "fullScreen", "maximized" }). `w` x `h` is always the windowed size, the
        // one the window goes back to when it leaves fullscreen or is un-maximized.
        WindowManager(int w, int h, const std::string& title, bool fullscreen = false, bool maximized = false);
        void ShutDown() { glfwDestroyWindow(m_pWindow); glfwTerminate(); }

        // Remove copy constructors.
        WindowManager(const WindowManager&) = delete;
        WindowManager& operator=(const WindowManager&) = delete;

        bool Init(const std::string& title, int width, int height, bool fullscreen = false, bool maximized = false);

        void PollEvents();
        bool ShouldClose() const;
        // Asks the window to close, exactly as if its X was clicked: Core::Run's loop ends after
        // this frame and shuts down normally.
        void RequestClose();

        GLFWwindow* GetNativeHandle() const { return m_pWindow; }
        // Current size of the window's client area, in screen coordinates (kept up to date on resize).
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }

        // ---- Fullscreen ------------------------------------------------------------------------ //
        //
        // Fullscreen covers the whole monitor the window is mostly on, at that monitor's current
        // resolution and refresh rate (no video-mode switch, so it's quick and alt-tab friendly).
        // Leaving it puts the window back exactly where it was: same position, same size, and
        // maximized again if it was maximized. The swapchain is rebuilt through the usual
        // framebuffer-resize path, nothing else needs to know.
        void SetFullscreen(bool fullscreen);
        void ToggleFullscreen() { SetFullscreen(!m_fullscreen); }
        bool IsFullscreen() const { return m_fullscreen; }

        // ---- Maximize -------------------------------------------------------------------------- //
        //
        // Maximize fills the monitor's work area (the taskbar stays visible) and keeps the title
        // bar. Also tracks the title bar's own maximize button. While fullscreen, these act on
        // the windowed state the window returns to.
        void Maximize();
        void Restore();             // un-maximize (does not leave fullscreen)
        void ToggleMaximize() { if (IsMaximized()) Restore(); else Maximize(); }
        bool IsMaximized() const { return m_maximized; }

        // The window's size when it is neither fullscreen nor maximized - what to save as
        // "windowWidth" / "windowHeight".
        int GetWindowedWidth() const { return m_windowedW; }
        int GetWindowedHeight() const { return m_windowedH; }

        // Keeps the window's client area at `width`:`height` (16:9, 4:3, ...) however the user
        // drags its edges, and resizes it once now to fit: the width is kept, the height
        // follows. With the shape fixed, the game shows the same framing on every machine.
        // Ignored if either number is not positive. Windowed mode only: a fullscreen or
        // maximized window always has the monitor's shape (the lock applies again when it is
        // restored).
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

        bool m_fullscreen = false;
        bool m_maximized  = false;    // also remembered while fullscreen / minimized
        bool m_refitPending = false;  // un-maximized from the title bar: re-fit to the aspect lock in PollEvents

        // Last windowed (not fullscreen, not maximized, not minimized) position and size:
        // where SetFullscreen(false) / Restore() put the window back.
        int m_windowedX = 100, m_windowedY = 100;
        int m_windowedW = 1,   m_windowedH = 1;

        // The monitor the window's centre is on (primary monitor if none).
        GLFWmonitor* FindCurrentMonitor() const;
        // Re-fits the window to the locked aspect ratio (no-op while fullscreen / maximized).
        void FitToAspectRatio();

        static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
        static void WindowSizeCallback(GLFWwindow* window, int width, int height);
        static void WindowPosCallback(GLFWwindow* window, int x, int y);
        static void WindowMaximizeCallback(GLFWwindow* window, int maximized);
    };

}