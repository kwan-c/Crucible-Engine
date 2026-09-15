#pragma once

#include "Core.h"
#include <memory>

class GameApp
{
public:
    GameApp() : m_engine { std::make_unique<Crucible::Core>() } {}

    // GameApp main functions
    bool    Init();
    void    Run();

private:
    std::unique_ptr<Crucible::Core> m_engine;

};
