#include "Core.h"

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