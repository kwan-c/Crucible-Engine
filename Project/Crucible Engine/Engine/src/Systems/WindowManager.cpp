#include "WindowManager.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

namespace Crucible {

    WindowManager::WindowManager(int w, int h, const std::string& title, bool fullscreen, bool maximized) {
        if (!Init(title, w, h, fullscreen, maximized)) {
            throw std::runtime_error("[WindowManager] Initialization failed.");
        }
    }

    WindowManager::WindowManager(int w, int h, const std::string& title, bool fullscreen, bool maximized,
                                 int fullscreenWidth, int fullscreenHeight, int fullscreenRefresh) {
        // Only stored here: GLFW is not up yet, so SetFullscreen checks the monitor offers it
        // (and falls back to borderless if it does not).
        if (fullscreenWidth > 0 && fullscreenHeight > 0) {
            m_fullscreenW       = fullscreenWidth;
            m_fullscreenH       = fullscreenHeight;
            m_fullscreenRefresh = std::max(fullscreenRefresh, 0);
        }
        if (!Init(title, w, h, fullscreen, maximized)) {
            throw std::runtime_error("[WindowManager] Initialization failed.");
        }
    }

    namespace {
        // `monitor`'s video mode at width x height: that refresh rate exactly, or (refresh 0)
        // the highest one. nullptr if the monitor has no such mode.
        const GLFWvidmode* FindMode(GLFWmonitor* monitor, int width, int height, int refreshRate) {
            if (!monitor) { return nullptr; }
            int count = 0;
            const GLFWvidmode* modes = glfwGetVideoModes(monitor, &count);
            const GLFWvidmode* best = nullptr;
            for (int i = 0; i < count; ++i) {
                const GLFWvidmode& mode = modes[i];
                if (mode.width != width || mode.height != height) { continue; }
                if (refreshRate > 0) {
                    if (mode.refreshRate == refreshRate) { return &mode; }
                }
                else if (!best || mode.refreshRate > best->refreshRate) {
                    best = &mode;
                }
            }
            return best;
        }
    }

    bool WindowManager::Init(const std::string& title, int width, int height, bool fullscreen, bool maximized) {

        // Set up GLFW ------------------------------------------------------------------------- //
        if (!glfwInit()) {
            std::cout << "[WindowManager] Failed to intialize GLFW." << std::endl;
            return false;
        }
        // No api -> Vulkan
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        // Opening maximized via the hint (rather than maximizing after) avoids a visible jump.
        // When opening fullscreen it is applied after, so leaving fullscreen restores to maximized.
        glfwWindowHint(GLFW_MAXIMIZED, (maximized && !fullscreen) ? GLFW_TRUE : GLFW_FALSE);
        // Borderless fullscreen is a plain window (ApplyFullscreen), so this only matters for
        // exclusive fullscreen, which turns it on when it starts.
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

        width  = std::max(width, 1);
        height = std::max(height, 1);

        m_pWindow = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
        if (m_pWindow == NULL)
        {
            std::cout << "[WindowManager] Failed to create GLFW window." << std::endl;
            glfwTerminate();
            return false;
        }
        glfwSetWindowUserPointer(m_pWindow, this);
        glfwSetFramebufferSizeCallback(m_pWindow, FramebufferResizeCallback);
        glfwSetWindowSizeCallback(m_pWindow, WindowSizeCallback);
        glfwSetWindowPosCallback(m_pWindow, WindowPosCallback);
        glfwSetWindowMaximizeCallback(m_pWindow, WindowMaximizeCallback);

        // The windowed rect to come back to is the requested size, wherever the OS placed it.
        m_windowedW = width;
        m_windowedH = height;
        glfwGetWindowPos(m_pWindow, &m_windowedX, &m_windowedY);

        m_maximized = glfwGetWindowAttrib(m_pWindow, GLFW_MAXIMIZED) == GLFW_TRUE;
        glfwGetWindowSize(m_pWindow, &m_width, &m_height);

        if (fullscreen) {
            if (maximized) { m_maximized = true; }   // restored to maximized when leaving fullscreen
            SetFullscreen(true);
        }

        return true;
    }

    // ---- Fullscreen / maximize --------------------------------------------------------------- //

