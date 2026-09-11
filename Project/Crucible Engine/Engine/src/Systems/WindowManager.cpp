#include "WindowManager.h"
#include <iostream>

namespace {

    // Helper functions for window operations;
    void static framebuffer_size_callback(GLFWwindow* window, int width, int height)
    {
        (void)window;
        glViewport(0, 0, width, height);
    }
    void static processInput(GLFWwindow *window)
    {
        if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
    }
}


namespace Crucible {

    bool WindowManager::Init(const std::string& title, int width, int height, float bgColor[4]) {

        // Set up GLFW ------------------------------------------------------------------------- //
        if (!glfwInit()) {
            std::cout << "Failed to intialize GLFW." << std::endl;
            return false;
        }
        // No api -> Vulkan
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        m_pWindow = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
        if (m_pWindow == NULL)
        {
            std::cout << "Failed to create GLFW window." << std::endl;
            glfwTerminate();
            return false;
        }

        return true;
    }

    void WindowManager::PollEvents() { glfwPollEvents(); }
    void WindowManager::SwapBuffers() { glfwSwapBuffers(m_pWindow); }
    bool WindowManager::ShouldClose() const { return glfwWindowShouldClose(m_pWindow); }
    void WindowManager::Shutdown() { glfwDestroyWindow(m_pWindow); glfwTerminate(); }
}