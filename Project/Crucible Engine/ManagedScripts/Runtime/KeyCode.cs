/*************************************************************************
* \file:         KeyCode.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         C# copy of the engine's KeyCode enum (Engine/src/Input/KeyCode.h).
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    The values are passed straight to C++, so this list must stay in the
*    EXACT order of KeyCode.h, with no explicit numbers. When a key is added
*    there, add it here in the same place. Two checks catch a mismatch:
*    a static_assert in Bindings/InputBindings.cpp, and Input.cs comparing
*    Count with the engine's count when it first loads.
*
**************************************************************************/
namespace Crucible
{
    /// <summary>
    /// Keys, mouse buttons and gamepad buttons, for Input.IsDown / IsPressed / IsReleased.
    /// Gamepad buttons use Xbox names and read the first connected pad.
    /// </summary>
    public enum KeyCode
    {
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

        // Modifiers
        LeftShift, LeftControl, LeftAlt, LeftSuper,
        RightShift, RightControl, RightAlt, RightSuper,

        // Locks
        CapsLock, NumLock, ScrollLock, PrintScreen, Pause,

        // Keypad
        KP0, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
        KPDecimal, KPDivide, KPMultiply, KPSubtract, KPAdd, KPEnter, KPEqual,

        // Mouse buttons
        MouseLeft, MouseRight, MouseMiddle,
        MouseButton4, MouseButton5, MouseButton6, MouseButton7, MouseButton8,

        // Gamepad buttons (Xbox names; PlayStation in brackets)
        GamepadA,           // bottom face button (Cross)
        GamepadB,           // right face button  (Circle)
        GamepadX,           // left face button   (Square)
        GamepadY,           // top face button    (Triangle)
        GamepadLB,          // left bumper        (L1)
        GamepadRB,          // right bumper       (R1)
        GamepadBack,        // View / Back        (Share / Create)
        GamepadStart,       // Menu / Start       (Options)
        GamepadGuide,       // logo button - Windows often keeps it for itself
        GamepadLeftStick,   // left stick pressed in  (L3)
        GamepadRightStick,  // right stick pressed in (R3)
        GamepadDpadUp, GamepadDpadRight, GamepadDpadDown, GamepadDpadLeft,

        // Triggers count as held once pulled past halfway (analog: Input.GamepadLeftTrigger)
        GamepadLT,          // (L2)
        GamepadRT,          // (R2)

        /// <summary>Number of keys. Not a key - must stay last.</summary>
        Count
    }
}
