#include "Core.h"
#include <algorithm>
#include "AssetPipe/Assetmanager.h"
#include "Configuration/EngineConfigLoader.h"
#include "Diagnostics/Logger.h"
#include "ECS/ECSAudioSystem.h"
#include "ECS/ECSPhysicsSystem.h"
#include "ECS/ECSPlayerControllerSystem.h"
#include "ECS/ECSNavigationSystem.h"
#include "ECS/ECSStateGraphSystem.h"
#include "ECS/ECSTerrainSystem.h"
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
#include "Text/WorldTextSystem.h"
#include "Graphics/StressMoverSystem.h"   // TEMPORARY: load testing
#include "Graphics/VineSystem.h"
#include "Resources/ResourceManager.h"
#include "UI/UISystem.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/EntityScriptSystem.h"
#include "Network/GamePrefabs.h"
#include "Network/GnsTransport.h"
#include "Network/NetContent.h"
#include "Network/NetIdentityHooks.h"
#include "Network/NetworkSession.h"
#include "Network/NetworkSettings.h"
#include "Network/Systems/NetReceiveSystem.h"
#include "Network/Systems/NetReplicationSendSystem.h"
#include "Network/Systems/LocalPlayerInputSystem.h"
#include "Network/Systems/NetInputSystem.h"
#include "Network/Systems/NetInterpolationSystem.h"

// STL
#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
// winmm's timer-resolution calls, declared here so Core.cpp does not pull in <windows.h> and its
// macros. Same signatures as <timeapi.h> (MMRESULT is a UINT, WINAPI is __stdcall).
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int uPeriod);
extern "C" __declspec(dllimport) unsigned int __stdcall timeEndPeriod(unsigned int uPeriod);
#pragma comment(lib, "winmm.lib")
#endif

namespace {

    // Windows sleeps in steps of the system timer (often 15.6 ms), far too coarse to pace a
    // 60 fps frame. This asks for 1 ms steps for as long as the main loop runs.
    struct FrameTimerResolution {
#ifdef _WIN32
        FrameTimerResolution()  { timeBeginPeriod(1); }
        ~FrameTimerResolution() { timeEndPeriod(1); }
#else
        FrameTimerResolution()  = default;
#endif
        FrameTimerResolution(const FrameTimerResolution&) = delete;
        FrameTimerResolution& operator=(const FrameTimerResolution&) = delete;
    };

    // Returns at `deadline` (glfwGetTime seconds). Sleeps for most of the wait and spins only the
    // last ~1.5 ms, since a sleep can wake a little late but a spin cannot.
    void WaitUntil(double deadline) {
        for (;;) {
            const double remaining = deadline - glfwGetTime();
            if (remaining <= 0.0) { return; }
            if (remaining > 0.002) { std::this_thread::sleep_for(std::chrono::duration<double>(remaining - 0.0015)); }
            else                   { std::this_thread::yield(); }
        }
    }

}   // anonymous namespace



namespace Crucible {

    Core::Core(const EngineConfig& config) :
        m_config{ config },
        m_userSettings{ UserSettingsLoader::Load() },
        m_window{ new WindowManager(static_cast<int>(m_config.graphics.windowWidth), static_cast<int>(m_config.graphics.windowHeight),
                                    m_config.graphics.windowTitle, m_config.graphics.fullScreen, m_config.graphics.maximized,
                                    static_cast<int>(m_config.graphics.fullscreenWidth), static_cast<int>(m_config.graphics.fullscreenHeight),
                                    static_cast<int>(m_config.graphics.fullscreenRefreshRate)) },
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

        // The player's graphics quality. Only the game starts with it: in the editor the Scene
        // View panel owns the renderer's settings, and a script's changes are undone at Stop.
        if (m_config.app == AppKind::Game) { ApplyGraphicsSettings(); }
        m_playMode->OnBeforeStop([this] { RestoreEditorRenderSettings(); });

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

    void Core::SetWindowedSize(int width, int height) {
        m_window->SetWindowedSize(width, height);
        SaveWindowState();
    }

