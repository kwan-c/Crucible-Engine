#include "WindowManager.h"
#include <iostream>

namespace Crucible {

    WindowManager::WindowManager(int w, int h, const std::string& title) {
        if (!Init(title, w, h)) {
            throw std::runtime_error("[WindowManager] Initialization failed.");
        }
    }

    bool WindowManager::Init(const std::string& title, int width, int height) {

        // Set up GLFW ------------------------------------------------------------------------- //
        if (!glfwInit()) {
            std::cout << "[WindowManager] Failed to intialize GLFW." << std::endl;
            return false;
        }
        // No api -> Vulkan
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_pWindow = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
        if (m_pWindow == NULL)
        {
            std::cout << "[WindowManager] Failed to create GLFW window." << std::endl;
            glfwTerminate();
            return false;
        }
        glfwSetWindowUserPointer(m_pWindow, this);
        glfwSetFramebufferSizeCallback(m_pWindow, FramebufferResizeCallback);

        m_width = width;
        m_height = height;

        return true;
    }

    void WindowManager::LockAspectRatio(int width, int height) {

        if (width <= 0 || height <= 0) { return; }
        m_aspectWidth  = width;
        m_aspectHeight = height;
        glfwSetWindowAspectRatio(m_pWindow, width, height);

        // GLFW only enforces the ratio on the next resize, so fit the window to it now.
        int currentWidth = 0, currentHeight = 0;
        glfwGetWindowSize(m_pWindow, &currentWidth, &currentHeight);
        if (currentWidth > 0) {
            const int fittedHeight = static_cast<int>(static_cast<long long>(currentWidth) * height / width);
            if (fittedHeight > 0 && fittedHeight != currentHeight) { glfwSetWindowSize(m_pWindow, currentWidth, fittedHeight); }
        }
    }

    void WindowManager::UnlockAspectRatio() {

        m_aspectWidth  = 0;
        m_aspectHeight = 0;
        glfwSetWindowAspectRatio(m_pWindow, GLFW_DONT_CARE, GLFW_DONT_CARE);
    }

    void WindowManager::PollEvents() { glfwPollEvents(); }
    bool WindowManager::ShouldClose() const { return glfwWindowShouldClose(m_pWindow); }

    void WindowManager::RequestClose() { if (m_pWindow) { glfwSetWindowShouldClose(m_pWindow, GLFW_TRUE); } }

    void WindowManager::FramebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        (void)width;
        (void)height;
        auto win = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
        win->m_framebufferResized = true;
    }
}