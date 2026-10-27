/*************************************************************************
* \file:         KeyCode.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          13 September 2026
* \version       1.2
* \brief
*                KeyCode: the engine's own ids for keys, mouse buttons and
*                gamepad buttons, so nothing above the input layer depends
*                on GLFW.
*
* \details
*                Dense and contiguous so they can index a bitset directly.
*                Add new keys before Count, and never give them explicit
*                numbers.
*
* \history
*    -v1.0       keyboard keys
*    -v1.1       mouse buttons
*    -v1.2       gamepad buttons (Xbox names), triggers as buttons too
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>

#include "EngineExport.h"

namespace Crucible {

    /*!
        \brief
        Engine-owned key identifiers. Deliberately dense and contiguous so they can index
        a bitset directly, and deliberately NOT equal to GLFW's values - nothing above
        Layer 1 should ever know which windowing library we happen to be using.

        Add new keys before Count. Never assign explicit numbers; the values are an
        implementation detail and must stay contiguous.
    */
    enum class KeyCode : std::uint16_t {

        Unknown = 0,

        // Letters
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

        // Number row
        Num0, Num1, Num2, Num3, Num4,
        Num5, Num6, Num7, Num8, Num9,

        // Function keys
        F1, F2, F3, F4, F5, F6,
        F7, F8, F9, F10, F11, F12,

        // Navigation
        Left, Right, Up, Down,
        Home, End, PageUp, PageDown,
        Insert, Delete,

        // Editing and whitespace
        Space, Enter, Tab, Backspace, Escape,

        // Punctuation
        Apostrophe, Comma, Minus, Period, Slash,
        Semicolon, Equal, LeftBracket, Backslash, RightBracket, GraveAccent,

        // Modifiers (left and right kept separate - rebinding UIs need the distinction)
        LeftShift, LeftControl, LeftAlt, LeftSuper,
        RightShift, RightControl, RightAlt, RightSuper,

        // Locks
        CapsLock, NumLock, ScrollLock, PrintScreen, Pause,

        // Keypad
        KP0, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
        KPDecimal, KPDivide, KPMultiply, KPSubtract, KPAdd, KPEnter, KPEqual,

        // Mouse buttons. They are KeyCodes on purpose: one bitset, one set of queries,
        // and a binding can name a mouse button anywhere it can name a key. Unity's
        // old KeyCode does the same thing with Mouse0..Mouse6.
        MouseLeft, MouseRight, MouseMiddle,
        MouseButton4, MouseButton5, MouseButton6, MouseButton7, MouseButton8,

        // Gamepad buttons, by their Xbox names (GLFW's own); PlayStation and Switch in
        // brackets. KeyCodes for the same reason the mouse buttons are: IsDown / IsPressed /
        // IsReleased work on them unchanged. They read the FIRST connected gamepad - one
        // local player; the other mechs arrive over the network.
        // Kept in the exact order of GLFW_GAMEPAD_BUTTON_A .. DPAD_LEFT, so translating one
        // is a single addition (KeyCode.cpp checks the order at compile time).
        GamepadA,           // bottom face button      (PS: Cross,    Switch: B)
        GamepadB,           // right face button       (PS: Circle,   Switch: A)
        GamepadX,           // left face button        (PS: Square,   Switch: Y)
        GamepadY,           // top face button         (PS: Triangle, Switch: X)
        GamepadLB,          // left bumper             (PS: L1)
        GamepadRB,          // right bumper            (PS: R1)
        GamepadBack,        // View / Back             (PS: Share / Create)
        GamepadStart,       // Menu / Start            (PS: Options)
        GamepadGuide,       // the big logo button - Windows often keeps it for itself
        GamepadLeftStick,   // left stick pressed in   (PS: L3)
        GamepadRightStick,  // right stick pressed in  (PS: R3)
        GamepadDpadUp, GamepadDpadRight, GamepadDpadDown, GamepadDpadLeft,

        // The triggers are analog (Input::GetGamepadLeftTrigger) but also count as held once
        // pulled past halfway, so "fire on RT" is simply Input::IsPressed(KeyCode::GamepadRT).
        GamepadLT,          // (PS: L2)
        GamepadRT,          // (PS: R2)

        Count    // must stay last - this is the bitset size

    };  // KEYCODE ENUM

    /*!
        \brief
        Translates a raw GLFW key value into a Crucible KeyCode.
        \param glfwKey
        A GLFW_KEY_* value as delivered by the key callback.
        \return
        The matching KeyCode, or KeyCode::Unknown for keys we do not track.
    */
    ENGINE_API KeyCode KeyCodeFromGlfw(const int& glfwKey);

    /*!
        \brief
        Translates a raw GLFW mouse-button value into a Crucible KeyCode.
        \param glfwButton
        A GLFW_MOUSE_BUTTON_* value as delivered by the mouse-button callback.
        \return
        The matching KeyCode, or KeyCode::Unknown for buttons we do not track.
        NOTE: separate from KeyCodeFromGlfw because GLFW numbers buttons from zero in
        their own space - a button value and a key value are not comparable.
    */
    ENGINE_API KeyCode KeyCodeFromGlfwMouseButton(const int& glfwButton);

    /*!
        \brief
        Translates a GLFW gamepad-button index into a Crucible KeyCode.
        \param glfwButton
        A GLFW_GAMEPAD_BUTTON_* value: an index into GLFWgamepadstate::buttons.
        \return
        The matching KeyCode, or KeyCode::Unknown for an index GLFW does not define.
        NOTE: the triggers are axes in GLFW, so GamepadLT / GamepadRT never come from here -
        Input sets them from the trigger values.
    */
    KeyCode KeyCodeFromGlfwGamepadButton(const int& glfwButton);

    /*!
        \brief
        Human-readable name for a key, for logs and rebinding UIs.
        \return
        A static string such as "Space". Never null.
    */
    ENGINE_API const char* KeyCodeToString(const KeyCode& key);

    // Key + modifier shortcuts ("Shift+F1") and KeyCodeFromString: Input/KeyChord.h.

}   // NAMESPACE CRUCIBLE
