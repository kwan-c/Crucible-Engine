#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <memory>

#include "Diagnostics/Logger.h"

#include "EngineExport.h"	// DLL export
#include "Systems/Layer.h"
#include "Systems/WindowManager.h"
#include "Input/InputSystem.h"

namespace Crucible
{
    class ENGINE_API Core
    {
    public:
        Core();
        ~Core();

        // Core main functions
        bool Init();
        void Run();

    private:

        // Logger is live before anything else is live, and dies last too
        Debug::LogSession m_logSession{ { .logFile = "crucible.log" } };
       
        void CleanUp();
        
        std::unique_ptr<WindowManager> m_window;
        std::unique_ptr<Input> m_input;

        // Core systems
        //std::vector<BaseSystem*> m_systems;
    };
}