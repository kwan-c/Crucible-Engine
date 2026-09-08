#include "AppManager.h"

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

    bool AppManager::Init(const std::string& title, int width, int height, float bgColor[4]) {

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

    void AppManager::Update() {

        glClear(GL_COLOR_BUFFER_BIT);
        
        processInput(m_appWindow);
        m_appShouldClose = glfwWindowShouldClose(m_appWindow);

        glfwSwapBuffers(m_appWindow);
        glfwPollEvents(); 
    }

    void AppManager::CleanUp() {

        glfwTerminate();
    }
    
}