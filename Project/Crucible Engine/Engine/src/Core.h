#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>

#include "Diagnostics/Logger.h"

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

        // Declared FIRST on purpose: members are constructed in declaration
        // order and destroyed in reverse, so logging is live before anything
        // else in Core starts, and is still live while everything else shuts
        // down. Owning it here means any consumer of the engine - game,
        // editor, tools - gets logging without having to switch it on.
        Debug::LogSession m_logSession{ { .logFile = "crucible.log" } };

        // Application window helpers
        bool InitAppWindow(std::string const&, int width, int height, float bgColor[4]);
        void UpdateAppWindow();
        void CleanUpAppWindow();

        GLFWwindow* m_appWindow;
        
        bool m_appShouldClose = false;

        // Core systems
        //std::vector<BaseSystem*> m_systems;
        
    };
}