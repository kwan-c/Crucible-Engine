#pragma once

#include <GLFW/glfw3.h>

#include <string>
#include <memory>
#include <vector>

#include "EngineExport.h"	// DLL export
#include "Systems/Layer.h"
#include "Systems/WindowManager.h"
#include "Graphics/RenderSystem.h"
#include "Input/InputSystem.h"
#include <ECS/World.h>
#include <ECS/Systems/ECSSystemManager.h>
#include <ECS/Systems/EventBus/EventBuses.h>
#include <ECS/Systems/EventBus/EcsEvents.h>
#include <Configuration/EngineConfig.h>
#include "AssetPipe/Assetmanager.h"
#include "Resources/ResourceManager.h"

namespace Crucible
{

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL member in an exported class; only used inside Engine.dll
#endif
    class ENGINE_API Core
    {
    public:
        explicit Core(const EngineConfig& m_config = EngineConfig{});
        ~Core();

        // Core owns raw pointers and unique_ptr layers: it must never be copied.
        // (Also required because MSVC instantiates copy operations for dllexport classes.)
        Core(const Core&) = delete;
        Core& operator=(const Core&) = delete;

        // Core main functions
        bool Init();
        void Run();

        Renderer& GetRenderer() const { return *m_renderer; }
        // Both valid only after Init(). The engine's shared asset index (AssetManager) and the
        // GUID -> Renderer-handle cache built on top of it (ResourceManager).
        AssetManager&    GetAssets() const { return *m_assets; }
        ResourceManager& GetResources() const { return *m_resources; }

        World& GetWorld() noexcept { return *m_world; }
        const World& GetWorld() const noexcept { return *m_world; }

        // The ECS system driver, e.g. for Systems().RegisterSystem<MySystem>().
        ECSSystemManager& Systems() noexcept { return m_systems; }

        // The engine / gameplay / network event buses (subscribe from a system's Init()).
        EventBuses& Events() noexcept { return m_events; }

        // Opt-in entity/component events on the gameplay bus, e.g. EcsBridge().OnRemoved<MeshRenderer>(...).
        // (Not called "EcsEvents()" - that would hide the EcsEvents namespace inside Core.)
        EcsEventBridge& EcsBridge() noexcept { return *m_ecsEvents; }
        // Attaches a layer (calls OnAttach) and takes ownership. Layers are updated in the
        // order pushed, and detached in reverse order when Core shuts down.
        void PushLayer(std::unique_ptr<Layer> layer);

    private:
        EngineConfig m_config;
        void RegisterEngineSystems();

        void CleanUp();

        WindowManager* m_window;
        Renderer* m_renderer;
        Input m_input;      // owns the GLFW input callbacks; queried statically, e.g. Input::IsDown
        std::vector<std::unique_ptr<Layer>> m_layers;

        // Declared BEFORE the World: members are destroyed in reverse order, so the buses
        // outlive the World and every system (the ECS bridge's World hooks point at a bus).
        EventBuses m_events;

        std::unique_ptr<World> m_world;
        std::unique_ptr<AssetManager>    m_assets;      // created in Init()
        std::unique_ptr<ResourceManager> m_resources;   // created in Init(), after m_assets

        // World hooks -> gameplay-bus events; created right after the World.
        std::unique_ptr<EcsEventBridge> m_ecsEvents;

        ECSSystemManager m_systems;

        // Core systems
        //std::vector<BaseSystem*> m_systems;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}