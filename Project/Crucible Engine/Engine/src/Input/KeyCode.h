#pragma once

// STL
#include <cstdint>

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
    KeyCode KeyCodeFromGlfw(const int& glfwKey);

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
    KeyCode KeyCodeFromGlfwMouseButton(const int& glfwButton);

    /*!
        \brief
        Human-readable name for a key, for logs and rebinding UIs.
        \return
        A static string such as "Space". Never null.
    */
    const char* KeyCodeToString(const KeyCode& key);

}   // NAMESPACE CRUCIBLE
