/*************************************************************************
* \file:         InputSystem.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          14 September 2026
* \version       1.3
* \brief
*                Crucible::Input's implementation: the GLFW callbacks, the
*                gamepad poll and the per-frame snapshot.
*
* \details
*                The callbacks write this frame's state; BeginFrame first
*                copies it to last frame's and clears the mouse movement
*                and scroll. Shutdown removes every callback it installed,
*                so a window that outlives Input never calls into freed
*                memory.
*
* \history
*    -v1.0       keyboard: down, pressed and released this frame
*    -v1.1       mouse: buttons, position, movement, scroll and cursor
*                lock; maths moved to glm
*    -v1.2       fix: a KeyCode past Count returns false instead of
*                throwing out of the key queries
*    -v1.3       gamepad: polled in BeginFrame - buttons into the same
*                bitset, sticks (dead zone) and triggers beside it
*
**************************************************************************/
#include "InputSystem.h"
#include "Diagnostics/Logger.h"
#include "GLFW/glfw3.h"

// STL
#include <algorithm>    // std::clamp

namespace {

    // Unity's default stick dead zone (the new Input System's StickDeadzone): below 0.125 of
    // full tilt a stick reads as centred - worn sticks rest slightly off-centre and would
    // otherwise drift the camera - and from 0.925 it reads as fully pushed, because many sticks
    // never quite reach 1. Everything between is stretched back over 0..1.
    constexpr float kStickDeadMin = 0.125f;
    constexpr float kStickDeadMax = 0.925f;

    // A trigger past this much counts as the button GamepadLT / GamepadRT being held.
    constexpr float kTriggerAsButton = 0.5f;

    /*!
        \brief
        A raw stick reading with the dead zone applied, as a whole vector (radial), not per
        axis: a per-axis dead zone snaps diagonals onto the axes and makes walking at an
        angle feel notchy. The direction is kept; only the length is remapped.
    */
    Crucible::Math::vec2 ApplyStickDeadZone(const float x, const float y) {
        const Crucible::Math::vec2 raw{ x, y };
        const float length = glm::length(raw);
        if (length <= kStickDeadMin) return Crucible::Math::vec2{ 0.f, 0.f };

        const float remapped = std::clamp((length - kStickDeadMin) / (kStickDeadMax - kStickDeadMin), 0.0f, 1.0f);
        return raw * (remapped / length);
    }

    // GLFW reports a trigger from -1 (released) to +1 (fully pulled).
    float TriggerTo01(const float raw) {
        return std::clamp((raw + 1.0f) * 0.5f, 0.0f, 1.0f);
    }

	Crucible::Input* g_instance = nullptr;  // set by AttachWindow, read by the static queries

    // A KeyCode made from a number - a saved key binding, a loop that runs up to
    // Count - can point past the end of the bitsets, and bitset::test THROWS
    // std::out_of_range on that: nothing catches it, so the game just closes.
    // The key queries refuse it here instead, and say why.
    bool IsValidKey(const Crucible::KeyCode& key) {
        if (static_cast<std::size_t>(key) < static_cast<std::size_t>(Crucible::KeyCode::Count)) return true;

        CRU_ERROR("Input queried with KeyCode {}, which is past KeyCode::Count - treated as not pressed", static_cast<int>(key));
        return false;
    }

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
        if (m_window != nullptr) {

            // Clear every callback we installed. The window can outlive this system, and a
            // stale callback would then fire into a dangling g_instance.
            glfwSetKeyCallback(m_window, nullptr);
            glfwSetWindowFocusCallback(m_window, nullptr);
            glfwSetMouseButtonCallback(m_window, nullptr);
            glfwSetCursorPosCallback(m_window, nullptr);
            glfwSetScrollCallback(m_window, nullptr);

            // Never leave the cursor captured: if the window outlives us, the user has no
            // pointer and no way to get it back.
            if (m_mouseLocked) { glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }
        }

        m_mouseLocked = false;
        m_lockRequested = m_releasedByMenu = m_releasedByEditor = false;
        g_instance = nullptr;
        m_window = nullptr;

