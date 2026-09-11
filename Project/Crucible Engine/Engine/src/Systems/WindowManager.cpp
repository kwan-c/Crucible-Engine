#include "WindowManager.h"
#include <iostream>

namespace {

    // Required OpenGL version;
    static constexpr int REQUIRED_GL_MAJOR = 4;
    static constexpr int REQUIRED_GL_MINOR = 5;

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
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, REQUIRED_GL_MAJOR);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, REQUIRED_GL_MINOR);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        m_pWindow = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
        if (m_pWindow == NULL)
        {
            std::cout << "Failed to create GLFW window." << std::endl;
            glfwTerminate();
            return false;
        }
        glfwMakeContextCurrent(m_pWindow);

        // Set up GLAD ------------------------------------------------------------------------- //
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cout << "Failed to initialize GLAD." << std::endl;
            glfwTerminate();
            return false;
        }

        glViewport(100, 100, width, height);
        glfwSetFramebufferSizeCallback(m_pWindow, framebuffer_size_callback);
        glClearColor(bgColor[0], bgColor[1], bgColor[2], bgColor[3]);

        return true;
    }

    void WindowManager::PollEvents() { glClear(GL_COLOR_BUFFER_BIT); glfwPollEvents(); processInput(m_pWindow); }
    void WindowManager::SwapBuffers() { glfwSwapBuffers(m_pWindow); }
    bool WindowManager::ShouldClose() const { return glfwWindowShouldClose(m_pWindow); }
    void WindowManager::Shutdown() { glfwDestroyWindow(m_pWindow); glfwTerminate(); }
}