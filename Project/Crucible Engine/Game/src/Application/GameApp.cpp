#include "GameApp.h"

bool GameApp::Init()
{
    if (!m_engine.Init())
    {
        // std::cout << "Failed to initialize Core Engine...\n";
        return false;
    }

    // Register and initialize game systems...

    return true;
}

void GameApp::Run()
{
    m_engine.Run();
}
