#include "Core.h"

// Extern Libs
#include <xmath.h>
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_glfw.h>
#include <fmod.hpp>

// STL
#include <iostream>

namespace
{
    FMOD::System* fmodSystem = nullptr;
    FMOD::Sound* testSound = nullptr;
}

void TestJolt()
{
    JPH::RegisterDefaultAllocator();

    JPH::Factory::sInstance = new JPH::Factory();

    JPH::RegisterTypes();

    std::cout << "Jolt Physics initialized!\n";

    JPH::UnregisterTypes();

    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

namespace
{
    // Required OpenGL version;
    static constexpr int REQUIRED_GL_MAJOR = 4;
    static constexpr int REQUIRED_GL_MINOR = 5;

    // Helper functions for window operations;
    void static framebuffer_size_callback(GLFWwindow* window, int width, int height)
    {
        (void)window;
        glViewport(0, 0, width, height);
    }
    void static processInput(GLFWwindow *window)
    {
        if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
    }
}

namespace Crucible
{
    bool Core::Init()
    {
        int winWidth = 1200;
        int winHeight = 800;
        float bgColor[] = { 0.3f, 0.1f, 0.1f, 1.0f };

        if (!InitAppWindow("Crucible Engine", winWidth, winHeight, bgColor))
        {
            std::cout << "Failed to initialize Core Engine.\n";
            return false;
        }

        std::cout << "The Hearth smoulders..." << std::endl;

        TestJolt();
        
        // ------------------------------------------------------------ //
        // ImGui
        // ------------------------------------------------------------ //

        std::cout << "Creating ImGui context...\n";

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        std::cout << "Initializing GLFW backend...\n";

        if (!ImGui_ImplGlfw_InitForOpenGL(m_appWindow, true))
        {
            std::cout << "GLFW backend FAILED!\n";
            return false;
        }

        std::cout << "Initializing OpenGL3 backend...\n";

        if (!ImGui_ImplOpenGL3_Init("#version 450"))
        {
            std::cout << "OpenGL3 backend FAILED!\n";
            return false;
        }

        std::cout << "All ImGui initialization complete!\n";

        std::cout << "ImGui initialized!\n";
        std::cout << "ImGui version: "
            << IMGUI_VERSION
            << '\n';

        // ------------------------------------------------------------ //
        // FMOD
        // ------------------------------------------------------------ //

        FMOD_RESULT result;

        result = FMOD::System_Create(&fmodSystem);

        if (result != FMOD_OK)
        {
            std::cout << "Failed to create FMOD system.\n";
            return false;
        }

        result = fmodSystem->init(
            512,
            FMOD_INIT_NORMAL,
            nullptr
        );

        if (result != FMOD_OK)
        {
            std::cout << "Failed to initialize FMOD system.\n";
            return false;
        }

        std::cout << "FMOD initialized!\n";

        result = fmodSystem->createSound(
            "Pull_Lever.wav",
            FMOD_DEFAULT,
            nullptr,
            &testSound
        );

        if (result != FMOD_OK)
        {
            std::cout << "Failed to load test.wav.\n";
        }

        std::cout << "Test sound loaded!\n";

        return true;
    }

    void Core::Update()
    {
        glClear(GL_COLOR_BUFFER_BIT);

        UpdateAppWindow();

        // -------------------------------------------------------- //
        // ImGui
        // -------------------------------------------------------- //

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();


        ImGui::Begin("Test Window");

        ImGui::Text("Hello from ImGui!");

        if (ImGui::Button("Play Sound"))
        {
            FMOD::Channel* channel = nullptr;

            FMOD_RESULT result = fmodSystem->playSound(
                testSound,
                nullptr,
                false,
                &channel
            );

            if (result != FMOD_OK)
            {
                std::cout << "Failed to play sound.\n";
            }
        }

        ImGui::End();


        ImGui::Render();

        ImGui_ImplOpenGL3_RenderDrawData(
            ImGui::GetDrawData()
        );


        if (fmodSystem)
        {
            fmodSystem->update();
        }

        glfwSwapBuffers(m_appWindow);
        /*
            for (System s : m_systems)
            {
                s.Update();
            }
        */
    }

    void Core::CleanUp()
    {
        CleanUpAppWindow();
    }

    bool Core::InitAppWindow(std::string const& windowTitle, int width, int height, float bgColor[4])
    {
        // Set up GLFW ------------------------------------------------------------------------- //
        if (!glfwInit())
            throw std::runtime_error("Failed to initialize GLFW.");

        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, REQUIRED_GL_MAJOR);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, REQUIRED_GL_MINOR);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_pMainWindow = glfwCreateWindow(width, height, windowTitle.c_str(), NULL, NULL);
        if (m_pMainWindow == NULL)
        {
            std::cout << "Failed to create GLFW window." << std::endl;
            glfwTerminate();
            return false;
        }
        glfwMakeContextCurrent(m_pMainWindow);

        // Set up GLAD ------------------------------------------------------------------------- //
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cout << "Failed to initialize GLAD." << std::endl;
            glfwTerminate();
            return false;
        }

        glViewport(0, 0, width, height);
        glfwSetFramebufferSizeCallback(m_pMainWindow, framebuffer_size_callback);
        glClearColor(bgColor[0], bgColor[1], bgColor[2], bgColor[3]);

        return true;
    }

    void Core::UpdateAppWindow()
    {
        //glClear(GL_COLOR_BUFFER_BIT);
        
        processInput(m_pMainWindow);
        m_appShouldClose = glfwWindowShouldClose(m_pMainWindow);

        glfwSwapBuffers(m_pMainWindow);
        glfwPollEvents();  
    }

    void Core::CleanUpAppWindow()
    {
        // -------------------------------------------------------- //
        // FMOD
        // -------------------------------------------------------- //

        if (testSound)
        {
            testSound->release();
            testSound = nullptr;
        }

        if (fmodSystem)
        {
            fmodSystem->close();
            fmodSystem->release();
            fmodSystem = nullptr;
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();

        ImGui::DestroyContext();

        glfwTerminate();
    }

    // Accessor => pointer to application window;
    GLFWwindow* Core::GetMainWindow() const { return m_pMainWindow; }
    // Accessor => boolean data member tracking whether app should close;
    bool Core::AppShouldClose() const { return m_appShouldClose; }
}