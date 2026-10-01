#include "GameApp.h"

#include "../Tests/AudioTestLayer.h"   // TEMPORARY - remove with the PushLayer below

bool GameApp::Init()
{
    if (!m_engine->Init())
    {
        // std::cout << "Failed to initialize Core Engine...\n";
        return false;
    }

    // Register and initialize game systems...

    m_engine->PushLayer(std::make_unique<AudioTestLayer>());   // TEMPORARY audio test - remove before pushing

    return true;
}

void GameApp::Run()
{
    m_engine->Run();
}
