/*************************************************************************
* \file:         InputSettings.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         The player's controls settings: the mech's keybinds,
*                look sensitivity and invert Y. Part of
*                Crucible.ScriptCore.dll.
*
* \details
*    These are the PLAYER'S settings (UserSettings.json, next to the .exe),
*    not the scene's: the same values for every scene and every mech, kept
*    between launches, never in git. Build an options menu on them:
*
*        InputSettings.JumpKey = KeyCode.F;       // applies this frame
*        InputSettings.MouseSensitivity = slider;
*        InputSettings.Save();                    // on "Apply" / menu close
*
*    A change applies at once; Save() writes it to the file so the next
*    launch starts with it (Audio.SaveSettings writes the same file - either
*    saves everything). Not undone by Stop: like the audio volumes, a change
*    made in Play stays until the editor closes, and is kept for good only
*    once saved.
*
*    Keys: any KeyCode (mouse and gamepad buttons too); KeyCode.Unknown =
*    unbound. The gamepad's left stick always walks.
*
*    C++ side: Engine/src/Scripting/Bindings/InputSettingsBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>The player's keybinds, look sensitivity and invert Y (UserSettings.json).</summary>
    public static class InputSettings
    {
        // --- keybinds ------------------------------------------------------------ //

        /// <summary>Walk forward (away from the camera). Default W.</summary>
        public static KeyCode ForwardKey
        {
            get { int v; return GetForwardKeyInternal(out v) != 0 ? (KeyCode)v : KeyCode.Unknown; }
            set { SetForwardKeyInternal((int)value); }
        }

        /// <summary>Walk back. Default S.</summary>
        public static KeyCode BackKey
        {
            get { int v; return GetBackKeyInternal(out v) != 0 ? (KeyCode)v : KeyCode.Unknown; }
            set { SetBackKeyInternal((int)value); }
        }

        /// <summary>Walk left. Default A.</summary>
        public static KeyCode LeftKey
        {
            get { int v; return GetLeftKeyInternal(out v) != 0 ? (KeyCode)v : KeyCode.Unknown; }
            set { SetLeftKeyInternal((int)value); }
        }

        /// <summary>Walk right. Default D.</summary>
        public static KeyCode RightKey
        {
            get { int v; return GetRightKeyInternal(out v) != 0 ? (KeyCode)v : KeyCode.Unknown; }
            set { SetRightKeyInternal((int)value); }
        }

        /// <summary>Jump. Default Space.</summary>
        public static KeyCode JumpKey
        {
            get { int v; return GetJumpKeyInternal(out v) != 0 ? (KeyCode)v : KeyCode.Unknown; }
            set { SetJumpKeyInternal((int)value); }
        }

        /// <summary>A second jump binding, for the gamepad. Default GamepadA.</summary>
        public static KeyCode JumpButton
        {
            get { int v; return GetJumpButtonInternal(out v) != 0 ? (KeyCode)v : KeyCode.Unknown; }
            set { SetJumpButtonInternal((int)value); }
        }

        /// <summary>All keybinds back to W A S D / Space / GamepadA. Sensitivity is left alone.</summary>
        public static void ResetKeys() { ResetKeysInternal(); }

        // --- look ---------------------------------------------------------------- //

        /// <summary>Mouse look multiplier, 0.1 .. 10 (1 = as the designer tuned it).</summary>
        public static float MouseSensitivity
        {
            get { float v; return GetMouseSensitivityInternal(out v) != 0 ? v : 1f; }
            set { SetMouseSensitivityInternal(ref value); }
        }

        /// <summary>Right-stick look multiplier, 0.1 .. 10.</summary>
        public static float StickSensitivity
        {
            get { float v; return GetStickSensitivityInternal(out v) != 0 ? v : 1f; }
            set { SetStickSensitivityInternal(ref value); }
        }

        /// <summary>Up on the mouse / right stick looks down.</summary>
        public static bool InvertY
        {
            get { int v; return GetInvertYInternal(out v) != 0 && v != 0; }
            set { SetInvertYInternal(value ? 1 : 0); }
        }

        /// <summary>Writes every player setting to UserSettings.json. False if it could not.</summary>
        public static bool Save() { return SaveInternal() != 0; }

        // --- internal calls (InputSettingsBindings.cpp) -------------------------- //

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetForwardKeyInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetForwardKeyInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBackKeyInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetBackKeyInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetLeftKeyInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetLeftKeyInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetRightKeyInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetRightKeyInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetJumpKeyInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetJumpKeyInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetJumpButtonInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetJumpButtonInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int ResetKeysInternal();

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetMouseSensitivityInternal(out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetMouseSensitivityInternal(ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetStickSensitivityInternal(out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetStickSensitivityInternal(ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetInvertYInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetInvertYInternal(int value);

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SaveInternal();
    }
}
