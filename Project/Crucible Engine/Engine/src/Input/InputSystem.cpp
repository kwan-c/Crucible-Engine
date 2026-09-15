#include "InputSystem.h"
#include "Diagnostics/Logger.h"
#include "GLFW/glfw3.h"

namespace {

	Crucible::Input* g_instance = nullptr;  // set by AttachWindow, read by the static queries

}// Anonymous namespace

namespace Crucible{

    void Input::Init(){
        // Zero initialise both bitsets
        m_previous.reset();
        m_current.reset();
        CRU_DEBUG("[Input] successfully initialised!");
    }

    void Input::Update(){

	}

    void Input::Shutdown(){
        // Unregister stuff
        glfwSetKeyCallback(m_window, nullptr);
        g_instance = nullptr;
        m_window = nullptr;

        CRU_DEBUG("[Input] successfully shutdown!");
    }

	// Save current state to previous state
    void Input::BeginFrame(){
		m_previous = m_current;
	}

    void Input::AttachWindow(GLFWwindow* window) {
        // Register the window
        m_window = window;

        // Publish the instance
        g_instance = this;

        glfwSetKeyCallback(window, &Input::KeyCallback);
        glfwSetWindowFocusCallback(window, &Input::FocusCallback);

    }

    bool Input::IsDown(const KeyCode& key) {
		if (g_instance == nullptr) {
			CRU_ERROR("Input queried before AttachWindow was called.");
            return false;
        }

		const std::size_t index = static_cast<std::size_t>(key);

        return g_instance->m_current.test(index);
	}

    bool Input::IsPressed(const KeyCode& key) {
        if (g_instance == nullptr) {
            CRU_ERROR("Input queried before AttachWindow was called.");
            return false;
        }

        const std::size_t index = static_cast<std::size_t>(key);

        return g_instance->m_current.test(index) && !g_instance->m_previous.test(index);
    }

    bool Input::IsReleased(const KeyCode& key) {
        if (g_instance == nullptr) {
            CRU_ERROR("Input queried before AttachWindow was called.");
            return false;
        }

        const std::size_t index = static_cast<std::size_t>(key);

        return !g_instance->m_current.test(index) && g_instance->m_previous.test(index);
	}

    void Input::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        (void)window; (void)scancode; (void)mods;   // not needed with the g_instance approach

        if (g_instance == nullptr) { return; }
        g_instance->OnKey(key, action);
	}

    void Input::FocusCallback(GLFWwindow* window, int focused) {
        (void)window;

        if (g_instance == nullptr) { return; }
        g_instance->OnFocusChanged(focused == GLFW_TRUE);
    }

    void Input::OnKey(const int& glfwKey, const int& glfwAction) {
        // Bail on GLFW_REPEAT
        if (glfwAction == GLFW_REPEAT) { return; }

        // Translate
        KeyCode crucibleKey = KeyCodeFromGlfw(glfwKey);

		if (crucibleKey == KeyCode::Unknown) {
            return; // Ignore unknown keys
        }
        // If pressed, set bit to true
        if (glfwAction == GLFW_PRESS) {
            m_current.set(static_cast<std::size_t>(crucibleKey), true);
            CRU_INFO("Keycode to string {}", KeyCodeToString(crucibleKey));
        }
        else // If released, set bit to false
            m_current.set(static_cast<std::size_t>(crucibleKey), false);


    }
    
    void Input::OnFocusChanged(const bool& focused) {
        if (focused) return;

		m_current.reset();  // clear all keys when focus is lost
	}

}