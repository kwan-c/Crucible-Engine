#include "Core.h"
#include "AssetPipe/Assetmanager.h"
#include "Configuration/EngineConfigLoader.h"
#include "Diagnostics/Logger.h"
#include "ECS/ECSAudioSystem.h"
#include "ECS/ECSPhysicsSystem.h"
#include "ECS/ECSPlayerControllerSystem.h"
#include "ECS/Components/Components.h"
#include "Json/JsonWrapper.h"
#include "Reflection/Serialization.h"
#include "ECS/Archetype/ArchetypeRegistry.h"
#include "ECS/World.h"
#include "Scene/Prefab.h"
#include "Scene/Scene.h"
#include "ECS/HierarchyTransformSystem.h"
#include "Graphics/RenderExtractSystem.h"
#include "Animation/AnimatedModelSystem.h"
#include "Animation/SocketAttachmentSystem.h"
#include "Graphics/DebugDrawSystem.h"
#include "Graphics/ParticleSystem.h"
#include "Graphics/StressMoverSystem.h"   // TEMPORARY: load testing
#include "Resources/ResourceManager.h"
#include "UI/UISystem.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/EntityScriptSystem.h"

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
        m_userSettings{ UserSettingsLoader::Load() },
        m_window{ new WindowManager(static_cast<int>(m_config.graphics.windowWidth), static_cast<int>(m_config.graphics.windowHeight),
                                    m_config.graphics.windowTitle, m_config.graphics.fullScreen, m_config.graphics.maximized) },
        m_renderer{ new Renderer(*m_window) },
        m_world{ std::make_unique<World>() },
        m_ecsEvents{ std::make_unique<EcsEventBridge>(*m_world, m_events.gameplay) },
        m_scenes{ std::make_unique<SceneManager>(*m_world) }
    {
        // Written straight back, so a first run (or a file from an older build) gets every
        // setting with its default: the file then shows the whole format, ready to edit.
        UserSettingsLoader::Save(m_userSettings);

        // Play mode drives the always-on systems too, so it is built once every member exists.
        m_playMode = std::make_unique<PlayMode>(*m_world, *m_scenes, m_events, m_systems);

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

    void  Core::LockWindowAspectRatio(int width, int height) { m_window->LockAspectRatio(width, height); }
    void  Core::UnlockWindowAspectRatio()                    { m_window->UnlockAspectRatio(); }
    float Core::GetWindowAspectRatioLock() const             { return m_window->GetLockedAspectRatio(); }

    void Core::SetFullscreen(bool fullscreen) {
        if (fullscreen == m_window->IsFullscreen()) { return; }
        m_window->SetFullscreen(fullscreen);
        SaveWindowState();
    }

    void Core::MaximizeWindow() { m_window->Maximize(); SaveWindowState(); }
    void Core::RestoreWindow()  { m_window->Restore();  SaveWindowState(); }

    void Core::SaveWindowState() const {
        EngineConfigLoader::SaveWindowState(m_config.app, m_window->IsFullscreen(), m_window->IsMaximized());
    }

    bool Core::Init()
    {
        std::cout << "The Hearth smoulders...\n";
        m_scriptRuntime = std::make_unique<ScriptRuntime>();
        if (!m_scriptRuntime->Initialize(m_config.scripting)) return false;
        if (!m_scriptRuntime->RunStartupEntry(m_config.scripting)) return false;

        // Input attaches before anything else installs GLFW callbacks. ImGui's backend
        // chains to callbacks that are already registered, but anything installed after it
        // replaces ImGui's - which is how its text fields go dead.
        m_input.Init();
        m_input.AttachWindow(m_window->GetNativeHandle());

        // Shared asset index + resource cache. Before RegisterEngineSystems(): the render
        // extraction system resolves MeshRenderer/Skybox asset ids through the ResourceManager.
        m_assets = std::make_unique<AssetManager>();
        // Without an asset index nothing resolves (scenes, meshes, materials): stop rather than run
        // a half-working engine. The mains' RequireAssetRoot() check normally catches this first.
        if (!m_assets->Init()) { CRU_ERROR("[Core] AssetManager found no asset root - cannot start"); return false; }
        m_resources = std::make_unique<ResourceManager>(*m_assets, *m_renderer);
        m_resources->Init();

        // ======== asset debugging (Removable) ==========
        // On the shared index. A second AssetManager here used to run its own reconcile -
        // writing .meta files a second time on every launch - before m_assets even existed.
        //{
        //    std::cout << "\n\n";
        //    m_assets->PrintIndex();

        //    auto tex = m_assets->Load("Textures/Orange_Milk.jpg");
        //    m_assets->PrintLoaded();
        //}
        // ===========================================

        // Startup scene, from EngineConfig.json ("project": { "startupScene" }) so the Editor
        // and the Game open the same one. Empty = start with no scene.
        if (!m_config.project.startupScene.empty())
            m_scenes->RequestLoad(m_assets->Resolve(m_config.project.startupScene));


        // Event buses: report listener failures through the engine log, and let the
        // system manager merge each system's outboxes into them at every stage flush.
        const auto logFailure = [](const std::string& message) { CRU_ERROR("{}", message); };
        m_events.engine.SetFailureLogger(logFailure);
        m_events.gameplay.SetFailureLogger(logFailure);
        m_events.network.SetFailureLogger(logFailure);
        m_systems.SetEventBuses(&m_events);

        RegisterEngineSystems();

        m_systems.Init();

        // The engine starts in Edit: AudioSources stay silent until Play (PlayMode switches it).
        if (ECSAudioSystem* audio = m_systems.FindSystem<ECSAudioSystem>()) audio->SetEditMode(true);

        // Standalone instances retain Core lifetime alongside the separate
        // Play-scoped ScriptComponent instances owned by EntityScriptSystem.
        if (!m_scriptRuntime->CreateConfiguredInstances(m_config.scripting)) return false;

        return true;
    }

    void Core::RequestQuit() {
        if (m_window) { m_window->RequestClose(); }
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

            // The safe point: nothing is iterating the world yet this frame. Scene swaps
            // destroy every entity, and play-mode transitions snapshot, restore and start or
            // end the simulation. The scene request goes first, so Play captures the scene just loaded.
            m_scenes->ApplyPending();
            m_playMode->ApplyRequests();

            // S3 instances follow Core lifetime. This tick excludes entity-owned
            // instances, which EntityScriptSystem schedules through PlayMode.
            if (m_scriptRuntime->IsRunning()) m_scriptRuntime->UpdateInstances(dt);

            m_world->SetDeltaTime(dt);
            m_world->AdvanceTick();
            m_playMode->UpdateSystems();        // every system (SimulationOnly ones only while Playing); each stage delivers its own events

            m_playMode->TickEvents(dt);         // delayed events whose time has come get queued (gameplay time only while Playing)

            for (auto& layer : m_layers) layer->OnUpdate(dt);
            m_renderer->Update(dt);
        }
        // Entity teardown precedes GPU shutdown as well as world destruction.
        if (auto* scripts = m_systems.FindSystem<EntityScriptSystem>()) scripts->StopAll();
        m_renderer->ShutDown();
    }

   
    void Core::PushLayer(std::unique_ptr<Layer> layer) {
        layer->OnAttach();
        m_layers.push_back(std::move(layer));
    }


    void Core::RegisterEngineSystems()
    {
        m_systems.RegisterSystem<EntityScriptSystem>(*m_scriptRuntime, *m_world, *m_playMode, *m_scenes);
        //sample system registeration
        m_systems.RegisterSystem<UIMenuSystem>();                                        // PreUpdate, Playing only: Escape opens / closes UICanvas::toggleWithEscape menus and frees / locks the cursor
        m_systems.RegisterSystem<UIInputSystem>(*m_renderer, m_scenes.get(), m_assets.get())                             // PreUpdate, Playing only: mouse -> UI buttons (states, UIEvents::ButtonClicked, Input::IsPointerOverUI) before gameplay reads input
            .ReactiveTo<UIButton, UISlider>();                                           // switched off mid-press / drag: cancelled
        m_systems.RegisterSystem<ECSPlayerControllerSystem>(m_userSettings.input)                           // Update, Playing only: keyboard -> CharacterController, before physics moves it; the main CameraComponent sets "forward"
            .ReactiveTo<PlayerController>();                                             // switched off: stop the mech
        m_systems.RegisterSystem<ECSPhysicsSystem>(m_scenes.get())                      // the scene turns a PhysicsJoint's saved connected body back into an entity
            .ReactiveTo<CharacterController>();                                          // switched back on: no stale fall
        m_systems.RegisterSystem<StressMoverSystem>();          // TEMPORARY, Update: walks StressMover entities in circles (load testing)
        m_systems.RegisterSystem<HierarchyTransformSystem>();   // after CPU poses/socket preparation: fills WorldTransform
        auto& animator = m_systems.RegisterSystem<AnimatedModelSystem>(*m_resources, *m_playMode, m_world.get());
        if (m_scriptRuntime->IsRunning()) m_scriptRuntime->SetAnimatorBinding(&animator);
        m_systems.RegisterSystem<SocketAttachmentSystem>(*m_resources);
        m_systems.RegisterSystem<RenderExtractSystem>(*m_renderer, m_resources.get());   // second in PreRender: Submit()s meshes
        m_systems.RegisterSystem<ParticleSystem>(*m_renderer, m_resources.get());        // PreRender: simulates ParticleEmitters, submits their particles and lights
        m_systems.RegisterSystem<DebugDrawSystem>(*m_renderer, m_resources.get());       // PreRender: colliders, velocities, light gizmos, grid, selection
        m_systems.RegisterSystem<UIRenderSystem>(*m_renderer, m_resources.get());        // PreRender, Edit mode too: lays out screen-space UICanvases against the game screen and SubmitUI()s them
        m_systems.RegisterSystem<ECSAudioSystem>(*m_assets, m_userSettings.audio, m_renderer);                // PreRender: owns the FMOD audio engine (editor previews too); m_assets resolves clips, the renderer's camera is the listener until the camera is an entity. PlayMode switches its edit/pause modes
    }

    void Core::CleanUp() {
        if (auto* scripts = m_systems.FindSystem<EntityScriptSystem>()) scripts->StopAll();
        // Run managed teardown while Core's resources/systems are still alive.
        if (m_scriptRuntime && m_scriptRuntime->IsRunning()) m_scriptRuntime->DestroyAllInstances();
        if (m_scriptRuntime && m_scriptRuntime->IsRunning()) m_scriptRuntime->SetAnimatorBinding(nullptr);
        // Destroy entities while systems are still alive, so resource-owning
        // OnComponentRemoved/OnEntityDestroyed hooks can still run.
        try { m_world->DestroyAllEntities(); }
        catch (const std::exception& e) { CRU_ERROR("Core::CleanUp: destroying entities failed - {}", e.what()); }

        // A play session still running at exit just ends (physics session first), nothing
        // is restored or saved; then every system shuts down.
        m_playMode->Shutdown();
        m_systems.Shutdown();
        // The GPU must be idle before layers free GPU resources (e.g. ImGui's Vulkan objects).
        for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it) (*it)->OnDetach();
        m_layers.clear();
        // Managed instances/bindings added later must be released before final cleanup.
        // Play/Stop never destroys or restarts the process-wide Mono runtime.
        if (m_scriptRuntime) m_scriptRuntime->Shutdown();

        // Remember how the window was left (fullscreen / maximized) for the next launch.
        SaveWindowState();

        m_input.Shutdown();
        m_window->ShutDown();
    }
}