    bool Core::SetFullscreenResolution(int width, int height, int refreshRate) {
        if (!m_window->SetFullscreenResolution(width, height, refreshRate)) { return false; }
        SaveWindowState();
        return true;
    }

    std::vector<WindowResolution> Core::GetScreenResolutions() const { return m_window->GetResolutions(); }

    void Core::SetGraphicsSettings(const GraphicsSettings& settings) {
        // First change in the editor: remember the Scene View panel's values for Stop.
        if (m_config.app == AppKind::Editor && !m_editorRenderSnapshot) {
            m_editorRenderSnapshot = RenderSettingsSnapshot{ m_renderer->GetPostSettings(), m_renderer->GetShadowSettings(), m_renderer->GetVSync() };
        }
        m_userSettings.graphics = settings;
        m_userSettings.graphics.quality = MatchQualityLevel(settings);
        ApplyGraphicsSettings();
    }

    void Core::ApplyGraphicsSettings() {
        const GraphicsSettings& g = m_userSettings.graphics;

        // Only the fields the player controls; the rest (bloom threshold, toon shading, ...) are
        // left as they are. The renderer clamps MSAA to the GPU and rebuilds its targets itself.
        PostSettings& post = m_renderer->GetPostSettings();
        post.fxaa          = g.antiAliasing != AntiAliasing::Off;
        post.msaaSamples   = g.antiAliasing == AntiAliasing::MSAA4x ? 4u
                           : g.antiAliasing == AntiAliasing::MSAA2x ? 2u : 1u;
        post.ao            = g.ambientOcclusion;
        post.bloom         = g.bloom;
        post.volumetricFog = g.volumetricFog;
        post.brightness          = ClampBrightness(g.brightness);
        post.fovOffset           = ClampFovOffset(g.fovOffset);
        post.vignette            = g.vignette;
        post.filmGrain           = g.filmGrain;
        post.chromaticAberration = g.chromaticAberration;

        // Cascades are the cost (each re-renders the casters); distance trades reach for
        // sharpness. High = the ShadowSettings defaults.
        ShadowSettings& shadows = m_renderer->GetShadowSettings();
        shadows.enabled = g.shadowQuality != ShadowQuality::Off;
        switch (g.shadowQuality) {
        case ShadowQuality::Off:    break;
        case ShadowQuality::Low:    shadows.cascadeCount = 1; shadows.distance = 40.0f;  break;
        case ShadowQuality::Medium: shadows.cascadeCount = 2; shadows.distance = 60.0f;  break;
        case ShadowQuality::High:   shadows.cascadeCount = 3; shadows.distance = 80.0f;  break;
        case ShadowQuality::Ultra:  shadows.cascadeCount = std::min(4u, kMaxShadowCascades); shadows.distance = 120.0f; break;
        }

        // Rebuilds the swapchain only if it changed. The frame-rate limit is read by
        // Run's frame cap every frame.
        m_renderer->SetVSync(g.vsync);
    }

    void Core::RestoreEditorRenderSettings() {
        if (!m_editorRenderSnapshot) { return; }
        m_renderer->GetPostSettings()   = m_editorRenderSnapshot->post;
        m_renderer->GetShadowSettings() = m_editorRenderSnapshot->shadows;
        m_renderer->SetVSync(m_editorRenderSnapshot->vsync);
        m_editorRenderSnapshot.reset();
    }

