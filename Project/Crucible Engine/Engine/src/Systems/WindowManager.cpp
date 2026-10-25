#include "WindowManager.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace Crucible {

    WindowManager::WindowManager(int w, int h, const std::string& title, bool fullscreen, bool maximized) {
        if (!Init(title, w, h, fullscreen, maximized)) {
            throw std::runtime_error("[WindowManager] Initialization failed.");
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
        // Fullscreen here is "borderless" style: alt-tabbing away must not minimize the window.
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
            const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
            if (!mode) {
                std::cout << "[WindowManager] No monitor to go fullscreen on." << std::endl;
                return;
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
            glfwSetWindowMonitor(m_pWindow, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        else {
            // Read before the switch, and keep m_fullscreen set during it: leaving the monitor
            // fires an un-maximize event that would otherwise clear m_maximized (and record
            // the fullscreen size as the windowed one) before we get to re-maximize.
            const bool restoreMaximized = m_maximized;
            glfwSetWindowMonitor(m_pWindow, nullptr, m_windowedX, m_windowedY, m_windowedW, m_windowedH, 0);
            m_fullscreen = false;
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
        FitToAspectRatio();
    }

    // ---- Aspect ratio ------------------------------------------------------------------------ //

    void WindowManager::LockAspectRatio(int width, int height) {

        if (width <= 0 || height <= 0) { return; }
        m_aspectWidth  = width;
        m_aspectHeight = height;
        // GLFW keeps the ratio for a fullscreen window and applies it once it is windowed again.
        glfwSetWindowAspectRatio(m_pWindow, width, height);
        FitToAspectRatio();
    }

    void WindowManager::FitToAspectRatio() {

        // Resizing a fullscreen window would change the monitor's video mode, and resizing a
        // maximized one would leave it "maximized" at the wrong size: only fit a normal window.
        if (!IsAspectRatioLocked() || m_fullscreen || m_maximized) { return; }

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

    void WindowManager::PollEvents() {
        glfwPollEvents();
        if (m_refitPending) { m_refitPending = false; FitToAspectRatio(); }
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
