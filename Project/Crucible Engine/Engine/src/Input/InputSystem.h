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

namespace Crucible {

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

    private:

        // --- GLFW plumbing --------------------------------------------------------------- //
        //
        // GLFW takes plain C function pointers, which a non-static member function cannot
        // satisfy (it has a hidden `this`). These statics are trampolines: they read the
        // file-static instance pointer published by AttachWindow and call the real handler.
        // They are the only places that pointer is needed - the handlers below have `this`.

        static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void FocusCallback(GLFWwindow* window, int focused);

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

        // --- state ----------------------------------------------------------------------- //

        static constexpr std::size_t KEY_COUNT = static_cast<std::size_t>(KeyCode::Count);

        std::bitset<KEY_COUNT> m_current;    // mutated by callbacks during PollEvents
        std::bitset<KEY_COUNT> m_previous;   // snapshot of m_current from before PollEvents

        GLFWwindow* m_window = nullptr;
    };  // INPUT CLASS

}   // NAMESPACE CRUCIBLE
