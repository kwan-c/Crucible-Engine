#include "GameApp.h"

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


    return true;
}

void GameApp::Run()
{
    m_engine->Run();
}
