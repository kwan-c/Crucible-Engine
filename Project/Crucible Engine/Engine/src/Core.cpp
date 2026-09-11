#include "Core.h"

#include <xmath.h>
#include <iostream>

namespace
{
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

namespace Crucible
{
    bool Core::Init()
    {
        int winWidth = 1200;
        int winHeight = 800;
        float bgColor[] = { 0.3f, 0.1f, 0.1f, 1.0f };

        if (!InitAppWindow("Crucible Engine", winWidth, winHeight, bgColor))
        {
            std::cout << "Failed to initialize Core Engine.\n";
            return false;
        }

        std::cout << "The Hearth smoulders..." << std::endl;

        return true;
    }

    void Core::Update()
    {
        UpdateAppWindow();

        /*
            for (System s : m_systems)
            {
                s.Update();
            }
        */
    }

    void Core::CleanUp()
    {
        CleanUpAppWindow();
    }

    bool Core::InitAppWindow(std::string const& windowTitle, int width, int height, float bgColor[4])
    {
        // Set up GLFW ------------------------------------------------------------------------- //
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, REQUIRED_GL_MAJOR);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, REQUIRED_GL_MINOR);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        m_appWindow = glfwCreateWindow(width, height, windowTitle.c_str(), NULL, NULL);
        if (m_appWindow == NULL)
        {
            std::cout << "Failed to create GLFW window." << std::endl;
            glfwTerminate();
            return false;
        }
        glfwMakeContextCurrent(m_appWindow);

        // Set up GLAD ------------------------------------------------------------------------- //
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cout << "Failed to initialize GLAD." << std::endl;
            glfwTerminate();
            return false;
        }

        glViewport(0, 0, width, height);
        glfwSetFramebufferSizeCallback(m_appWindow, framebuffer_size_callback);
        glClearColor(bgColor[0], bgColor[1], bgColor[2], bgColor[3]);

        return true;
    }

    void Core::UpdateAppWindow()
    {
        glClear(GL_COLOR_BUFFER_BIT);
        
        processInput(m_appWindow);
        m_appShouldClose = glfwWindowShouldClose(m_appWindow);

        glfwSwapBuffers(m_appWindow);
        glfwPollEvents();  
    }

    void Core::CleanUpAppWindow()
    {
        glfwTerminate();
    }

    // Accessor => pointer to application window;
    GLFWwindow* Core::GetWindow() const { return m_appWindow; }
    // Accessor => boolean data member tracking whether app should close;
    bool Core::AppShouldClose() const { return m_appShouldClose; }
}