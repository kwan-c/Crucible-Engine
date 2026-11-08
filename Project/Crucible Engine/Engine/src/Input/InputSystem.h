/*************************************************************************
* \file:         InputSystem.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          14 September 2026
* \version       1.2
* \brief
*                Crucible::Input: turns GLFW events into one snapshot of
*                the keyboard, mouse and gamepad per frame.
*
* \details
*                Layers 1 and 2 of the input design: GLFW events become
*                KeyCodes, then are frozen once per frame, so every system
*                sees the same input and "pressed this frame" can be
*                worked out. Queried statically: Input::IsDown, IsPressed,
*                IsReleased, GetMouseDelta and so on.
*
*                Single instance by design: the static queries go through
*                one pointer that AttachWindow sets.
*
* \history
*    -v1.0       keyboard: down, pressed and released this frame
*    -v1.1       mouse: buttons, position, movement, scroll and cursor
*                lock; clean at /W4
*    -v1.2       gamepad: buttons as KeyCodes, sticks with a dead zone,
*                triggers, polled once per frame in BeginFrame
*
**************************************************************************/
#pragma once

// ------------------------------------------------------------------------------------------- //
//  Crucible input - Layers 1 and 2 (device + snapshot).
//
//  Layer 1 turns GLFW events into engine-owned KeyCodes.
//  Layer 2 freezes those into one snapshot per frame, so every system in a frame sees
//  the same input, and so "pressed THIS frame" is computable at all.
//
//  Layers 3 and 4 (ActionMap, named actions) come next and sit on top of this without
//  changing it.
//
//  SINGLE INSTANCE BY DESIGN. The static queries resolve through one file-static pointer
//  that AttachWindow publishes, so exactly one Input may be attached at a time. Supporting
//  several windows means revisiting that indirection, not this class's layout.
// ------------------------------------------------------------------------------------------- //

// Third-party
struct GLFWwindow;   // forward declared on purpose - GLFW must not leak to our includers

// STL
#include <bitset>
#include <cstddef>
#include <string>

// Crucible engine
#include "EngineExport.h"
#include "Systems/ISystem.h"
#include "Input/KeyCode.h"
#include "Math/Math.h"

namespace Crucible {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // private std::bitset members; nothing outside Engine.dll touches them
#pragma warning(disable : 4275)   // ISystem is a pure interface, so it has nothing to export
#endif
    class ENGINE_API Input : public ISystem {

    public:

        // --- ISystem lifecycle ----------------------------------------------------------- //

        /*!
            \brief
            Zeroes both snapshots. Nothing else - the window is not known yet.
        */
        void Init() override;

        /*!
            \brief
            Per-frame work that is not the snapshot copy. Currently empty; gamepad state is
            polled rather than event-driven, so it will be read here.
            NOTE: Core does not call this yet. Wire it into the frame loop before putting
            anything in it, or the body will silently never run.
        */
        void Update() override;

        /*!
            \brief
            Unregisters the GLFW callbacks and drops both the window handle and the static
            instance pointer.
        */
        void Shutdown() override;

        /*!
            \brief
            Copies current -> previous. Core MUST call this immediately BEFORE PollEvents().
        */
        void BeginFrame();

        // --- wiring ---------------------------------------------------------------------- //

        /*!
            \brief
            Registers the GLFW key and focus callbacks against a window, and publishes the
            static instance pointer the query functions read.

            \param window
            The window owned by WindowManager. Must outlive this system.
            NOTE: the setters return the previous callback, which is how a later system
            (ImGui, an editor overlay) can chain instead of stomping.
        */
        void AttachWindow(GLFWwindow* window);

        // --- queries (static, callable from anywhere) ------------------------------------- //
        //
        // Static so gameplay never has to reach through Core, same split as the logger:
        // LogSession is an object, CRU_INFO is not. They forward to the single instance.
        //
        // All three log an error and return false if no instance is attached yet.

        /*!  \brief Is the key held right now? True every frame it is down. */
        static bool IsDown(const KeyCode& key);

        /*!
            \brief
            Did the key go down THIS frame? True for exactly one frame per press.
            Equals: current && !previous.
        */
        static bool IsPressed(const KeyCode& key);

        /*!
            \brief
            Did the key come up THIS frame? True for exactly one frame per release.
            Equals: !current && previous.
        */
        static bool IsReleased(const KeyCode& key);

        // --- mouse ----------------------------------------------------------------------- //
        //
        // Buttons are KeyCodes, so the three queries above already cover them:
        //     Input::IsPressed(KeyCode::MouseLeft)
        // What follows is only what a bitset cannot express: where the cursor is, how far it
        // moved, and the wheel. These return zero silently when nothing is attached - a
        // camera calls them every frame, so an error line here would be sixty a second.

