#pragma once

#include <GLFW/glfw3.h>

#include <string>
#include <memory>
#include <optional>
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
#include <Configuration/UserSettings.h>
#include "AssetPipe/Assetmanager.h"
#include "Resources/ResourceManager.h"

namespace Crucible
{
    class ScriptRuntime;
    class NetworkSession;
    class NetContent;

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

        // The settings this Core was started with (see EngineConfigLoader::LoadForApp).
        const EngineConfig& GetConfig() const noexcept { return m_config; }

        // The PLAYER's settings (volumes, look sensitivity, invert Y), from UserSettings.json
        // next to the .exe - see Configuration/UserSettings.h. An options menu edits them here:
        // the audio and player-controller systems read them every frame, so a slider is heard
        // and felt straight away. SaveUserSettings writes them to disk (when the player
        // confirms, so dragging a slider doesn't write the file every frame).
        UserSettings& GetUserSettings() noexcept { return m_userSettings; }
        const UserSettings& GetUserSettings() const noexcept { return m_userSettings; }
        bool SaveUserSettings() const { return UserSettingsLoader::Save(m_userSettings); }

        // The player's graphics quality (UserSettings::graphics, C# QualitySettings). Set stores
        // them and applies them to the renderer at once (next frame); SaveUserSettings keeps them.
        // The standalone game applies the saved ones at startup. The EDITOR does not - its Scene
        // View panel owns the renderer's settings - but a script may still change them while
        // testing an options menu in Play: Stop then puts the editor's own values back.
        const GraphicsSettings& GetGraphicsSettings() const noexcept { return m_userSettings.graphics; }
        void SetGraphicsSettings(const GraphicsSettings& settings);

        ScriptRuntime& GetScriptRuntime() const noexcept { return *m_scriptRuntime; }

        // Fixes the window's shape to `width`:`height` (e.g. 16, 9) so the game is framed the
        // same on every machine, or frees it again. Meant for the standalone game and its
        // settings menu; the editor leaves the window free. See WindowManager::LockAspectRatio.
        void  LockWindowAspectRatio(int width, int height);
        void  UnlockWindowAspectRatio();
        // Width / height of the locked shape, or 0 when the window is free.
        float GetWindowAspectRatioLock() const;

        // Window mode. F11 or Alt+Enter toggle fullscreen in both apps. Every change is saved to
        // UserSettings.json next to the .exe (also at shutdown, which catches the title bar's
        // maximize button), so the next launch opens the same way. See WindowManager.
        void SetFullscreen(bool fullscreen);
        void ToggleFullscreen() { SetFullscreen(!IsFullscreen()); }
        bool IsFullscreen() const { return m_window->IsFullscreen(); }
        void MaximizeWindow();
        void RestoreWindow();      // un-maximize
        bool IsWindowMaximized() const { return m_window->IsMaximized(); }

        // Windowed size and fullscreen resolution (both saved to UserSettings.json, like the mode).
        // See WindowManager::SetWindowedSize / SetFullscreenResolution.
        void SetWindowedSize(int width, int height);
        int  GetWindowedWidth() const { return m_window->GetWindowedWidth(); }
        int  GetWindowedHeight() const { return m_window->GetWindowedHeight(); }
        // 0 x 0 = borderless (the monitor's own). False for a size the monitor does not offer.
        bool SetFullscreenResolution(int width, int height, int refreshRate = 0);
        int  GetFullscreenWidth() const { return m_window->GetFullscreenWidth(); }
        int  GetFullscreenHeight() const { return m_window->GetFullscreenHeight(); }
        int  GetFullscreenRefreshRate() const { return m_window->GetFullscreenRefreshRate(); }
        // The window's monitor's fullscreen sizes, smallest first.
        std::vector<WindowResolution> GetScreenResolutions() const;
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

        // The multiplayer session (Network/NetworkSession.h). Offline unless this instance's
        // NetworkSettings say Host or Client; then it starts when Play starts and stops at Stop.
        // The settings start as EngineConfig.json's "network" defaults; the editor's Network panel
        // changes them for this instance only with GetNetwork().SetSettings(...), before Play.
        NetworkSession& GetNetwork() noexcept { return *m_network; }

        // What two machines must agree on to play together: the serializers and the network
        // prefabs (Network/NetContent.h). The game registers its prefabs in Network/GamePrefabs.h;
        // the table is validated and hashed again before every Host or Client start.
        NetContent& GetNetContent() noexcept { return *m_netContent; }

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

        // Quits: the main loop ends after the current frame and the engine shuts down normally,
        // as if the window's X was clicked. For the standalone game (a UI "Quit" button); the
        // editor intercepts window closes (unsaved-changes prompt) and doesn't use this.
        void RequestQuit();

    private:
        EngineConfig m_config;
        // Declared before the systems that hold a pointer into it, so it outlives them.
        UserSettings m_userSettings;
        void RegisterEngineSystems();

        // Writes the window's current mode to UserSettings.json (for m_config.app).
        void SaveWindowState() const;

        // UserSettings::graphics -> the renderer's PostSettings / ShadowSettings.
        void ApplyGraphicsSettings();
        // Editor only: the renderer's settings from before a script first changed them, put back
        // at Stop (RestoreEditorRenderSettings). Empty while no script has changed them.
        struct RenderSettingsSnapshot { PostSettings post; ShadowSettings shadows; };
        std::optional<RenderSettingsSnapshot> m_editorRenderSnapshot;
        void RestoreEditorRenderSettings();

        void CleanUp();

        WindowManager* m_window;
        Renderer* m_renderer;
        Input m_input;      // owns the GLFW input callbacks; queried statically, e.g. Input::IsDown
        std::vector<std::unique_ptr<Layer>> m_layers;

        // Declared BEFORE the World: members are destroyed in reverse order, so the buses
        // outlive the World and every system (the ECS bridge's World hooks point at a bus).
        EventBuses m_events;

        // Created in Init(); declared after the buses it posts to and before the systems that
        // hold a reference to it, so it outlives them. m_networkPlayState starts and stops it.
        std::unique_ptr<NetworkSession> m_network;
        std::unique_ptr<NetContent>     m_netContent;   // created in Init(), next to the session
        Subscription m_networkPlayState;

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
        std::unique_ptr<ScriptRuntime> m_scriptRuntime;

        // Core systems
        //std::vector<BaseSystem*> m_systems;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
