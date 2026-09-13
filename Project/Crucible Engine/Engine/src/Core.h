#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <memory>

#include "EngineExport.h"	// DLL export
#include "Systems/Layer.h"
#include "Systems/WindowManager.h"
#include "Graphics/RenderSystem.h"

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
        void CleanUp();
        
        WindowManager* m_window;
        Renderer* m_renderer;

        // Core systems
        //std::vector<BaseSystem*> m_systems;
    };
}