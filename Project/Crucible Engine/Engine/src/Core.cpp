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

        // ======== scene / prefab debugging (Removable) ==========
        {
            Crucible::AssetManager assets;
            assets.Init();

            World& world = *m_world;

            const std::filesystem::path scenePath  = assets.Resolve("Scenes/demo.scene");
            const std::filesystem::path prefabPath = assets.Resolve("Prefabs/Crate.prefab");

            Scene scene;

            if (std::filesystem::exists(scenePath))
            {
                // Normal path: the scene already exists, so load it. Loading
                // restores each entity's guid, so a later save produces the
                // same file and the .meta stays untouched.
                scene.Load(world, scenePath);
            }
            else
            {
                // First run only: build a world by hand and write it out.
                const Entity a = world.CreateEntity();
                world.AddComponent<Transform>(a, Transform{ Math::vec3{ 1.f, 2.f, 3.f } });
                world.AddComponent<RigidBody>(a, RigidBody{ Math::vec3{ 0.f, -9.8f, 0.f }, 5.f });

                const Entity b = world.CreateEntity();
                world.AddComponent<Transform>(b, Transform{ Math::vec3{ 7.f, 0.f, 0.f } });
                world.AddComponent<MeshRenderer>(b,
                    MeshRenderer{ false, Math::vec4{ 1.f, 0.5f, 0.25f, 1.f } });

                scene.SetName("demo");
                scene.Save(world, scenePath);
                Prefab::SaveFrom(world, b, prefabPath, "Crate");

                // Both files were created after Init() indexed the folder, so
                // index them now rather than on the next launch.
                assets.Refresh();
            }

            // Stamp two copies of the prefab on top. Additive - nothing that
            // is already in the world is touched.
            Prefab crate;
            if (crate.Load(prefabPath))
            {
                crate.Instantiate(world);
                crate.Instantiate(world);
            }

            std::size_t total = 0;
            for (const Archetype* archetype : world.Archetypes().All())
            {
                if (archetype != nullptr)
                {
                    total += archetype->Entities().size();
                }
            }

            CRU_INFO("[Scene] {} entities live (expect 4: 2 from the scene + 2 prefab copies)",
                     total);
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