#include "Core.h"

// STL
#include <iostream>

namespace Crucible {

    Core::Core() : 
        m_window{ std::make_unique<WindowManager>(1200, 800, "Crucible Engine") },
        m_renderer{}
    {}
    Core::~Core() { CleanUp(); }

    bool Core::Init() 
    {
        m_renderer->Init();

        std::cout << "The Hearth smoulders...\n";

        return true;
    }

    void Core::Run() {

        while (!m_window->ShouldClose()) {

            m_window->PollEvents();

            m_window->SwapBuffers();
        }
    }

    void Core::CleanUp() {

    }

}