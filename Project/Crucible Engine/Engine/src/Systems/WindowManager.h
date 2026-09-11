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
        ~WindowManager() { Shutdown(); }

        bool Init(const std::string& title, int width, int height, float bgColor[4]);
        
        void PollEvents();
        void SwapBuffers();
        void Shutdown();
        bool ShouldClose() const;

        GLFWwindow* GetNativeHandle() const { return m_pWindow; }

    private:
        GLFWwindow* m_pWindow = nullptr;

    };

}