#pragma once

#include "Core.h"
#include <memory>
#include <Configuration/EngineConfigLoader.h>
#include <Diagnostics/Logger.h>

namespace
{
    Crucible::EngineConfig LoadConfig()
    {
        Crucible::EngineConfig config;
        try
        {
            config = Crucible::EngineConfigLoader::Load("../../../EngineConfig/EngineConfig.json");

        }
        catch (std::exception& e)
        {
            CRU_WARN("CORE : engine config loader did not Load properly {}", e.what());
            config = Crucible::EngineConfig{};

        }
        return config;
    }
}

class GameApp
{
public:
    GameApp() : m_config( LoadConfig()), m_engine{std::make_unique<Crucible::Core>(m_config)} {}

    // GameApp main functions
    bool    Init();
    void    Run();

private:
    Crucible::EngineConfig m_config;
    std::unique_ptr<Crucible::Core> m_engine;

};
