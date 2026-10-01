#include "Core.h"
#include "AssetPipe/Assetmanager.h"
#include "Configuration/EngineConfigLoader.h"
#include "Diagnostics/Logger.h"
#include "ECS/ECSPhysicsSystem.h"
#include "ECS/Components/Components.h"
#include "Json/JsonWrapper.h"
#include "Reflection/Serialization.h"
#include "ECS/Archetype/ArchetypeRegistry.h"
#include "ECS/World.h"
#include "Scene/Prefab.h"
#include "Scene/Scene.h"
#include "ECS/HierarchyTransformSystem.h"
#include "Graphics/RenderExtractSystem.h"
#include "Graphics/DebugDrawSystem.h"
#include "Resources/ResourceManager.h"

// STL
#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <cstdlib>



namespace Crucible {

    Core::Core(const EngineConfig& config) :
        m_config{ config },
        m_window{ new WindowManager(m_config.graphics.windowWidth,m_config.graphics.windowHeight, m_config.graphics.windowTitle) },
        m_renderer{ new Renderer(*m_window) },
        m_world{ std::make_unique<World>() },
        m_ecsEvents{ std::make_unique<EcsEventBridge>(*m_world, m_events.gameplay) },
        m_scenes{ std::make_unique<SceneManager>(*m_world) }
    {   
        // Layers draw inside the renderer's open render pass via OnRender().
        m_renderer->SetOverlayCallback([this] {
            for (auto& layer : m_layers) layer->OnRender();
        });
    }
    Core::~Core() {
        CleanUp();
        delete m_renderer;
        delete m_window;
    }

    bool Core::Init()
    {
        std::cout << "The Hearth smoulders...\n";

        // ======== asset debugging (Removable) ==========
        {
            Crucible::AssetManager assets;
            std::cout << "\n\n";
            assets.Init();
            assets.PrintIndex();

            auto tex = assets.Load("Textures/Orange_Milk.jpg");
            assets.PrintLoaded();
        }
        // ===========================================

        // ======== scene / prefab debugging (Removable) ==========
        {
            Crucible::AssetManager assets;
            assets.Init();
            m_scenes->RequestLoad(assets.Resolve("Scenes/demo.scene"));
        }
        // ===========================================
        // Input attaches before anything else installs GLFW callbacks. ImGui's backend
        // chains to callbacks that are already registered, but anything installed after it
        // replaces ImGui's - which is how its text fields go dead.
        m_input.Init();
        m_input.AttachWindow(m_window->GetNativeHandle());

        // Shared asset index + resource cache. Before RegisterEngineSystems(): the render
        // extraction system resolves MeshRenderer/Skybox asset ids through the ResourceManager.
        m_assets = std::make_unique<AssetManager>();
        if (!m_assets->Init()) { CRU_WARN("[Core] AssetManager found no asset root; asset ids will not resolve"); }
        m_resources = std::make_unique<ResourceManager>(*m_assets, *m_renderer);
        m_resources->Init();


        // Event buses: report listener failures through the engine log, and let the
        // system manager merge each system's outboxes into them at every stage flush.
        const auto logFailure = [](const std::string& message) { CRU_ERROR("{}", message); };
        m_events.engine.SetFailureLogger(logFailure);
        m_events.gameplay.SetFailureLogger(logFailure);
        m_events.network.SetFailureLogger(logFailure);
        m_systems.SetEventBuses(&m_events);

        RegisterEngineSystems();

        m_systems.Init();

        return true;
    }

    void Core::Run() {

        double lastTime = glfwGetTime();

        while (!m_window->ShouldClose()) {

            double const now = glfwGetTime();
            float dt = static_cast<float>(now - lastTime);
            lastTime = now;

            // Clamp so a debugger breakpoint, window drag, or other multi-second stall doesn't
            // make every dt-driven system leap forward by however long the freeze lasted.
            dt = std::min(dt, 1.0f / 15.0f);

            // Snapshot last frame's input and zero the per-frame mouse deltas. This MUST stay
            // immediately before PollEvents, because PollEvents is what fires the callbacks
            // that fill this frame's state.
            m_input.BeginFrame();
            m_window->PollEvents();
            m_events.engine.DeliverPending();   // window/input events queued by callbacks

            // Scene swaps destroy every entity - only safe here, before any
            // system or layer is iterating the world.
            m_scenes->ApplyPending();

            m_world->SetDeltaTime(dt);
            m_world->AdvanceTick();
            m_systems.Update(*m_world);         // each stage delivers its own events

            m_events.TickAll(dt);               // delayed events whose time has come get queued

            for (auto& layer : m_layers) layer->OnUpdate(dt);
            m_renderer->Update(dt);
        }
        m_renderer->ShutDown();
    }

   
    void Core::PushLayer(std::unique_ptr<Layer> layer) {
        layer->OnAttach();
        m_layers.push_back(std::move(layer));
    }


    void Core::RegisterEngineSystems()
    {
        //sample system registeration
        m_systems.RegisterSystem<ECSPhysicsSystem>();
        m_systems.RegisterSystem<HierarchyTransformSystem>();   // first in PreRender: fills WorldTransform
        m_systems.RegisterSystem<RenderExtractSystem>(*m_renderer, m_resources.get());   // second in PreRender: Submit()s meshes
        m_systems.RegisterSystem<DebugDrawSystem>(*m_renderer, m_resources.get());       // PreRender: colliders, velocities, light gizmos, grid, selection
    }

    void Core::CleanUp() {
        // Destroy entities while systems are still alive, so resource-owning
        // OnComponentRemoved/OnEntityDestroyed hooks can still run.
        try { m_world->DestroyAllEntities(); }
        catch (const std::exception& e) { CRU_ERROR("Core::CleanUp: destroying entities failed - {}", e.what()); }

        m_systems.Shutdown();
        // The GPU must be idle before layers free GPU resources (e.g. ImGui's Vulkan objects).
        for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it) (*it)->OnDetach();
        m_layers.clear();

        m_input.Shutdown();
        m_window->ShutDown();
    }
}