    GLFWmonitor* WindowManager::FindCurrentMonitor() const {

        int x = 0, y = 0, w = 0, h = 0;
        glfwGetWindowPos(m_pWindow, &x, &y);
        glfwGetWindowSize(m_pWindow, &w, &h);
        const int cx = x + w / 2;
        const int cy = y + h / 2;

        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {
            int mx = 0, my = 0;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (!mode) { continue; }
            if (cx >= mx && cx < mx + mode->width && cy >= my && cy < my + mode->height) {
                return monitors[i];
            }
        }
        return glfwGetPrimaryMonitor();
    }

    void WindowManager::SetFullscreen(bool fullscreen) {

        if (!m_pWindow || fullscreen == m_fullscreen) { return; }

        if (fullscreen) {
            GLFWmonitor* monitor = FindCurrentMonitor();
            if (!monitor || !glfwGetVideoMode(monitor)) {
                std::cout << "[WindowManager] No monitor to go fullscreen on." << std::endl;
                return;
            }
            GLFWvidmode chosen{};
            if (!PickFullscreenMode(monitor, chosen)) {
                // A saved resolution this monitor does not offer (another monitor, a driver
                // change): fall back to borderless rather than fail.
                std::cout << "[WindowManager] Fullscreen resolution " << m_fullscreenW << "x" << m_fullscreenH
                          << " is not available on this monitor - using its own resolution." << std::endl;
                m_fullscreenW = m_fullscreenH = m_fullscreenRefresh = 0;
            }
            // m_windowedX/Y/W/H already hold the last normal rect (the callbacks skip
            // maximized sizes), so leaving fullscreen can put the window back there.
            // Set the flag first: the size/pos callbacks fired by the switch must not
            // overwrite the remembered windowed rect.
            m_fullscreen = true;
            // A maximized window has to be restored to normal first: on Windows it otherwise
            // keeps its "maximized" flag through fullscreen, so maximizing it again on the way
            // out does nothing and it comes back at the smaller windowed size. m_maximized
            // stays set (the callback ignores changes while m_fullscreen is true), so leaving
            // fullscreen maximizes it again.
            if (glfwGetWindowAttrib(m_pWindow, GLFW_MAXIMIZED) == GLFW_TRUE) { glfwRestoreWindow(m_pWindow); }
            ApplyFullscreen(monitor);
            return;   // ApplyFullscreen read the size and flagged the swapchain
        }
        else {
            // Read before the switch, and keep m_fullscreen set during it: leaving the monitor
            // fires an un-maximize event that would otherwise clear m_maximized (and record
            // the fullscreen size as the windowed one) before we get to re-maximize.
            const bool restoreMaximized = m_maximized;
            glfwSetWindowAttrib(m_pWindow, GLFW_DECORATED, GLFW_TRUE);   // borderless took the title bar away
            glfwSetWindowMonitor(m_pWindow, nullptr, m_windowedX, m_windowedY, m_windowedW, m_windowedH, 0);
            m_fullscreen = false;
            UpdateBorderlessTopmost();   // back to a normal (not always-on-top) window
            if (restoreMaximized) { glfwMaximizeWindow(m_pWindow); }
            else if (IsAspectRatioLocked()) { glfwSetWindowAspectRatio(m_pWindow, m_aspectWidth, m_aspectHeight); }
            m_maximized = restoreMaximized;
        }
        glfwGetWindowSize(m_pWindow, &m_width, &m_height);
        m_framebufferResized = true;
    }

    void WindowManager::Maximize() {

        if (!m_pWindow) { return; }
        if (m_fullscreen) { m_maximized = true; return; }   // applied when fullscreen ends
        glfwMaximizeWindow(m_pWindow);
        m_maximized = true;
    }

    void WindowManager::Restore() {

        if (!m_pWindow) { return; }
        if (m_fullscreen) { m_maximized = false; return; }  // applied when fullscreen ends
        glfwRestoreWindow(m_pWindow);
        m_maximized = false;
        ApplyPendingSize();
        FitToAspectRatio();
    }

    // ---- Fullscreen resolution / windowed size ----------------------------------------------- //

