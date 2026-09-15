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
            TRICKY: both bitsets MUST start zeroed. If m_previous holds garbage, frame one
            reports every key as just-released. std::bitset zero-initialises, so this is
            mostly about not adding a constructor that breaks it later.
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
            TRICKY: must run BEFORE the window is destroyed - clearing a callback slot needs
            a live window handle, and glfwTerminate takes the whole library down with it.
            Core::CleanUp calls this ahead of WindowManager::Shutdown for exactly that reason.
            TRICKY: clear the callback slots before nulling m_window, or there is nothing left
            to name the window with.
        */
        void Shutdown() override;

        /*!
            \brief
            Copies current -> previous. Core MUST call this immediately BEFORE PollEvents().
            TRICKY: this is the single most important line in the system. Call it after
            PollEvents and every edge query silently returns false forever, while IsDown
            keeps working - so the system looks half-alive and you debug the wrong thing.
            NOT part of ISystem: double-buffered state is specific to input, and Core holds
            named members rather than a container of ISystem*, so it can just call this.
        */
        void BeginFrame();

        // --- wiring ---------------------------------------------------------------------- //

        /*!
            \brief
            Registers the GLFW key and focus callbacks against a window, and publishes the
            static instance pointer the query functions read.
            \param window
            The window owned by WindowManager. Must outlive this system.
            TRICKY: GLFW keeps one callback slot per window per event type, and setting one
            silently evicts whatever was there. WindowManager already owns the framebuffer
            resize slot; the key and focus slots are ours. Check before claiming a third.
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
            TRICKY: ignore GLFW_REPEAT entirely. It is the OS text-repeat firing many times a
            second while a key is held, and has nothing to do with frames. Only GLFW_PRESS
            and GLFW_RELEASE may touch the bitset. Ignore KeyCode::Unknown too.
        */
        void OnKey(const int& glfwKey, const int& glfwAction);

        /*!
            \brief
            Clears every key when the window loses focus. Does nothing on focus gain.
            TRICKY: alt-tab while holding a key and GLFW never delivers the key-up, because
            the window was not focused when it happened. Without this the player comes back
            to a key stuck down forever.
        */
        void OnFocusChanged(const bool& focused);

        // --- state ----------------------------------------------------------------------- //

        static constexpr std::size_t KEY_COUNT = static_cast<std::size_t>(KeyCode::Count);

        std::bitset<KEY_COUNT> m_current;    // mutated by callbacks during PollEvents
        std::bitset<KEY_COUNT> m_previous;   // snapshot of m_current from before PollEvents

        GLFWwindow* m_window = nullptr;
    };  // INPUT CLASS

}   // NAMESPACE CRUCIBLE
