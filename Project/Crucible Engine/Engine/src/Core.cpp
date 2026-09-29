#include "Core.h"
#include "AssetPipe/Assetmanager.h"
#include "Configuration/EngineConfigLoader.h"
#include "Diagnostics/Logger.h"
#include "ECS/ECSPhysicsSystem.h"

// STL
#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <cstdlib>



namespace Crucible {

    Core::Core(const EngineConfig& config) :
        m_config{ config },
        m_window{ new WindowManager(m_config.graphics.windowWidth,m_config.graphics.windowHeight, m_config.graphics.windowTitle) },
        m_renderer{ new Renderer(*m_window) },
        m_world{ std::make_unique<World>() }
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

        RegisterEngineSystems();

        m_systems.Init();

        return true;
    }

    void Core::Run() {

        while (!m_window->ShouldClose()) {

            const float dt = 1.f / 60.f;
            m_window->PollEvents();

            m_world->SetDeltaTime(dt);
            m_world->AdvanceTick();
            m_systems.Update(*m_world);

            for (auto& layer : m_layers) layer->OnUpdate(dt);
            m_renderer->Update(dt);
            //m_window->SwapBuffers();
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

        m_window->ShutDown();
    }
}