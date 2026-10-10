/*************************************************************************
* \file:         InputSystem.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          14 September 2026
* \version       1.1
* \brief
*                Crucible::Input: turns GLFW events into one snapshot of
*                the keyboard and mouse per frame.
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
            Hides and captures the cursor so the mouse keeps reporting movement instead of
            stopping at the screen edge. This is the mode a first- or third-person camera
            runs in. Unity: Cursor.lockState plus Cursor.visible = false.
            \param locked
            True to capture, false to release.
        */
        static void SetMouseLocked(const bool& locked);

        /*!  \brief Is the cursor captured right now? */
        static bool IsMouseLocked();

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
        bool m_skipNextDelta = true;    // the next cursor event is a teleport, not a movement

        GLFWwindow* m_window = nullptr;
    };  // INPUT CLASS
#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