    void Core::SaveWindowState() const {
        SavedWindowState state;
        state.fullScreen            = m_window->IsFullscreen();
        state.maximized             = m_window->IsMaximized();
        state.windowWidth           = static_cast<std::uint32_t>(std::max(m_window->GetWindowedWidth(), 0));
        state.windowHeight          = static_cast<std::uint32_t>(std::max(m_window->GetWindowedHeight(), 0));
        state.fullscreenWidth       = static_cast<std::uint32_t>(m_window->GetFullscreenWidth());
        state.fullscreenHeight      = static_cast<std::uint32_t>(m_window->GetFullscreenHeight());
        state.fullscreenRefreshRate = static_cast<std::uint32_t>(m_window->GetFullscreenRefreshRate());
        EngineConfigLoader::SaveWindowState(m_config.app, state);
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
        m_scenes->SetResources(m_resources.get());   // scenes by GUID load through the ResourceManager
        // After every scene change (new, open, a game's LoadScene button) free what the new scene
        // no longer uses. It reads the scene as it is in memory, so unsaved edits count too.
        m_scenes->OnSceneChanged([this] {
            SceneManager::Snapshot snapshot;
            if (m_scenes->CaptureSnapshot(snapshot)) { m_resources->UnloadUnused(snapshot.scene.document); }
        });

        // Startup scene, from EngineConfig.json ("project": { "startupScene" }) so the Editor
        // and the Game open the same one. Empty = start with no scene.
        // The config names the file (a path is what people edit by hand); it is turned into the
        // scene's GUID here and loaded through the ResourceManager, like every other scene load.
        // A file not in the asset index yet (no .meta) is still opened by path.
        if (!m_config.project.startupScene.empty())
        {
            const AssetId startup = m_resources->GetIdForPath(m_config.project.startupScene);
            if (startup == 0 || !m_scenes->RequestLoadAsset(startup))
                m_scenes->RequestLoad(m_assets->Resolve(m_config.project.startupScene));
        }


        // Event buses: report listener failures through the engine log, and let the
        // system manager merge each system's outboxes into them at every stage flush.
        const auto logFailure = [](const std::string& message) { CRU_ERROR("{}", message); };
        m_events.engine.SetFailureLogger(logFailure);
        m_events.gameplay.SetFailureLogger(logFailure);
        m_events.network.SetFailureLogger(logFailure);
        m_systems.SetEventBuses(&m_events);

        // Networking: the session exists for the whole run but stays Offline (no sockets) unless
        // this instance's settings say Host or Client. Then it starts when Play starts and stops
        // when Play stops; Pause keeps it running.
        m_network = std::make_unique<NetworkSession>(m_events);
        {
            // The shared defaults from EngineConfig.json; the editor's Network panel may change them
            // for this instance (in memory) before Play.
            std::string configWarning;
            m_network->SetSettings(FromEngineConfig(m_config.network, configWarning));
            if (!configWarning.empty()) CRU_WARN("[Net] EngineConfig.json: {}", configWarning);
            const NetworkSettings& net = m_network->Settings();
            CRU_INFO("[Net] defaults from EngineConfig.json: mode {}, host port {}, listen on {}, client joins {}:{}, max clients {}",
                ToString(net.mode), net.hostPort, net.listenAddress.empty() ? "all interfaces" : net.listenAddress,
                net.connectAddress, net.connectPort, net.maxClients);
        }
        // The session's NetworkId -> entity map follows every NetworkIdentity add and remove (World
        // hooks, registered once; the session outlives every entity of the World).
        RegisterIdentityHooks(*m_world, *m_network);

        // What both machines must agree on: the serializers and the game's network prefabs
        // (GamePrefabs.h). A registration mistake is reported now and refuses every session.
        m_netContent = std::make_unique<NetContent>();
        m_network->SetPrefabCheck([content = m_netContent.get()](Net::NetPrefabId id) { return content->Prefabs().Desc(id) != nullptr; });   // for RequestSpawn
        std::string netRegistrationError;
        if (!RegisterGameNetPrefabs(m_netContent->Prefabs(), netRegistrationError))
            CRU_ERROR("[Net] network prefab registration: {}", netRegistrationError);

        // Play starts the session; Stop ends it BEFORE the world is restored (OnBeforeStop), so
        // tearing down the play world announces nothing to the other players. Before a Host or
        // Client start, the prefab files are read and validated again and the content hash is
        // recomputed: an invalid table refuses the start (the editor's Network panel shows why).
        m_networkPlayState = m_events.engine.Subscribe<EngineEvents::PlayStateChanged>([this, netRegistrationError](const EngineEvents::PlayStateChanged& e) {
            if (e.from == PlayState::Edit && e.to == PlayState::Playing)
            {
                if (m_network->Settings().mode != NetMode::Offline)
                {
                    std::string contentError = netRegistrationError;
                    if (contentError.empty() && m_netContent->Prepare(*m_world, *m_assets, *m_resources, contentError))
                        m_network->SetContentHash(m_netContent->Hash());
                    m_network->SetContentError(contentError);
                }
                std::string error;
                if (!m_network->Start(error)) CRU_ERROR("[Net] could not start the {} session: {}", ToString(m_network->Settings().mode), error);
            }
        });
        m_playMode->OnBeforeStop([this] { m_network->Stop("Play stopped"); });

        // v1 rule: a scene change during a session ends it (clients cannot follow a level switch
        // yet). This runs inside SceneManager::ApplyPending, before any system of the frame.
        m_scenes->OnSceneChanged([this] {
            if (m_network->IsActive()) m_network->Stop("the scene changed during the session");
        });

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

        // Frame pacing: EngineConfig "graphics" targetFPS, and backgroundFPS while the window is
        // minimized or unfocused (see EngineConfig.h). Without it the loop runs as fast as the GPU
        // can draw, which pins the GPU at 100% whatever the scene costs.
        const FrameTimerResolution timerResolution;
        const auto frameCap = [this](bool minimized) -> uint32_t {
            // The player's limit (options menu) wins over the team's targetFPS when set.
            // The editor only uses it while a script has changed the settings in Play.
            const bool playerLimit = m_config.app == AppKind::Game || m_editorRenderSnapshot.has_value();
            const int32_t limit = playerLimit ? m_userSettings.graphics.frameRateLimit : kFrameRateDefault;
            const uint32_t target = limit == kFrameRateDefault ? m_config.graphics.targetFPS : static_cast<uint32_t>(limit);
            const bool background = minimized || !m_window->IsFocused();
            const bool networked  = m_network && m_network->IsActive();
            uint32_t cap = target;
            if (background && !networked) {
                const uint32_t bg = m_config.graphics.backgroundFPS;
                if (bg > 0 && (cap == 0 || bg < cap)) { cap = bg; }
            }
            // A minimized window draws nothing, so an uncapped loop would only spin the CPU.
            if (minimized && cap == 0) { cap = 60; }
            return cap;
        };

        double lastTime = glfwGetTime();
        double nextFrame = lastTime;   // when the next frame may start (frame pacing)

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

            // Minimized: nothing can be seen, so nothing is drawn. Everything else above still
            // runs, so a network session or a script keeps ticking.
            const bool minimized = m_window->IsMinimized();
            m_renderer->SetDrawSuspended(minimized);
            m_renderer->Update(dt);

            // Wait out the rest of this frame. The schedule advances by whole periods so the
            // average rate is exact; if the loop has fallen more than a frame behind (a slow
            // frame, a breakpoint, the cap just changed) it restarts from now instead of racing
            // to catch up.
            if (const uint32_t cap = frameCap(minimized); cap > 0) {
                const double period = 1.0 / static_cast<double>(cap);
                nextFrame += period;
                const double after = glfwGetTime();
                if (after - nextFrame > period || nextFrame - after > period) { nextFrame = after; }
                WaitUntil(nextFrame);
            } else {
                nextFrame = glfwGetTime();
            }
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
        m_systems.RegisterSystem<NetReceiveSystem>(*m_network, *m_netContent, *m_scenes)             // PreUpdate, first, every state: runs the network session, host spawns, client proxies (Offline: nothing)
            .SetSceneAssetResolver([this](const std::filesystem::path& scene) -> std::uint64_t {    // scene objects: the loaded scene's asset GUID, compared on join
                const std::filesystem::path relative = std::filesystem::absolute(scene).lexically_normal().lexically_relative(std::filesystem::absolute(m_assets->GetAssetRoot()).lexically_normal());
                const std::u8string text = relative.generic_u8string();
                return text.empty() ? 0 : m_assets->GetGuid(std::string(text.begin(), text.end())).m_Value;
            });
        m_systems.RegisterSystem<EntityScriptSystem>(*m_scriptRuntime, *m_world, *m_playMode, *m_scenes);
        m_systems.RegisterSystem<UIMenuSystem>();                                        // PreUpdate, Playing only: Escape opens / closes UICanvas::toggleWithEscape menus and frees / locks the cursor
        m_systems.RegisterSystem<UIInputSystem>(*m_renderer, m_scenes.get())                             // PreUpdate, Playing only: mouse -> UI buttons (states, UIEvents::ButtonClicked, Input::IsPointerOverUI) before gameplay reads input
            .ReactiveTo<UIButton, UISlider>();                                           // switched off mid-press / drag: cancelled
        // PreUpdate, after the UI, every state: this machine's keys, mouse and pad -> its players' PlayerInput,
        // cursor lock, camera pivot. Neutral input while Paused, and while the last argument says something
        // else owns the input: the in-game menu (drop it to let players walk with the menu open).
        m_systems.RegisterSystem<LocalPlayerInputSystem>(m_userSettings.input, m_network.get(), *m_playMode,
            [] { return UI::IsMenuOpen(); });
        // PreUpdate, after the sampler, every state: a client's commands, acks and heartbeat to the host; on the
        // host, each client's commands into its players' PlayerInput (neutral after ~270 ms without one), and a
        // player spawned per peer: the network prefab whose NetworkObjectAuthoring has spawnPerPlayer (none: no
        // auto-spawn). Spawn points: the last argument, else where the scene placed that prefab (removed in a session), else 3 m apart along +X.
        m_systems.RegisterSystem<NetInputSystem>(*m_network, *m_netContent, Net::kInvalidNetPrefab);
        m_systems.RegisterSystem<ECSPlayerControllerSystem>()                                               // Update, Playing only: PlayerInput -> CharacterController, before physics moves it; the mech faces its camera
            .ReactiveTo<PlayerController>();                                             // switched off: stop the mech
        m_systems.RegisterSystem<ECSStateGraphSystem>(*m_resources, *m_playMode, *m_scriptRuntime);   // Update, Playing only, before navigation: each StateGraphAgent's state graph (its trees run in C#) gives its FlowFieldAgent orders
        m_systems.RegisterSystem<ECSNavigationSystem>(*m_playMode, m_systems);          // Update, Edit too: bakes the FlowFieldSurface's grid (Inspector Bake, and on Play) and draws it; Playing: flow field + agents (finds physics in m_systems for collision)
        m_systems.RegisterSystem<ECSPhysicsSystem>(m_scenes.get())                      // the scene turns a PhysicsJoint's saved connected body back into an entity
            .ReactiveTo<CharacterController>();                                          // switched back on: no stale fall
        // PostUpdate, after physics, every state: the host's world, spawn, despawn and snapshot messages. The
        // network tick counts the fixed steps physics really ran: only while Playing, because physics is
        // SimulationOnly and its GetStepsLastFrame() keeps the last playing frame's value while paused.
        m_systems.RegisterSystem<NetReplicationSendSystem>(*m_network, *m_netContent,
            [physics = m_systems.FindSystem<ECSPhysicsSystem>(), play = m_playMode.get()]() -> std::uint32_t {
                return physics != nullptr && play->State() == PlayState::Playing ? physics->GetStepsLastFrame() : 0u;
            });
        m_systems.RegisterSystem<StressMoverSystem>();          // TEMPORARY, Update: walks StressMover entities in circles (load testing)
        m_systems.RegisterSystem<NetInterpolationSystem>(*m_network);   // PreRender, before the hierarchy, client only: proxies drawn 100 ms behind the host, between snapshots
        m_systems.RegisterSystem<HierarchyTransformSystem>();   // after CPU poses/socket preparation: fills WorldTransform
        auto& animator = m_systems.RegisterSystem<AnimatedModelSystem>(*m_resources, *m_playMode, m_world.get());
        m_systems.RegisterSystem<SocketAttachmentSystem>(*m_resources);
        m_systems.RegisterSystem<ECSTerrainSystem>(*m_renderer);                          // PreRender, Edit too, before RenderExtract: each Terrain's mesh onto its MeshRenderer (rebuilt only when its grid changes)
        m_systems.RegisterSystem<RenderExtractSystem>(*m_renderer, m_resources.get());   // second in PreRender: Submit()s meshes
        m_systems.RegisterSystem<ParticleSystem>(*m_renderer, m_resources.get(), &m_systems);   // PreRender: simulates ParticleEmitters, submits their particles and lights; m_systems: Play vs Edit
        m_systems.RegisterSystem<VineSystem>(*m_renderer, m_resources.get());            // PreRender: grows/simulates Vines (proof of concept), submits their meshes
        m_systems.RegisterSystem<WorldTextSystem>(*m_renderer, m_resources.get());       // PreRender, Edit mode too: lays out WorldText components and submits their glyphs
        m_systems.RegisterSystem<DebugDrawSystem>(*m_renderer, m_resources.get());       // PreRender: colliders, velocities, light gizmos, grid, selection
        m_systems.RegisterSystem<UIRenderSystem>(*m_renderer, m_resources.get());        // PreRender, Edit mode too: lays out screen-space UICanvases against the game screen and SubmitUI()s them
        m_systems.RegisterSystem<WorldCanvasSystem>(*m_renderer, m_resources.get());     // PreRender, Edit mode too: lays out world-space UICanvases and submits each as a world UI panel
        m_systems.RegisterSystem<ECSAudioSystem>(*m_resources, m_userSettings.audio, m_renderer);                // PreRender: owns the FMOD audio engine (editor previews too); m_assets resolves clips, the renderer's camera is the listener until the camera is an entity. PlayMode switches its edit/pause modes

        // The C# bindings' engine objects, all at once now every system exists. A new binding is one line here.
        if (m_scriptRuntime->IsRunning()) {
            auto* physics = m_systems.FindSystem<ECSPhysicsSystem>();
            m_scriptRuntime->ConnectEngine({
                .animator     = &animator,                                   // C# Animator
                .world        = m_world.get(),                               // Transform, Entity, Prefab, components
                .play         = m_playMode.get(),
                .scenes       = m_scenes.get(),
                .assets       = m_assets.get(),
                .userSettings = &m_userSettings,                             // Audio bus volumes, InputSettings
                .physics      = physics,                                     // Physics.Raycast (entity queries: ignores the shooter properly)
                .renderer     = m_renderer,                                  // Camera screen maths
                .particles    = m_systems.FindSystem<ParticleSystem>(),      // ParticleEmitter Emit / Restart / ParticleCount
                .events       = &m_events,                                   // UI and collision callbacks
                .core         = this,                                        // Screen (window mode; ignored in the editor)
                .resources    = m_resources.get(),                           // MeshRenderer material slots
            });
        }
    }

    void Core::CleanUp() {
        // A multiplayer session ends first: clients are told, and nothing networked runs during teardown.
        if (m_network) m_network->Stop("engine shutting down");
        if (auto* scripts = m_systems.FindSystem<EntityScriptSystem>()) scripts->StopAll();
        // Run managed teardown while Core's resources/systems are still alive.
        if (m_scriptRuntime && m_scriptRuntime->IsRunning()) m_scriptRuntime->DestroyAllInstances();
        if (m_scriptRuntime && m_scriptRuntime->IsRunning()) m_scriptRuntime->DisconnectEngine();   // every C# binding
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

        // GameNetworkingSockets stops last among the engine's own subsystems (a no-op if no session ever started).
        GnsTransport::KillLibrary();

        // Remember how the window was left (fullscreen / maximized) for the next launch.
        SaveWindowState();

        m_input.Shutdown();
        m_window->ShutDown();
    }
}
