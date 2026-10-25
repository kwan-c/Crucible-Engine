#pragma once

#include "Core.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <Configuration/EngineConfigLoader.h>
#include "ECS/Systems/EventBus/Subscription.h"

class GameApp
{
public:
    GameApp() : m_config(Crucible::EngineConfigLoader::LoadForApp(Crucible::AppKind::Game)), m_engine{std::make_unique<Crucible::Core>(m_config)} {}

    bool    Init();
    void    Run();

    // A UI button with onClick = Custom and this actionId runs `action` when clicked (the button's
    // entity is passed in). Registering the same name again replaces it. Until the scripting layer
    // arrives, this is where a menu's "Settings", "Credits", "StartCoOp" ... buttons get wired up.
    void    RegisterAction(const std::string& actionId, std::function<void(Crucible::Entity button)> action);

private:
    void    SubscribeToUI();

    Crucible::EngineConfig m_config;
    std::unique_ptr<Crucible::Core> m_engine;

    std::unordered_map<std::string, std::function<void(Crucible::Entity)>> m_actions;
    std::unordered_set<std::string> m_warnedActions;   // unknown actionIds already reported

    // Declared after m_engine on purpose: destroyed first, while the engine's event bus still exists.
    Crucible::Subscription m_onQuit;     // UIEvents::QuitRequested
    Crucible::Subscription m_onCustom;   // UIEvents::CustomAction
};
