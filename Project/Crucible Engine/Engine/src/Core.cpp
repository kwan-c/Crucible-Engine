#include "Core.h"

// STL
#include <iostream>

namespace Crucible {

    Core::Core() : m_window{ std::make_unique<WindowManager>() }, 
                   m_input{ std::make_unique<Input>() } {}
    Core::~Core() { CleanUp(); }

    bool Core::Init() 
    {

        int winWidth = 1200;
        int winHeight = 800;
        float bgColor[] = { 0.3f, 0.1f, 0.1f, 1.0f };

        if (!m_window->Init("Crucible Engine", winWidth, winHeight, bgColor))
        {
            std::cout << "Failed to initialize Core Engine.\n";
            return false;
        }
        m_input->Init();
        m_input->AttachWindow(m_window->GetNativeHandle());


        std::cout << "The Hearth smoulders...\n";

        return true;
    }

    void Core::Run() {

        while (!m_window->ShouldClose()) {

            m_input->BeginFrame();
            m_window->PollEvents();

            m_window->SwapBuffers();
        }
    }

    void Core::CleanUp() {

        m_input->Shutdown();
        m_window->Shutdown();
    }

}