#include "GameApp.h"

bool GameApp::Init()
{
    if (!m_engine->Init())
    {
        // std::cout << "Failed to initialize Core Engine...\n";
        return false;
    }

    // Register and initialize game systems...
    // Gameplay systems: m_engine->Systems().RegisterSystem<T>(...), with
    // desc.SimulationOnly() in their Describe() so they run only while playing.

    // The game is always playing: start the session at the first frame (after the
    // startup scene loads). No snapshot - there is no editor to go back to.
    m_engine->RequestPlay(/*restoreOnStop*/ false);

    return true;
}

void GameApp::Run()
{
    m_engine->Run();
}