    bool WindowManager::PickFullscreenMode(GLFWmonitor* monitor, GLFWvidmode& out) const {

        const GLFWvidmode* current = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!current) { return false; }
        if (!IsExclusiveFullscreen()) { out = *current; return true; }
        const GLFWvidmode* mode = FindMode(monitor, m_fullscreenW, m_fullscreenH, m_fullscreenRefresh);
        if (!mode) { return false; }
        out = *mode;
        return true;
    }

    bool WindowManager::SetFullscreenResolution(int width, int height, int refreshRate) {

        if (!m_pWindow) { return false; }
        GLFWmonitor* monitor = m_fullscreen && glfwGetWindowMonitor(m_pWindow) ? glfwGetWindowMonitor(m_pWindow) : FindCurrentMonitor();

        if (width <= 0 || height <= 0) {
            m_fullscreenW = m_fullscreenH = m_fullscreenRefresh = 0;     // borderless
        }
        else {
            if (!FindMode(monitor, width, height, std::max(refreshRate, 0))) { return false; }
            m_fullscreenW       = width;
            m_fullscreenH       = height;
            m_fullscreenRefresh = std::max(refreshRate, 0);
        }

        // Already fullscreen: switch now (exclusive <-> borderless, or another resolution).
        // Not fullscreen: used by the next SetFullscreen.
        if (m_fullscreen) { return ApplyFullscreen(monitor); }
        return true;
    }

    bool WindowManager::ApplyFullscreen(GLFWmonitor* monitor) {

        if (!m_pWindow || !monitor) { return false; }

        // A fullscreen window has the monitor's shape, whatever the aspect lock says: drop GLFW's
        // ratio so nothing resizes the borderless window to it (Windows applies it to any
        // non-monitor window). SetFullscreen(false) / FitToAspectRatio put it back.
        glfwSetWindowAspectRatio(m_pWindow, GLFW_DONT_CARE, GLFW_DONT_CARE);

        if (IsExclusiveFullscreen()) {
            // A monitor window at the chosen video mode. It minimizes on alt-tab, so the desktop
            // gets its own resolution back (and the always-on-top monitor window gets out of the way).
            GLFWvidmode mode{};
            if (!PickFullscreenMode(monitor, mode)) { return false; }
            glfwSetWindowAttrib(m_pWindow, GLFW_AUTO_ICONIFY, GLFW_TRUE);
            glfwSetWindowMonitor(m_pWindow, monitor, 0, 0, mode.width, mode.height, mode.refreshRate);
        }
        else {
            // Borderless: a normal window with no title bar, exactly over the monitor. Not a
            // monitor window - GLFW makes those always-on-top on Windows, so with no
            // auto-minimize it would stay in front of whatever alt-tab switched to.
            // Coming from exclusive: leave the monitor first, so it is back at its own resolution
            // before that resolution is read.
            if (glfwGetWindowMonitor(m_pWindow)) {
                glfwSetWindowMonitor(m_pWindow, nullptr, m_windowedX, m_windowedY, m_windowedW, m_windowedH, 0);
            }
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (!mode) { return false; }
            int monitorX = 0, monitorY = 0;
            glfwGetMonitorPos(monitor, &monitorX, &monitorY);
            glfwSetWindowAttrib(m_pWindow, GLFW_AUTO_ICONIFY, GLFW_FALSE);
            glfwSetWindowAttrib(m_pWindow, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(m_pWindow, nullptr, monitorX, monitorY, mode->width, mode->height, 0);
            glfwFocusWindow(m_pWindow);
        }
        UpdateBorderlessTopmost();
        glfwGetWindowSize(m_pWindow, &m_width, &m_height);
        m_framebufferResized = true;
        return true;
    }

    std::vector<WindowResolution> WindowManager::GetResolutions() const {

        std::vector<WindowResolution> list;
        if (!m_pWindow) { return list; }
        GLFWmonitor* monitor = m_fullscreen && glfwGetWindowMonitor(m_pWindow) ? glfwGetWindowMonitor(m_pWindow) : FindCurrentMonitor();
        if (!monitor) { return list; }

        int count = 0;
        const GLFWvidmode* modes = glfwGetVideoModes(monitor, &count);
        for (int i = 0; i < count; ++i) {
            const GLFWvidmode& mode = modes[i];
            auto same = std::find_if(list.begin(), list.end(), [&](const WindowResolution& r) {
                return r.width == mode.width && r.height == mode.height; });
            if (same == list.end()) { list.push_back({ mode.width, mode.height, mode.refreshRate }); }
            else if (mode.refreshRate > same->refreshRate) { same->refreshRate = mode.refreshRate; }
        }
        std::sort(list.begin(), list.end(), [](const WindowResolution& a, const WindowResolution& b) {
            return a.width != b.width ? a.width < b.width : a.height < b.height; });
        return list;
    }

    void WindowManager::SetWindowedSize(int width, int height) {

        if (!m_pWindow || width <= 0 || height <= 0) { return; }

        // The aspect lock wins: keep the width, the height follows (as FitToAspectRatio does).
        if (IsAspectRatioLocked()) {
            height = std::max(1, static_cast<int>(static_cast<long long>(width) * m_aspectHeight / m_aspectWidth));
        }

        // Keep the whole window, title bar and borders included, inside the monitor's work area
        // (not while fullscreen: an exclusive mode shrinks the work area until it ends).
        if (!m_fullscreen) {
            int areaX = 0, areaY = 0, areaW = 0, areaH = 0;
            if (GLFWmonitor* monitor = FindCurrentMonitor()) { glfwGetMonitorWorkarea(monitor, &areaX, &areaY, &areaW, &areaH); }
            int left = 0, top = 0, right = 0, bottom = 0;
            glfwGetWindowFrameSize(m_pWindow, &left, &top, &right, &bottom);
            const int maxW = areaW - left - right;
            const int maxH = areaH - top - bottom;
            if (maxW > 0 && maxH > 0 && (width > maxW || height > maxH)) {
                const double scale = std::min(static_cast<double>(maxW) / width, static_cast<double>(maxH) / height);
                width  = std::max(1, static_cast<int>(width * scale));
                height = std::max(1, static_cast<int>(height * scale));
            }
        }

        m_windowedW = width;
        m_windowedH = height;

        if (m_fullscreen) { m_pendingW = m_pendingH = 0; return; }        // SetFullscreen(false) uses m_windowedW/H
        if (m_maximized)  { m_pendingW = width; m_pendingH = height; return; }   // applied on Restore
        glfwSetWindowSize(m_pWindow, width, height);
    }

    void WindowManager::ApplyPendingSize() {

        if (m_pendingW <= 0 || m_fullscreen || m_maximized) { return; }
        const int width = m_pendingW, height = m_pendingH;
        m_pendingW = m_pendingH = 0;
        glfwSetWindowSize(m_pWindow, width, height);
        m_windowedW = width;
        m_windowedH = height;
    }

    // ---- Aspect ratio ------------------------------------------------------------------------ //

    void WindowManager::LockAspectRatio(int width, int height) {

        if (width <= 0 || height <= 0) { return; }
        m_aspectWidth  = width;
        m_aspectHeight = height;
        // Only handed to GLFW for a normal window (FitToAspectRatio does it). On Windows,
        // glfwSetWindowAspectRatio resizes a non-monitor window on the spot - and borderless
        // fullscreen is one - so locking 16:9 while borderless on a 16:10 screen cut the window
        // short and left a strip of desktop below it. A maximized window would be squashed the
        // same way. Leaving fullscreen / un-maximizing applies the lock (SetFullscreen,
        // FitToAspectRatio).
        FitToAspectRatio();
    }

    void WindowManager::FitToAspectRatio() {

        // Resizing a fullscreen window would change the monitor's video mode, and resizing a
        // maximized one would leave it "maximized" at the wrong size: only fit a normal window.
        if (!IsAspectRatioLocked() || m_fullscreen || m_maximized) { return; }

        // (Re)arm GLFW's enforcement, which keeps the shape while the user drags the edges.
        // ApplyFullscreen clears it, so this also restores it after leaving fullscreen.
        glfwSetWindowAspectRatio(m_pWindow, m_aspectWidth, m_aspectHeight);

        // GLFW only enforces the ratio on the next resize, so fit the window to it now.
        int currentWidth = 0, currentHeight = 0;
        glfwGetWindowSize(m_pWindow, &currentWidth, &currentHeight);
        if (currentWidth > 0) {
            const int fittedHeight = static_cast<int>(static_cast<long long>(currentWidth) * m_aspectHeight / m_aspectWidth);
            if (fittedHeight > 0 && fittedHeight != currentHeight) { glfwSetWindowSize(m_pWindow, currentWidth, fittedHeight); }
        }
    }

    void WindowManager::UnlockAspectRatio() {

        m_aspectWidth  = 0;
        m_aspectHeight = 0;
        glfwSetWindowAspectRatio(m_pWindow, GLFW_DONT_CARE, GLFW_DONT_CARE);
    }

    void WindowManager::UpdateBorderlessTopmost() {

#ifdef _WIN32
        if (!m_pWindow) { return; }
        HWND hwnd = glfwGetWin32Window(m_pWindow);
        if (!hwnd) { return; }

        // Windows only hides the taskbar for a window it decides is "fullscreen", and for a plain
        // borderless window that check is unreliable: it is only re-evaluated when the window is
        // activated, so a window that was made monitor-sized after it was shown (startup, F11),
        // or that did not get the foreground at launch, can keep the taskbar on top of it.
        // Making the borderless window topmost while it has focus puts it above the taskbar
        // every time. It drops back to a normal window the moment it loses focus, so alt-tab
        // still shows the other app (the reason borderless is not a GLFW monitor window).
        // Exclusive fullscreen is left alone: GLFW makes monitor windows topmost itself.
        const bool borderless = m_fullscreen && glfwGetWindowMonitor(m_pWindow) == nullptr;
        const bool focused    = glfwGetWindowAttrib(m_pWindow, GLFW_FOCUSED)   == GLFW_TRUE;
        const bool iconified  = glfwGetWindowAttrib(m_pWindow, GLFW_ICONIFIED) == GLFW_TRUE;
        const bool want       = borderless && focused && !iconified;

        // Read the real state rather than caching it: GLFW changes it too (monitor windows).
        const bool isTopmost = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        if (want == isTopmost) { return; }
        if (glfwGetWindowMonitor(m_pWindow)) { return; }   // exclusive: GLFW's business

        constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
        if (want) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags);
        }
        else {
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
            // HWND_NOTOPMOST puts it at the top of the normal windows - i.e. in front of the
            // app the player just alt-tabbed to. Tuck it back behind that one.
            HWND foreground = GetForegroundWindow();
            if (foreground && foreground != hwnd) {
                SetWindowPos(hwnd, foreground, 0, 0, 0, 0, flags);
            }
        }
