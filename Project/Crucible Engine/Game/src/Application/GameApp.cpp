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

int GameApp::Run()
{
    while (!m_engine.AppShouldClose())
    {
        m_engine.Update();

        /*
            for (GameSystem gs : m_gameSystems)
            {
                gs.Update();
            }
        */
    }
    m_engine.CleanUp();
    // GameApp::CleanUp();

    return 0;
}
