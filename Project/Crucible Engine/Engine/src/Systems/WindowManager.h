#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>

namespace Crucible {

    /*
        Manager for application window.
    */
    class WindowManager {
    
    public:
        WindowManager(int w, int h, const std::string& title);
        ~WindowManager() { glfwDestroyWindow(m_pWindow); glfwTerminate(); }

        // Remove copy constructors.
        WindowManager(const WindowManager&) = delete;
        WindowManager& operator=(const WindowManager&) = delete;

        bool Init(const std::string& title, int width, int height);
        
        void PollEvents();
        void SwapBuffers();
        bool ShouldClose() const;

        GLFWwindow* GetNativeHandle() const { return m_pWindow; }

    private:
        GLFWwindow* m_pWindow = nullptr;

        int m_width = 1;
        int m_height = 1;

    };

}