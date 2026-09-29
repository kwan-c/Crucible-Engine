#include "Core.h"
#include "AssetPipe/Assetmanager.h"

// STL
#include <algorithm>
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

        // Real frame time, not an assumed 60 FPS: nothing here caps the actual frame rate (no
        // glfwSwapInterval, no sleep/limiter), and the swapchain's present mode is chosen
        // dynamically (VulkanSwapchain::ChoosePresentMode() prefers Mailbox, which does not
        // block the app the way Fifo/vsync does). A hard-coded "1.f / 60.f" here silently
        // assumed the app always ran at exactly 60 FPS - so at whatever *actual* frame rate
        // Mailbox let it run at (which can be very high with an empty-ish scene and no cap),
        // every animation driven by dt (cube spin, the editor fly-camera, ...) played back
        // proportionally too fast or too slow. It happened to look "corrected" after toggling
        // fullscreen because that forces a swapchain recreate (via the resize it causes), and
        // depending on the platform/compositor a fullscreen-negotiated surface can end up stuck
        // on a hard-blocking present mode afterwards even once back in windowed mode - which
        // only ever fixed the SYMPTOM (actual FPS settling near 60) rather than the cause.
        double lastTime = glfwGetTime();

        while (!m_window->ShouldClose()) {

            double const now = glfwGetTime();
            float dt = static_cast<float>(now - lastTime);
            lastTime = now;

            // Clamp so a debugger breakpoint, window drag, or other multi-second stall doesn't
            // make every dt-driven system leap forward by however long the freeze lasted.
            dt = std::min(dt, 1.0f / 15.0f);

            m_window->PollEvents();

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
        m_systems.Shutdown();
        // The GPU must be idle before layers free GPU resources (e.g. ImGui's Vulkan objects).
        for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it) (*it)->OnDetach();
        m_layers.clear();

        m_window->ShutDown();
    }
}