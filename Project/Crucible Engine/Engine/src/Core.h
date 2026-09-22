#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <memory>
#include <vector>

#include "EngineExport.h"	// DLL export
#include "Systems/Layer.h"
#include "Systems/WindowManager.h"
#include "Graphics/RenderSystem.h"
#include <ECS/World.h>
#include <ECS/Systems/ECSSystemManager.h>

namespace Crucible
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL member in an exported class; only used inside Engine.dll
#endif
    class ENGINE_API Core
    {
    public:
        Core();
        ~Core();

        // Core owns raw pointers and unique_ptr layers: it must never be copied.
        // (Also required because MSVC instantiates copy operations for dllexport classes.)
        Core(const Core&) = delete;
        Core& operator=(const Core&) = delete;

        // Core main functions
        bool Init();
        void Run();

        Renderer& GetRenderer() const { return *m_renderer; }

        World& GetWorld() noexcept { return *m_world; }
        const World& GetWorld() const noexcept { return *m_world; }
        // Attaches a layer (calls OnAttach) and takes ownership. Layers are updated in the
        // order pushed, and detached in reverse order when Core shuts down.
        void PushLayer(std::unique_ptr<Layer> layer);

    private:

        void RegisterEngineSystems();

        void CleanUp();

        WindowManager* m_window;
        Renderer* m_renderer;
        std::vector<std::unique_ptr<Layer>> m_layers;

        std::unique_ptr<World> m_world;

        ECSSystemManager m_systems;

        // Core systems
        //std::vector<BaseSystem*> m_systems;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}