        /*!
            \brief
            Cursor position in pixels, origin top-left of the window. Absolute, and stable
            for the whole frame.
        */
        static Math::vec2 GetMousePosition();

        /*!
            \brief
            How far the cursor moved during THIS frame, in pixels. This is what a camera
            wants. It is already frame-local, so do not subtract positions yourself, and do
            NOT scale it by delta time - a mouse reports distance, not speed. Multiply by
            your own sensitivity and nothing else.
        */
        static Math::vec2 GetMouseDelta();

        /*!
            \brief
            Wheel movement during THIS frame. y is the usual wheel, positive away from you
            (Unity's sign); x is the horizontal wheel most mice do not have. One notch is
            1.0, and touchpads report fractions.
        */
        static Math::vec2 GetMouseScroll();

        /*!
            \brief
            The game's request to hide and capture the cursor, so the mouse keeps reporting
            movement instead of stopping at the screen edge. This is the mode a first- or
            third-person camera runs in. Unity: Cursor.lockState plus Cursor.visible = false.
            Scripts call it (C# Input.SetMouseLocked); nothing in the engine locks on its own.
            \param locked
            True to capture, false to release.
            \note
            A request, not a command: while a release hold is on (SetMouseReleased - a menu
            that needs the pointer, or the editor) the cursor stays free, and the latest
            request applies the moment the last hold comes off.
        */
        static void SetMouseLocked(const bool& locked);

        /*!  \brief Is the cursor captured right now (the request, minus any release hold)? */
        static bool IsMouseLocked();

        /*!  \brief Who can hold the cursor free over the game's request. */
        enum class MouseRelease { Menu, Editor };

        /*!
            \brief
            Puts on or takes off a release hold. While any hold is on the cursor is free;
            when none is, IsMouseLocked() follows the game's latest SetMouseLocked request.
            Menu: UIMenuSystem, while a menu or a showsCursor canvas is shown. Editor: the
            release key and Pause; a click on the Game view takes it off.
        */
        static void SetMouseReleased(const MouseRelease& source, const bool& released);

        /*!  \brief Is this release hold on? */
        static bool IsMouseReleased(const MouseRelease& source);

        /*!  \brief What the game last asked for with SetMouseLocked, whatever the holds. */
        static bool IsMouseLockRequested();

        /*!
            \brief
            Drops the request and every hold, so the cursor is free and nothing is waiting
            to lock it. The editor calls it when Play stops.
        */
        static void ResetMouseLock();

        /*!
            \brief
            Is the mouse on the game's UI right now - over an element that blocks clicks
            (UIRect::raycastTarget), or holding a press that started on a button? Check it
            before acting on a click in gameplay, so pressing "Resume" doesn't also fire a
            weapon. Unity: EventSystem.current.IsPointerOverGameObject().
            \note
            Set by UIInputSystem, which runs early in the frame (PreUpdate) and only while
            Playing; false whenever the cursor is locked.
        */
        static bool IsPointerOverUI();
        /*!  \brief For UIInputSystem: what IsPointerOverUI() reports for the rest of this frame. */
        static void SetPointerOverUI(bool over);

        // --- gamepad --------------------------------------------------------------------- //
        //
        // Buttons are KeyCodes (GamepadA, GamepadRT, ...), so IsDown / IsPressed / IsReleased
        // already cover them:
        //     if (Input::IsPressed(KeyCode::GamepadA)) Jump();
        // What follows is what a bitset cannot hold. All of it reads the FIRST connected
        // gamepad - one local player - and is zero when none is connected or the window is not
        // focused. Any pad GLFW's built-in database knows (Xbox, PlayStation, Switch Pro and
        // most others) reads through the same Xbox-style layout. Unlike the mouse, these are
        // positions, not distances: a camera multiplies a stick by its speed AND by dt.

        /*!  \brief Is a gamepad connected (and recognised) right now? Unity: Gamepad.current != null. */
        static bool IsGamepadConnected();

        /*!  \brief The connected pad's name, e.g. "Xbox Controller"; empty when none. For logs and menus. */
        static std::string GetGamepadName();

        /*!
            \brief
            The left stick, each axis -1..1 with right and UP positive (Unity's sign - GLFW
            reports up as negative, so it is flipped here). A dead zone is already applied: a
            stick resting slightly off-centre reads exactly zero, and full tilt reads exactly
            1 in length. Unity: Gamepad.current.leftStick.ReadValue().
        */
        static Math::vec2 GetGamepadLeftStick();

