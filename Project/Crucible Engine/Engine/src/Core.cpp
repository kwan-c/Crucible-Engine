#include "Core.h"
#include "AssetPipe/Assetmanager.h"

// STL
#include <iostream>

namespace Crucible {

    Core::Core() :
        m_window{ new WindowManager(1200, 800, "Crucible Engine") },
        m_renderer{ new Renderer(*m_window) }
    {
    }
    Core::~Core() {
        CleanUp();
        delete m_renderer;
        delete m_window;
    }

    bool Core::Init()
    {
        std::cout << "The Hearth smoulders...\n";

        // ======== asset debugging (Removable) ==========
        {
            Crucible::AssetManager assets;
            std::cout << "\n\n";
            assets.Init();
            assets.PrintIndex();

            auto tex = assets.Load("Textures/Orange_Milk.jpg");
            assets.PrintLoaded();
        }
        // ===========================================

        ImGuiManager::Init(*this);

        return true;
    }

    void Core::Run() {

        while (!m_window->ShouldClose()) {

            float dt = 1.f / 60.f;

            m_window->PollEvents();
            m_renderer->Update(dt);
            m_window->SwapBuffers();
        }
    }

    void Core::CleanUp() {}
}