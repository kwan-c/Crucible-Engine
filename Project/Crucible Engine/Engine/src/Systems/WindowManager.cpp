#include "WindowManager.h"
#include <iostream>

namespace {

    void static processInput(GLFWwindow* window)
    {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
    }
}


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

    void WindowManager::PollEvents() { glfwPollEvents(); processInput(m_pWindow); }
    void WindowManager::SwapBuffers() { glfwSwapBuffers(m_pWindow); }
    bool WindowManager::ShouldClose() const { return glfwWindowShouldClose(m_pWindow); }

    void WindowManager::FramebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        (void)width;
        (void)height;
        auto win = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
        win->m_framebufferResized = true;
    }
}