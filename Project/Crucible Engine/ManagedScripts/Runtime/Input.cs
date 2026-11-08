/*************************************************************************
* \file:         Input.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         Keyboard, mouse and gamepad queries for scripts.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Wraps the engine's Input system (Engine/src/Input). The engine takes
*    one snapshot per frame, so read input in OnUpdate: "pressed this frame"
*    is true for exactly one OnUpdate per press.
*
*        public override void OnUpdate(float deltaTime)
*        {
*            if (Input.IsPressed(KeyCode.Space)) Jump();
*            Vector2 look = Input.MouseDelta;   // pixels this frame - do not scale by deltaTime
*        }
*
*    The cursor lock is the game's to ask for: Input.SetMouseLocked(true)
*    when gameplay starts, false for a menu of your own. Nothing locks it
*    automatically. A menu canvas (or the editor's release key / Pause)
*    can still hold it free; your latest request applies once it lets go.
*
*    C++ side: Engine/src/Scripting/Bindings/InputBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>
    /// Keyboard, mouse and gamepad state for the current frame.
    /// </summary>
    public static class Input
    {
        // False when KeyCode.cs and the engine's KeyCode.h disagree; key queries
        // then return false instead of reading the wrong key.
        private static readonly bool keyCodesMatch;

        // Runs once, the first time a script touches Input (again after each hot reload).
        static Input()
        {
            int engineCount = GetKeyCodeCountInternal();
            keyCodesMatch = engineCount == (int)KeyCode.Count;
            if (!keyCodesMatch)
                Log.Error("Input: KeyCode.cs has " + (int)KeyCode.Count + " keys but the engine has " + engineCount +
                          ". Rebuild Crucible.ScriptCore.dll; key queries are disabled until then.");
        }

        // --- keys, mouse buttons and gamepad buttons ------------------------------ //

        /// <summary>Is the key held right now? True every frame it is down.</summary>
        public static bool IsDown(KeyCode key)
        {
            int result;
            return keyCodesMatch && IsDownInternal((int)key, out result) != 0 && result != 0;
        }

        /// <summary>Did the key go down this frame? True for one frame per press.</summary>
        public static bool IsPressed(KeyCode key)
        {
            int result;
            return keyCodesMatch && IsPressedInternal((int)key, out result) != 0 && result != 0;
        }

        /// <summary>Did the key come up this frame? True for one frame per release.</summary>
        public static bool IsReleased(KeyCode key)
        {
            int result;
            return keyCodesMatch && IsReleasedInternal((int)key, out result) != 0 && result != 0;
        }

        // --- mouse ----------------------------------------------------------------- //

        /// <summary>Cursor position in pixels, origin at the top-left of the window.</summary>
        public static Vector2 MousePosition
        {
            get { Vector2 v; return GetMousePositionInternal(out v) != 0 ? v : Vector2.Zero; }
        }

        /// <summary>
        /// How far the mouse moved this frame, in pixels. Multiply by a sensitivity,
        /// but NOT by deltaTime - it is already a distance.
        /// </summary>
        public static Vector2 MouseDelta
        {
            get { Vector2 v; return GetMouseDeltaInternal(out v) != 0 ? v : Vector2.Zero; }
        }

        /// <summary>Wheel movement this frame. Y is the normal wheel, positive away from you; one notch is 1.</summary>
        public static Vector2 MouseScroll
        {
            get { Vector2 v; return GetMouseScrollInternal(out v) != 0 ? v : Vector2.Zero; }
        }

        /// <summary>Is the cursor hidden and captured (mouse-look mode) right now?</summary>
        public static bool IsMouseLocked
        {
            get { int v; return IsMouseLockedInternal(out v) != 0 && v != 0; }
        }

        /// <summary>
        /// Asks for the cursor to be hidden and captured (true) or free (false). Call it when
        /// gameplay starts or resumes, and with false for a menu of your own. While a menu
        /// canvas is shown, or the editor has taken the mouse back, the cursor stays free and
        /// this request applies once they let go.
        /// </summary>
        public static void SetMouseLocked(bool locked)
        {
            SetMouseLockedInternal(locked ? 1 : 0);
        }

        /// <summary>
        /// Is the mouse over game UI that blocks clicks? Check it before acting on a click,
        /// so pressing a menu button does not also fire a weapon.
        /// </summary>
        public static bool IsPointerOverUI
        {
            get { int v; return IsPointerOverUIInternal(out v) != 0 && v != 0; }
        }

        // --- gamepad (first connected pad) ----------------------------------------- //

        /// <summary>Is a gamepad connected?</summary>
        public static bool IsGamepadConnected
        {
            get { int v; return IsGamepadConnectedInternal(out v) != 0 && v != 0; }
        }

        /// <summary>The connected pad's name, e.g. "Xbox Controller"; empty when none.</summary>
        public static string GamepadName
        {
            get { return GetGamepadNameInternal() ?? string.Empty; }
        }

        /// <summary>Left stick, each axis -1..1, right and up positive, dead zone applied.</summary>
        public static Vector2 GamepadLeftStick
        {
            get { Vector2 v; return GetGamepadLeftStickInternal(out v) != 0 ? v : Vector2.Zero; }
        }

        /// <summary>Right stick, same as the left. Unlike the mouse, multiply by deltaTime.</summary>
        public static Vector2 GamepadRightStick
        {
            get { Vector2 v; return GetGamepadRightStickInternal(out v) != 0 ? v : Vector2.Zero; }
        }

        /// <summary>Left trigger, 0 released to 1 fully pulled.</summary>
        public static float GamepadLeftTrigger
        {
            get { float v; return GetGamepadLeftTriggerInternal(out v) != 0 ? v : 0f; }
        }

        /// <summary>Right trigger, 0 released to 1 fully pulled.</summary>
        public static float GamepadRightTrigger
        {
            get { float v; return GetGamepadRightTriggerInternal(out v) != 0 ? v : 0f; }
        }

        // --- internal calls (InputBindings.cpp) ------------------------------------ //

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetKeyCodeCountInternal();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsDownInternal(int key, out int result);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsPressedInternal(int key, out int result);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsReleasedInternal(int key, out int result);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMousePositionInternal(out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMouseDeltaInternal(out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMouseScrollInternal(out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsMouseLockedInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetMouseLockedInternal(int locked);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsPointerOverUIInternal(out int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsGamepadConnectedInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetGamepadNameInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetGamepadLeftStickInternal(out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetGamepadRightStickInternal(out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetGamepadLeftTriggerInternal(out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetGamepadRightTriggerInternal(out float value);
    }
}
