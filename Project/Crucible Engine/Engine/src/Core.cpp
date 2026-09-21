#include "Core.h"
#include "AssetPipe/Assetmanager.h"

// STL
#include <iostream>
#include <ECS/ECSPhysicsSystem.h>

namespace Crucible {

    Core::Core() :
        m_window{ new WindowManager(1200, 800, "Crucible Engine") },
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

            float dt = 1.f / 60.f;

            m_window->PollEvents();

            m_systems.Update(*m_world);

            for (auto& layer : m_layers) layer->OnUpdate(dt);
            m_renderer->Update(dt);
            //m_window->SwapBuffers();
        }
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
        m_systems.Shutdown();
        // The GPU must be idle before layers free GPU resources (e.g. ImGui's Vulkan objects).
        if (m_renderer) m_renderer->WaitIdle();
        for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it) (*it)->OnDetach();
        m_layers.clear();
    }
}