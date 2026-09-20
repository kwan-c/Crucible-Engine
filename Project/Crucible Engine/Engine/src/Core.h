#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <memory>

#include "EngineExport.h"	// DLL export
#include "Systems/Layer.h"
#include "Systems/WindowManager.h"
#include "Graphics/RenderSystem.h"
#include <ECS/World.h>
#include <ECS/Systems/ECSSystemManager.h>

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

        World& GetWorld() noexcept { return *m_world; }
        const World& GetWorld() const noexcept { return *m_world; }

    private:
        void CleanUp();

        WindowManager* m_window;
        Renderer* m_renderer;

        std::unique_ptr<World> m_world;

        ECSSystemManager m_systems;

        // Core systems
        //std::vector<BaseSystem*> m_systems;
    };
}