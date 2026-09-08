#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>

namespace Crucible {

    /*
        Manager for main application window.
    */
    class AppManager {
    
    public:
        bool Init(const std::string& title, int width, int height, float bgColor[4]);
        void Update(float deltaTime);
        void CleanUp();

        // Accesors
        GLFWwindow* GetParentWindow() const { return m_parentWindow; }
        bool AppShouldClose() const { return m_appShouldClose; }

    private:
        GLFWwindow* m_parentWindow;
        bool m_appShouldClose = false;

    };

}