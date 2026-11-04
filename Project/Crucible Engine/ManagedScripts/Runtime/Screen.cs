/*************************************************************************
* \file:         Screen.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         The game window: fullscreen, maximized or windowed.
*                Unity's Screen. Part of Crucible.ScriptCore.dll.
*
* \details
*        Screen.Mode = WindowMode.Fullscreen;    // options menu
*        Screen.FullScreen = !Screen.FullScreen; // a toggle
*
*    Fullscreen is borderless on the monitor the window is on, at that
*    monitor's resolution (quick to alt-tab) - unless SetResolution picked
*    another one ("exclusive": the monitor switches mode). Maximized fills
*    the monitor but keeps the taskbar and title bar. A change is saved to
*    UserSettings.json straight away, so the next launch opens the same way -
*    no Save() needed (F11 / Alt+Enter do the same).
*
*        foreach (var r in Screen.Resolutions) ...   // an options menu's list
*        Screen.SetResolution(1280, 720, WindowMode.Windowed);
*        Screen.SetResolution(1920, 1080, WindowMode.Fullscreen);
*        Screen.SetBorderlessFullscreen();
*
*    IN THE EDITOR, CHANGES ARE IGNORED (as in Unity): the window is the
*    editor itself. Setting returns nothing and logs once; test in the game
*    build. Reading works in both (IsEditor tells you which).
*
*    C++ side: Engine/src/Scripting/Bindings/ScreenBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>How the game window is shown.</summary>
    public enum WindowMode
    {
        /// <summary>A normal window with a title bar, at its windowed size.</summary>
        Windowed = 0,
        /// <summary>Fills the monitor's work area; the taskbar and title bar stay.</summary>
        Maximized = 1,
        /// <summary>Borderless, covering the whole monitor at its own resolution.</summary>
        Fullscreen = 2,
    }

    /// <summary>One size a monitor can show fullscreen. Unity's Resolution.</summary>
    public struct Resolution
    {
        public int Width { get; private set; }
        public int Height { get; private set; }
        /// <summary>Hz; the highest the monitor offers at this size.</summary>
        public int RefreshRate { get; private set; }

        internal Resolution(int width, int height, int refreshRate) : this()
        {
            Width = width;
            Height = height;
            RefreshRate = refreshRate;
        }

        public override string ToString() { return Width + " x " + Height + " @ " + RefreshRate + "Hz"; }
    }

    /// <summary>The game window. Unity's Screen. Changes are ignored in the editor.</summary>
    public static class Screen
    {
        /// <summary>Windowed, Maximized or Fullscreen. Setting it is ignored in the editor.</summary>
        public static WindowMode Mode
        {
            get { int v; return GetModeInternal(out v) != 0 ? (WindowMode)v : WindowMode.Windowed; }
            set { SetModeInternal((int)value); }
        }

        /// <summary>
        /// Fullscreen on / off. Turning it off goes back to maximized or windowed, whichever
        /// the window was before. Setting it is ignored in the editor.
        /// </summary>
        public static bool FullScreen
        {
            get { return Mode == WindowMode.Fullscreen; }
            set { SetFullScreenInternal(value ? 1 : 0); }
        }

        /// <summary>True in the editor, where Mode / FullScreen changes are ignored.</summary>
        public static bool IsEditor
        {
            get { int v; return GetIsEditor(out v) && v != 0; }
        }

        /// <summary>Width of the game screen in pixels (the Game view in the editor).</summary>
        public static int Width { get { return (int)Camera.ScreenSize.X; } }

        /// <summary>Height of the game screen in pixels (the Game view in the editor).</summary>
        public static int Height { get { return (int)Camera.ScreenSize.Y; } }

        /// <summary>Width / height the window is locked to (the game locks it to the project's shape), or 0.</summary>
        public static float AspectRatioLock
        {
            get { float v; return GetAspectLockInternal(out v) != 0 ? v : 0f; }
        }

        // --- resolution ------------------------------------------------------------ //

        /// <summary>
        /// Unity's Screen.SetResolution. Fullscreen: switches the monitor to width x height
        /// ("exclusive" - the screen blinks, and alt-tab minimizes the game); it must be one of
        /// Resolutions, refreshRate 0 = the highest at that size. Windowed / Maximized: sets the
        /// window's size (Maximized uses it when restored; kept inside the screen, and the height
        /// follows the aspect lock). Saved for the next launch. False in the editor or for a size
        /// the monitor does not offer.
        /// </summary>
        public static bool SetResolution(int width, int height, WindowMode mode, int refreshRate = 0)
        {
            return SetResolutionInternal(width, height, (int)mode, refreshRate) != 0;
        }

        /// <summary>Fullscreen at the monitor's own resolution (the default; quick alt-tab). Forgets a chosen one.</summary>
        public static bool SetBorderlessFullscreen() { return SetBorderlessInternal() != 0; }

        /// <summary>The window's size when windowed (also while maximized / fullscreen: the size it returns to).</summary>
        public static int WindowedWidth { get { int[] s = Sizes(); return s[0]; } }
        /// <summary>See WindowedWidth.</summary>
        public static int WindowedHeight { get { int[] s = Sizes(); return s[1]; } }

        /// <summary>The resolution fullscreen uses, or 0 when borderless (the monitor's own).</summary>
        public static int FullscreenWidth { get { int[] s = Sizes(); return s[2]; } }
        /// <summary>See FullscreenWidth.</summary>
        public static int FullscreenHeight { get { int[] s = Sizes(); return s[3]; } }
        /// <summary>Fullscreen refresh rate in Hz, or 0 for the highest / borderless.</summary>
        public static int FullscreenRefreshRate { get { int[] s = Sizes(); return s[4]; } }
        /// <summary>True if fullscreen switches the monitor to a chosen resolution.</summary>
        public static bool IsExclusiveFullscreen { get { return FullscreenWidth > 0; } }

        /// <summary>The sizes the window's monitor can show fullscreen, smallest first (one per size).</summary>
        public static Resolution[] Resolutions
        {
            get
            {
                int count;
                if (GetResolutionCountInternal(out count) == 0 || count <= 0)
                    return new Resolution[0];
                var list = new Resolution[count];
                for (int i = 0; i < count; ++i)
                {
                    int w, h, r;
                    if (GetResolutionInternal(i, out w, out h, out r) != 0)
                        list[i] = new Resolution(w, h, r);
                }
                return list;
            }
        }

        private static int[] Sizes()
        {
            int ww, wh, fw, fh, fr;
            if (GetSizesInternal(out ww, out wh, out fw, out fh, out fr) == 0)
                return new int[5];
            return new[] { ww, wh, fw, fh, fr };
        }

        private static bool GetIsEditor(out int value) { return IsEditorInternal(out value) != 0; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetResolutionInternal(int width, int height, int mode, int refreshRate);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetBorderlessInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetSizesInternal(out int windowedWidth, out int windowedHeight, out int fullscreenWidth, out int fullscreenHeight, out int fullscreenRefresh);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetResolutionCountInternal(out int count);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetResolutionInternal(int index, out int width, out int height, out int refreshRate);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetModeInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetModeInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetFullScreenInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsEditorInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetAspectLockInternal(out float value);
    }
}
