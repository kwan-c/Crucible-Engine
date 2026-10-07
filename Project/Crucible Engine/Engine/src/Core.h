#pragma once

#include <GLFW/glfw3.h>

#include <string>
#include <memory>
#include <vector>

#include "EngineExport.h"	// DLL export
#include "Systems/Layer.h"
#include "Systems/PlayMode.h"
#include "Scene/SceneManager.h"
#include "Systems/WindowManager.h"
#include "Graphics/RenderSystem.h"
#include "Input/InputSystem.h"

#include <Configuration/EngineConfig.h>
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

        // Fixes the window's shape to `width`:`height` (e.g. 16, 9) so the game is framed the
        // same on every machine, or frees it again. Meant for the standalone game and its
        // settings menu; the editor leaves the window free. See WindowManager::LockAspectRatio.
        void  LockWindowAspectRatio(int width, int height);
        void  UnlockWindowAspectRatio();
        // Width / height of the locked shape, or 0 when the window is free.
        float GetWindowAspectRatioLock() const;
        // Both valid only after Init(). The engine's shared asset index (AssetManager) and the
        // GUID -> Renderer-handle cache built on top of it (ResourceManager).
        AssetManager&    GetAssets() const { return *m_assets; }
        ResourceManager& GetResources() const { return *m_resources; }

        World& GetWorld() noexcept { return *m_world; }
        const World& GetWorld() const noexcept { return *m_world; }

        // Every ECS system, registered once and alive until shutdown. Systems that declare
        // SystemDesc::SimulationOnly() in Describe() (physics, gameplay) run only while
        // Playing; the rest (transform hierarchy, render extraction, debug draw, audio) run
        // in Edit, Playing and Paused.
        ECSSystemManager& Systems() noexcept { return m_systems; }

        // The engine / gameplay / network event buses (subscribe from a system's Init()).
        EventBuses& Events() noexcept { return m_events; }

        // Opt-in entity/component events on the gameplay bus, e.g. EcsBridge().OnRemoved<MeshRenderer>(...).
        // (Not called "EcsEvents()" - that would hide the EcsEvents namespace inside Core.)
        EcsEventBridge& EcsBridge() noexcept { return *m_ecsEvents; }

        // Open scene: Save/SaveAs now, New/Load deferred to the next frame.
        SceneManager& GetScenes() noexcept { return *m_scenes; }

        // Attaches a layer (calls OnAttach) and takes ownership. Layers are updated in the
        // order pushed, and detached in reverse order when Core shuts down.
        void PushLayer(std::unique_ptr<Layer> layer);

        // ===================================================================
        //  Play mode (Unity's Play / Pause / Stop) - see Systems/PlayMode.h
        // ===================================================================
        //
        // Requests are queued and applied at the start of the next frame, after a pending
        // scene New/Load and before any system runs. Each applied transition is dispatched
        // on the engine bus (EngineEvents::PlayStateChanged), each failure as
        // EngineEvents::PlayRequestFailed. Invalid or repeated requests are ignored (false).
        // During a play session scene New/Load are runtime changes that Stop undoes, and
        // saving is refused.

        PlayState GetPlayState() const noexcept { return m_playMode->State(); }

        // True from Play to Stop (Playing or Paused).
        bool IsInPlaySession() const noexcept { return m_playMode->InSession(); }

        // Edit -> Playing: snapshot the open scene in memory (unsaved edits included), then
        // start a fresh simulation session (physics builds its world from the components).
        // restoreOnStop = false skips the snapshot: Stop then just ends the session and keeps
        // the world (the standalone game does this).
        bool RequestPlay(bool restoreOnStop = true) { return m_playMode->RequestPlay(restoreOnStop); }

        // Playing -> Paused: simulation systems, gameplay event time and runtime audio freeze;
        // editing, input and rendering carry on.
        bool RequestPause() { return m_playMode->RequestPause(); }

        // Paused -> Playing: the same session carries on (nothing is reset).
        bool RequestResume() { return m_playMode->RequestResume(); }

        // Playing/Paused -> Edit: the simulation session ends (physics cleared), runtime
        // events dropped, and the world rebuilt from the Play snapshot (entities, component
        // values, guids, scene name, path and dirty flag). Systems stay alive.
        bool RequestStop() { return m_playMode->RequestStop(); }

        // True while a request is waiting for the next frame (UI can grey its buttons).
        bool HasPendingPlayRequest() const noexcept { return m_playMode->HasPendingRequest(); }

        // Why the last failed request failed; cleared by the next successful transition.
        const std::string& LastPlayError() const noexcept { return m_playMode->LastError(); }

        // The play-mode controller itself.
        PlayMode& GetPlayMode() noexcept { return *m_playMode; }

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

        std::unique_ptr<SceneManager> m_scenes;

        ECSSystemManager m_systems;     // every system, registered once

        // Play mode: state, requests and the simulation switch. Declared after (so destroyed
        // before) the World, the buses and the systems.
        std::unique_ptr<PlayMode> m_playMode;

        // Core systems
        //std::vector<BaseSystem*> m_systems;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
