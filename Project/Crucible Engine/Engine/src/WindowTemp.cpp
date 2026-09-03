#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>

#include "WindowTemp.h"

namespace Crucible
{
    // Required OpenGL version;
    static constexpr int REQUIRED_GL_MAJOR = 4;
    static constexpr int REQUIRED_GL_MINOR = 5;

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

    int CreateWindow()
    {
        // Set up GLFW ------------------------------------------------------------------------- //
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, REQUIRED_GL_MAJOR);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, REQUIRED_GL_MINOR);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        GLFWwindow* window = glfwCreateWindow(1200, 800, "Crucible Engine", NULL, NULL);
        if (window == NULL)
        {
            std::cout << "Failed to create GLFW window" << std::endl;
            glfwTerminate();
            return -1;
        }
        glfwMakeContextCurrent(window);

        // Set up GLAD ------------------------------------------------------------------------- //
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cout << "Failed to initialize GLAD" << std::endl;
            return -1;
        }

        glViewport(0, 0, 1200, 800);
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glClearColor(0.2f, 0.1f, 0.1f, 1.0f);
    
        while(!glfwWindowShouldClose(window))
        {
            processInput(window);

            glClear(GL_COLOR_BUFFER_BIT);

            glfwSwapBuffers(window);
            glfwPollEvents();    
        }

        // Clean-up ---------------------------------------------------------------------------- //
        glfwTerminate();
        return 0;
    }
}