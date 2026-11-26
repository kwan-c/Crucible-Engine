#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>
#include <vector>

namespace Crucible {

    // One size a monitor can show fullscreen (GLFW's video modes, deduplicated).
    struct WindowResolution {
        int width       = 0;
        int height      = 0;
        int refreshRate = 0;    // Hz; the highest the monitor offers at this size
    };

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

        // `fullscreenWidth` x `fullscreenHeight` @ `fullscreenRefresh` is the resolution fullscreen
        // uses (see SetFullscreenResolution); 0 = the monitor's own (borderless). Set before the
        // window opens fullscreen, so it starts at that resolution.
        WindowManager(int w, int h, const std::string& title, bool fullscreen, bool maximized,
                      int fullscreenWidth, int fullscreenHeight, int fullscreenRefresh);

        // ---- Fullscreen ------------------------------------------------------------------------ //
        //
        // Fullscreen covers the whole monitor the window is mostly on. By default at that
        // monitor's current resolution and refresh rate ("borderless": no video-mode switch, so
        // it's quick and alt-tab friendly - a normal window with no title bar, placed over the
        // whole monitor, so other windows can go in front of it). With SetFullscreenResolution it switches the monitor
        // to another resolution instead ("exclusive": the screen blinks on the switch, and the
        // window minimizes on alt-tab so the desktop gets its own resolution back).
        // Leaving it puts the window back exactly where it was: same position, same size, and
        // maximized again if it was maximized. The swapchain is rebuilt through the usual
        // framebuffer-resize path, nothing else needs to know.
        void SetFullscreen(bool fullscreen);
        void ToggleFullscreen() { SetFullscreen(!m_fullscreen); }
        bool IsFullscreen() const { return m_fullscreen; }

        // The resolution fullscreen uses: one of GetResolutions() (refreshRate 0 = the highest at
        // that size), or 0 x 0 for the monitor's own (borderless). Applied at once if fullscreen
        // now. Returns false (and changes nothing) for a size the monitor does not offer.
        bool SetFullscreenResolution(int width, int height, int refreshRate = 0);
        // 0 when borderless (the monitor's own resolution).
        int  GetFullscreenWidth() const { return m_fullscreenW; }
        int  GetFullscreenHeight() const { return m_fullscreenH; }
        int  GetFullscreenRefreshRate() const { return m_fullscreenRefresh; }
        bool IsExclusiveFullscreen() const { return m_fullscreenW > 0; }

        // The sizes the window's monitor can show fullscreen, smallest first, one per size
        // (the highest refresh rate at each). For an options menu's resolution list.
        std::vector<WindowResolution> GetResolutions() const;

        // ---- Windowed size --------------------------------------------------------------------- //
        //
        // Sets the client area for windowed mode (1280 x 720 ...). Kept inside the monitor's work
        // area; with an aspect lock the width is kept and the height follows. Applied now when
        // windowed; while maximized or fullscreen it is remembered and used when the window comes
        // back to normal.
        void SetWindowedSize(int width, int height);

        // ---- Maximize -------------------------------------------------------------------------- //
        //
        // Maximize fills the monitor's work area (the taskbar stays visible) and keeps the title
        // bar. Also tracks the title bar's own maximize button. While fullscreen, these act on
        // the windowed state the window returns to.
        void Maximize();
        void Restore();             // un-maximize (does not leave fullscreen)
        void ToggleMaximize() { if (IsMaximized()) Restore(); else Maximize(); }
        bool IsMaximized() const { return m_maximized; }

        // Minimized to the taskbar (or a 0x0 framebuffer): nothing can be seen, so Core skips
        // drawing. Unfocused: another app has the keyboard; Core drops to backgroundFPS.
        bool IsMinimized() const;
        bool IsFocused() const;

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

        // SetWindowedSize while maximized: the size to apply on Restore (0 = none). Separate from
        // m_windowedW/H because restoring fires a size event with the OS's old normal size.
        int m_pendingW = 0, m_pendingH = 0;

        // Fullscreen resolution; 0 = the monitor's own (borderless).
        int m_fullscreenW = 0, m_fullscreenH = 0, m_fullscreenRefresh = 0;

        // The monitor the window's centre is on (primary monitor if none).
        GLFWmonitor* FindCurrentMonitor() const;
        // The video mode fullscreen should use on `monitor`: the chosen resolution, or the
        // monitor's current mode. Fills `out` and returns false if there is none.
        bool PickFullscreenMode(GLFWmonitor* monitor, GLFWvidmode& out) const;
        // Puts the window into fullscreen on `monitor`, in the current kind: exclusive (a GLFW
        // monitor window at the chosen video mode) or borderless (an undecorated, NOT monitor
        // window covering the monitor - GLFW keeps monitor windows always-on-top on Windows,
        // so a "borderless" monitor window would stay in front after alt-tab). Also switches
        // between the two while fullscreen. False if the monitor has no usable mode.
        bool ApplyFullscreen(GLFWmonitor* monitor);
        // Borderless fullscreen: topmost while focused (so the taskbar cannot stay on top of it),
        // a normal window otherwise. Windows only; no-op in windowed / exclusive fullscreen.
        void UpdateBorderlessTopmost();
        // Applies the pending windowed size (after Restore / the title bar's un-maximize).
        void ApplyPendingSize();
        // Re-fits the window to the locked aspect ratio (no-op while fullscreen / maximized).
        void FitToAspectRatio();

        static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
        static void WindowSizeCallback(GLFWwindow* window, int width, int height);
        static void WindowPosCallback(GLFWwindow* window, int x, int y);
        static void WindowMaximizeCallback(GLFWwindow* window, int maximized);
    };

}