        // Forget the pad, so a later Init starts from "nothing connected" and logs the
        // connection again rather than inheriting stale sticks.
        m_gamepadId = -1;
        m_gamepadName.clear();
        m_leftStick = m_rightStick = Math::vec2{ 0.f, 0.f };
        m_leftTrigger = m_rightTrigger = 0.0f;

        CRU_DEBUG("[Input] successfully shutdown!");
    }

	// Save current state to previous state
    void Input::BeginFrame(){
		m_previous = m_current;

        // Delta and scroll describe one frame only, so they start every frame at zero and
        // are refilled by the callbacks during PollEvents. That is the whole reason this
        // function has to run immediately before PollEvents.
        m_mouseDelta = Math::vec2{ 0.f, 0.f };
        m_mouseScroll = Math::vec2{ 0.f, 0.f };

        // AFTER the copy: the pad's buttons land in m_current as this frame's state, so
        // IsPressed(GamepadA) compares them against last frame's exactly like a key.
        PollGamepad();
	}

    void Input::PollGamepad() {

        // WHICH PAD ----------------------------------------------------------------------- //
        // Keep the one we have while it stays connected, so plugging in a second pad does
        // not steal control mid-game; otherwise take the first one GLFW recognises.
        // glfwJoystickIsGamepad, not glfwJoystickPresent: a flight stick or a wheel is a
        // joystick with no gamepad layout, and its buttons would mean nothing here.
        if (m_gamepadId >= 0 && glfwJoystickIsGamepad(m_gamepadId) == GLFW_FALSE) {
            CRU_INFO("[Input] gamepad disconnected: {}", m_gamepadName);
            m_gamepadId = -1;
            m_gamepadName.clear();
        }
        if (m_gamepadId < 0) {
            for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST; ++id) {
                if (glfwJoystickIsGamepad(id) == GLFW_FALSE) continue;
                const char* const name = glfwGetGamepadName(id);
                m_gamepadId   = id;
                m_gamepadName = name != nullptr ? name : "gamepad";
                CRU_INFO("[Input] gamepad connected: {}", m_gamepadName);
                break;
            }
        }

        // READ IT ------------------------------------------------------------------------- //
        // Ignored while the window is in the background, the same as the keys: a pad left on
        // the desk next to a browser should not walk the mech. The read still has to happen
        // to zero everything, or a button held when focus left would stay "down" forever.
        GLFWgamepadstate state{};
        const bool live = m_gamepadId >= 0 && m_hasFocus && glfwGetGamepadState(m_gamepadId, &state) == GLFW_TRUE;

        for (int button = GLFW_GAMEPAD_BUTTON_A; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button) {
            const std::size_t index = static_cast<std::size_t>(KeyCodeFromGlfwGamepadButton(button));
            m_current.set(index, live && state.buttons[button] == GLFW_PRESS);
        }

        if (!live) {
            m_leftStick = m_rightStick = Math::vec2{ 0.f, 0.f };
            m_leftTrigger = m_rightTrigger = 0.0f;
        }
        else {
            // GLFW's Y axes are positive DOWN (its database follows SDL). Flipped, so up is
            // positive the way Unity and every camera here expect.
            m_leftStick    = ApplyStickDeadZone(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X],  -state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
            m_rightStick   = ApplyStickDeadZone(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], -state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
            m_leftTrigger  = TriggerTo01(state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]);
            m_rightTrigger = TriggerTo01(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]);
        }

        // The triggers as buttons too, so "fire on RT" can use IsPressed.
        m_current.set(static_cast<std::size_t>(KeyCode::GamepadLT), m_leftTrigger  > kTriggerAsButton);
        m_current.set(static_cast<std::size_t>(KeyCode::GamepadRT), m_rightTrigger > kTriggerAsButton);

    }   // Input::PollGamepad()

    // --- gamepad queries: zero, silently, with no instance - a camera reads them every frame --- //

    bool Input::IsGamepadConnected() {
        return g_instance != nullptr && g_instance->m_gamepadId >= 0;
    }

    std::string Input::GetGamepadName() {
        return g_instance != nullptr ? g_instance->m_gamepadName : std::string{};
    }

    Math::vec2 Input::GetGamepadLeftStick() {
        return g_instance != nullptr ? g_instance->m_leftStick : Math::vec2{ 0.f, 0.f };
    }

    Math::vec2 Input::GetGamepadRightStick() {
        return g_instance != nullptr ? g_instance->m_rightStick : Math::vec2{ 0.f, 0.f };
    }

    float Input::GetGamepadLeftTrigger() {
        return g_instance != nullptr ? g_instance->m_leftTrigger : 0.0f;
    }

    float Input::GetGamepadRightTrigger() {
        return g_instance != nullptr ? g_instance->m_rightTrigger : 0.0f;
    }

    void Input::AttachWindow(GLFWwindow* window) {
        // Register the window
        m_window = window;

        // Publish the instance
        g_instance = this;

        glfwSetKeyCallback(window, &Input::KeyCallback);
        glfwSetWindowFocusCallback(window, &Input::FocusCallback);

        glfwSetMouseButtonCallback(window, &Input::MouseButtonCallback);
        glfwSetCursorPosCallback(window, &Input::CursorPosCallback);
        glfwSetScrollCallback(window, &Input::ScrollCallback);

        // Start from where the cursor already is. Without this the first cursor event is
        // measured against (0, 0), which is a delta of hundreds of pixels - the classic
        // camera snap on the first frame somebody moves the mouse.
        double startX = 0.0;
        double startY = 0.0;
        glfwGetCursorPos(window, &startX, &startY);

        m_mousePosition = Math::vec2{ static_cast<float>(startX), static_cast<float>(startY) };
        m_skipNextDelta = false;

    }

    bool Input::IsDown(const KeyCode& key) {
		if (g_instance == nullptr) {
			CRU_ERROR("Input queried before AttachWindow was called.");
            return false;
        }
        if (!IsValidKey(key)) return false;

		const std::size_t index = static_cast<std::size_t>(key);

        return g_instance->m_current.test(index);
	}

    bool Input::IsPressed(const KeyCode& key) {
        if (g_instance == nullptr) {
            CRU_ERROR("Input queried before AttachWindow was called.");
            return false;
        }
        if (!IsValidKey(key)) return false;

        const std::size_t index = static_cast<std::size_t>(key);

        return g_instance->m_current.test(index) && !g_instance->m_previous.test(index);
    }

    bool Input::IsReleased(const KeyCode& key) {
        if (g_instance == nullptr) {
            CRU_ERROR("Input queried before AttachWindow was called.");
            return false;
        }
        if (!IsValidKey(key)) return false;

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
        }
        else {// If released, set bit to false
            m_current.set(static_cast<std::size_t>(crucibleKey), false);
        } 


    }
    
    // --- mouse ---------------------------------------------------------------------------- //
    //
    // The buttons live in m_current with the keys, so IsDown / IsPressed / IsReleased already
    // work on them. Only what a bitset cannot hold needs code of its own.
    //
    // These five guard on g_instance WITHOUT logging, unlike the key queries: a camera calls
    // them every frame, so one error line would be sixty a second.

    Math::vec2 Input::GetMousePosition() {
        if (g_instance == nullptr) { return Math::vec2{ 0.f, 0.f }; }

        return g_instance->m_mousePosition;
    }

    Math::vec2 Input::GetMouseDelta() {
        if (g_instance == nullptr) { return Math::vec2{ 0.f, 0.f }; }

        return g_instance->m_mouseDelta;
    }

    Math::vec2 Input::GetMouseScroll() {
        if (g_instance == nullptr) { return Math::vec2{ 0.f, 0.f }; }

        return g_instance->m_mouseScroll;
    }

    void Input::SetMouseLocked(const bool& locked) {
        if (g_instance == nullptr) { return; }

        g_instance->m_lockRequested = locked;
        g_instance->ApplyMouseLock();
    }

    void Input::SetMouseReleased(const MouseRelease& source, const bool& released) {
        if (g_instance == nullptr) { return; }

        (source == MouseRelease::Menu ? g_instance->m_releasedByMenu : g_instance->m_releasedByEditor) = released;
        g_instance->ApplyMouseLock();
    }

    bool Input::IsMouseReleased(const MouseRelease& source) {
        if (g_instance == nullptr) { return false; }

        return source == MouseRelease::Menu ? g_instance->m_releasedByMenu : g_instance->m_releasedByEditor;
    }

    bool Input::IsMouseLockRequested() {
        if (g_instance == nullptr) { return false; }

        return g_instance->m_lockRequested;
    }

    void Input::ResetMouseLock() {
        if (g_instance == nullptr) { return; }

        g_instance->m_lockRequested = g_instance->m_releasedByMenu = g_instance->m_releasedByEditor = false;
        g_instance->ApplyMouseLock();
    }

    void Input::ApplyMouseLock() {
        if (m_window == nullptr) { return; }

        const bool locked = m_lockRequested && !m_releasedByMenu && !m_releasedByEditor;

        // Already in that mode: return before the warp, so no delta is skipped for nothing.
        if (m_mouseLocked == locked) { return; }

        m_mouseLocked = locked;

        glfwSetInputMode(m_window, GLFW_CURSOR,
            locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

        // Raw motion skips the OS pointer acceleration curve, so the same hand movement
        // always turns the camera by the same amount. GLFW only honours it while the cursor
        // is disabled, and not every platform has it - hence the support check.
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION,
                locked ? GLFW_TRUE : GLFW_FALSE);
        }

        // Disabling the cursor warps it to the window centre and enabling it puts it back,
        // so either way the next event is a teleport. Swallow one delta, or the camera spins
        // the moment you enter or leave mouse-look.
        m_skipNextDelta = true;
    }

    bool Input::IsMouseLocked() {
        if (g_instance == nullptr) { return false; }

        return g_instance->m_mouseLocked;
    }

    bool Input::IsPointerOverUI() {
        if (g_instance == nullptr) { return false; }

        return g_instance->m_pointerOverUI;
    }

    void Input::SetPointerOverUI(bool over) {
        if (g_instance == nullptr) { return; }

        g_instance->m_pointerOverUI = over;
    }

    void Input::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        (void)window; (void)mods;

        if (g_instance == nullptr) { return; }
        g_instance->OnMouseButton(button, action);
    }

    void Input::CursorPosCallback(GLFWwindow* window, double xPos, double yPos) {
        (void)window;

        if (g_instance == nullptr) { return; }
        g_instance->OnCursorMoved(xPos, yPos);
    }

    void Input::ScrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
        (void)window;

        if (g_instance == nullptr) { return; }
        g_instance->OnScroll(xOffset, yOffset);
    }

    void Input::OnMouseButton(const int& glfwButton, const int& glfwAction) {
        // Own numbering, own translation - but the same bitset as the keys, which is why
        // there is no second set of query functions for buttons.
        const KeyCode crucibleButton = KeyCodeFromGlfwMouseButton(glfwButton);

        if (crucibleButton == KeyCode::Unknown) {
            return;   // a button beyond the eight GLFW reports
        }

        // No GLFW_REPEAT exists for buttons, so PRESS or RELEASE is the whole story.
        m_current.set(static_cast<std::size_t>(crucibleButton), glfwAction == GLFW_PRESS);
    }

    void Input::OnCursorMoved(const double& xPos, const double& yPos) {
        const Math::vec2 position{ static_cast<float>(xPos), static_cast<float>(yPos) };

        if (m_skipNextDelta) {
            m_mousePosition = position;
            m_skipNextDelta = false;
            return;
        }

        // GLFW can deliver several cursor events inside one PollEvents, so the movement is
        // accumulated rather than assigned. Working it out from positions in gameplay code
        // would throw away everything except the last event of the frame.
        m_mouseDelta.x += position.x - m_mousePosition.x;
        m_mouseDelta.y += position.y - m_mousePosition.y;

        m_mousePosition = position;
    }

    void Input::OnScroll(const double& xOffset, const double& yOffset) {
        // Accumulated for the same reason as the delta: one frame can hold several notches.
        m_mouseScroll.x += static_cast<float>(xOffset);
        m_mouseScroll.y += static_cast<float>(yOffset);
    }

    void Input::OnFocusChanged(const bool& focused) {
        m_hasFocus = focused;   // PollGamepad ignores the pad while this is false
        if (focused) return;

		m_current.reset();  // clear all keys when focus is lost

        // The cursor can be anywhere by the time focus comes back, so the first event after
        // that would be a teleport-sized delta. Swallow it, exactly like a lock change does.
        m_skipNextDelta = true;
	}


}