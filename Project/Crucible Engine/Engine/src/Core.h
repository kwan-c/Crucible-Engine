#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <memory>

#include "EngineExport.h"	// DLL export
#include "Systems/Layer.h"

namespace Crucible
{
    class ENGINE_API Core
    {
    public:
        // Core main functions
        bool Init();
        void Run();

        // Manage system layers
        void PushLayer(std::shared_ptr<Layer> layer);

    private:

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