#pragma once

#include "Core.h"
#include <memory>
#include <Configuration/EngineConfigLoader.h>

class GameApp
{
public:
    // Same config file as the Editor (Assets/Config/EngineConfig.json), plus its "game" overrides.
    GameApp() : m_config(Crucible::EngineConfigLoader::LoadForApp(Crucible::AppKind::Game)), m_engine{std::make_unique<Crucible::Core>(m_config)} {}

    // GameApp main functions
    bool    Init();
    void    Run();

private:
    Crucible::EngineConfig m_config;
    std::unique_ptr<Crucible::Core> m_engine;

};
