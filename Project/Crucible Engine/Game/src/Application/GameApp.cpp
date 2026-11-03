#include "GameApp.h"

#include "Diagnostics/Logger.h"
#include "UI/UIEvents.h"

bool GameApp::Init()
{
    if (!m_engine->Init())
    {
        // std::cout << "Failed to initialize Core Engine...\n";
        return false;
    }

    // Keep the window the shape the game is framed for ("project": { "aspectRatio" } in
    // EngineConfig.json - the Editor's Game view uses the same value), so every player sees the
    // same view. A settings menu can call this again with another ratio, or UnlockWindowAspectRatio().
    const Crucible::ProjectConfig& project = m_engine->GetConfig().project;
    if (project.AspectRatio() > 0.0f)
        m_engine->LockWindowAspectRatio(static_cast<int>(project.aspectWidth), static_cast<int>(project.aspectHeight));

    // Register and initialize game systems...
    // Gameplay systems: m_engine->Systems().RegisterSystem<T>(...), with
    // desc.SimulationOnly() in their Describe() so they run only while playing.

    // The game is always playing: start the session at the first frame (after the
    // startup scene loads). No snapshot - there is no editor to go back to.
    m_engine->RequestPlay(/*restoreOnStop*/ false);

    SubscribeToUI();

    // UI button actions (onClick = Custom, actionId = the name). Examples:
    //     RegisterAction("OpenSettings", [](Crucible::Entity) { /* show the settings canvas */ });
    //     RegisterAction("Credits",      [](Crucible::Entity) { /* ... */ });

    return true;
}

void GameApp::RegisterAction(const std::string& actionId, std::function<void(Crucible::Entity)> action)
{
    m_actions[actionId] = std::move(action);
    m_warnedActions.erase(actionId);
}

void GameApp::SubscribeToUI()
{
    Crucible::EventBus& bus = m_engine->Events().engine;

    // A "Quit" button: close the window; Core::Run's loop ends after this frame and shuts down normally.
    m_onQuit = bus.Subscribe<Crucible::UIEvents::QuitRequested>([this](const Crucible::UIEvents::QuitRequested&) {
        CRU_INFO("[Game] Quit button pressed - closing");
        m_engine->RequestQuit();
    });

    // A "Custom" button: run the action registered under its actionId.
    m_onCustom = bus.Subscribe<Crucible::UIEvents::CustomAction>([this](const Crucible::UIEvents::CustomAction& click) {
        auto it = m_actions.find(click.actionId);
        if (it != m_actions.end() && it->second) {
            it->second(click.button);
        } else if (m_warnedActions.insert(click.actionId).second) {
            CRU_USER_WARN("[Game] UI button (entity {}) asks for action '{}', but no action has that name "
                          "(GameApp::RegisterAction) - check the spelling", click.button.index, click.actionId);
        }
    });
}

void GameApp::Run()
{
    m_engine->Run();
}
