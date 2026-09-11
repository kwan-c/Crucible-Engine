#pragma once

#include "Core.h"

class GameApp
{
public:
    // GameApp main functions
    bool    Init();
    void    Run();

private:
    Crucible::Core m_engine;

};
