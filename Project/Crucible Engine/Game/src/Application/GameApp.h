#pragma once

#include "Core.h"

class GameApp
{
public:
    // GameApp main functions
    bool    Init();
    int     Run();

private:
    Crucible::Core m_engine;

};
