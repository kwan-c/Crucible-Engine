#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>

#include "EngineExport.h"	// DLL export

namespace Crucible
{
    class ENGINE_API Core
    {
    public:
        // Core main functions
        bool Init();
        void Update();
        void CleanUp();

        // Accessors
        GLFWwindow* GetWindow() const;
        bool AppShouldClose() const;

    private:

        // Application window helpers
        bool InitAppWindow(std::string const&, int width, int height);
        void UpdateAppWindow();
        void CleanUpAppWindow();

        GLFWwindow* m_appWindow;
        bool m_appShouldClose = false;

        // Core systems
        //std::vector<BaseSystem*> m_systems;
        
    };
}