        /*!  \brief The right stick, the same way as the left. Usually the camera. */
        static Math::vec2 GetGamepadRightStick();

        /*!
            \brief
            How far the left trigger is pulled: 0 released, 1 fully. Past halfway it also
            counts as KeyCode::GamepadLT being held. Unity: Gamepad.current.leftTrigger.
        */
        static float GetGamepadLeftTrigger();

        /*!  \brief The right trigger, the same way as the left. */
        static float GetGamepadRightTrigger();

    private:

        // --- GLFW plumbing --------------------------------------------------------------- //
        //
        // GLFW takes plain C function pointers, which a non-static member function cannot
        // satisfy (it has a hidden `this`). These statics are trampolines: they read the
        // file-static instance pointer published by AttachWindow and call the real handler.
        // They are the only places that pointer is needed - the handlers below have `this`.

        static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void FocusCallback(GLFWwindow* window, int focused);
        static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void CursorPosCallback(GLFWwindow* window, double xPos, double yPos);
        static void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset);

        /*!
            \brief
            Sets or clears one bit in m_current.
        */
        void OnKey(const int& glfwKey, const int& glfwAction);

        /*!
            \brief
            Clears every key when the window loses focus. Does nothing on focus gain.
        */
        void OnFocusChanged(const bool& focused);

        /*!
            \brief
            Sets or clears one bit in m_current, exactly like OnKey - buttons share the
            bitset with the keys. There is no GLFW_REPEAT for buttons.
        */
        void OnMouseButton(const int& glfwButton, const int& glfwAction);

        /*!
            \brief
            Accumulates this frame's movement, then stores the new position. Swallows one
            event after a lock change or a focus loss, where the cursor teleports.
        */
        void OnCursorMoved(const double& xPos, const double& yPos);

        /*!
            \brief
            Accumulates this frame's wheel movement.
        */
        void OnScroll(const double& xOffset, const double& yOffset);

        /*!
            \brief
            Reads the gamepad into this frame's snapshot: its buttons into m_current, its
            sticks and triggers into the members below. Called by BeginFrame. A gamepad has
            no events to wait for - GLFW asks the device directly - so polling it once per
            frame, at the same moment as the copy, keeps it in step with the keys.
        */
        void PollGamepad();

        /*!
            \brief
            Locks or frees the GLFW cursor to match the request and the release holds. Does
            nothing when it already matches, so the delta skip below only follows a real change.
        */
        void ApplyMouseLock();

        // --- state ----------------------------------------------------------------------- //

        static constexpr std::size_t KEY_COUNT = static_cast<std::size_t>(KeyCode::Count);

        std::bitset<KEY_COUNT> m_current;    // mutated by callbacks during PollEvents
        std::bitset<KEY_COUNT> m_previous;   // snapshot of m_current from before PollEvents

        // Mouse state a bitset cannot hold. xmath vectors do not zero themselves when default
        // constructed, hence the explicit initialisers. Position is absolute; delta and
        // scroll describe ONE frame and are cleared by BeginFrame.
        Math::vec2 m_mousePosition{ 0.f, 0.f };   // pixels, origin top-left
        Math::vec2 m_mouseDelta{ 0.f, 0.f };      // pixels moved this frame
        Math::vec2 m_mouseScroll{ 0.f, 0.f };     // wheel notches this frame

        bool m_mouseLocked = false;     // cursor captured for mouse-look
        bool m_lockRequested = false;   // the game's SetMouseLocked request
        bool m_releasedByMenu = false;  // release hold: a menu needs the pointer
        bool m_releasedByEditor = false;// release hold: the editor took the pointer back
        bool m_pointerOverUI = false;   // IsPointerOverUI(), written by UIInputSystem
        bool m_skipNextDelta = true;    // the next cursor event is a teleport, not a movement
        bool m_hasFocus      = true;    // a gamepad is ignored while the window is in the background

        // Gamepad state a bitset cannot hold, refilled by PollGamepad every frame.
        int         m_gamepadId    = -1;               // GLFW joystick slot, -1 = none
        std::string m_gamepadName;                     // for the connect / disconnect log lines
        Math::vec2  m_leftStick{ 0.f, 0.f };           // -1..1, up positive, dead zone applied
        Math::vec2  m_rightStick{ 0.f, 0.f };
        float       m_leftTrigger  = 0.0f;             // 0..1
        float       m_rightTrigger = 0.0f;

        GLFWwindow* m_window = nullptr;
    };  // INPUT CLASS
#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