#endif
    }

    bool WindowManager::IsMinimized() const {
        if (glfwGetWindowAttrib(m_pWindow, GLFW_ICONIFIED) == GLFW_TRUE) { return true; }
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_pWindow, &width, &height);
        return width == 0 || height == 0;
    }

    bool WindowManager::IsFocused() const {
        return glfwGetWindowAttrib(m_pWindow, GLFW_FOCUSED) == GLFW_TRUE;
    }

    void WindowManager::PollEvents() {
        glfwPollEvents();
        // Focus changes (alt-tab, clicking another monitor) arrive with the events.
        UpdateBorderlessTopmost();
        if (m_refitPending) { m_refitPending = false; ApplyPendingSize(); FitToAspectRatio(); }
    }
    bool WindowManager::ShouldClose() const { return glfwWindowShouldClose(m_pWindow); }

    void WindowManager::RequestClose() { if (m_pWindow) { glfwSetWindowShouldClose(m_pWindow, GLFW_TRUE); } }

    // ---- GLFW callbacks ---------------------------------------------------------------------- //

    void WindowManager::FramebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        (void)width;
        (void)height;
        auto win = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
        win->m_framebufferResized = true;
    }

    void WindowManager::WindowSizeCallback(GLFWwindow* window, int width, int height)
    {
        auto win = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
        if (width <= 0 || height <= 0) { return; }   // minimized
        win->m_width  = width;
        win->m_height = height;

        // Remember the normal size only; IsZoomed is already up to date when this fires on Windows.
        if (!win->m_fullscreen && glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != GLFW_TRUE
            && glfwGetWindowAttrib(window, GLFW_ICONIFIED) != GLFW_TRUE) {
            win->m_windowedW = width;
            win->m_windowedH = height;
        }
    }

    void WindowManager::WindowPosCallback(GLFWwindow* window, int x, int y)
    {
        auto win = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
        if (!win->m_fullscreen && glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != GLFW_TRUE
            && glfwGetWindowAttrib(window, GLFW_ICONIFIED) != GLFW_TRUE) {
            win->m_windowedX = x;
            win->m_windowedY = y;
        }
    }

    void WindowManager::WindowMaximizeCallback(GLFWwindow* window, int maximized)
    {
        auto win = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
        // Entering / leaving fullscreen can report un-maximize; keep the remembered state then.
        if (win->m_fullscreen) { return; }
        win->m_maximized = (maximized == GLFW_TRUE);
        // Re-fit to the aspect lock after the event loop, not from inside the OS's size message.
        if (!win->m_maximized) { win->m_refitPending = true; }
    }
}
