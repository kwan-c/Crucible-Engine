#include "Core.h"
#include "AssetPipe/Assetmanager.h"

// STL
#include <iostream>

namespace Crucible {

    Core::Core() : 
        m_window{ new WindowManager(1200, 800, "Crucible Engine") },
        m_renderer{ new Renderer(m_window->GetNativeHandle()) }
    {}
    Core::~Core() { 
        CleanUp(); 
        delete m_renderer;
        delete m_window;
    }

    bool Core::Init() 
    {
        std::cout << "The Hearth smoulders...\n";

        // Enable memory leak checking at program exit
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

        // ======== asset debugging (Removable) ==========
        {
            Crucible::AssetManager assets;
            std::cout << "\n\n";
            assets.Init();
            assets.PrintIndex();

            auto tex = assets.Load("Textures/Crucible.jpg");
            assets.PrintLoaded();
        }
        // ===========================================

        return true;
    }

    void Core::Run() {

        while (!m_window->ShouldClose()) {

            m_window->PollEvents();

            m_window->SwapBuffers();
        }
    }

    void Core::CleanUp() {}
}