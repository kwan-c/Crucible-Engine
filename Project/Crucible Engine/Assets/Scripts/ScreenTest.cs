using System;

namespace Crucible.Scripts
{
    // Tests the Screen API (window mode, window size, fullscreen resolution).
    //   1. Attach to any entity.
    //   2. In the EDITOR: press Play and use the keys below - the window must NOT change, and one
    //      "ignored in the editor" line must appear in the log (only once). L still lists sizes.
    //   3. In the GAME BUILD (Mekanomachia.exe):
    //        F8   Windowed            F9   Maximized          F10  Fullscreen (last chosen resolution)
    //        G    toggle FullScreen   H    log the mode and sizes
    //        L    list the monitor's resolutions
    //        1    windowed 1280 x 720      2  windowed 1600 x 900 (height follows the aspect lock)
    //        3    exclusive fullscreen at the next resolution in the list (press again to step on)
    //        4    borderless fullscreen (the monitor's own resolution)
    //      Close the game and start it again: it must open in the last mode, size and resolution.
    //      In exclusive fullscreen, alt-tab must minimize the game and give the desktop its own
    //      resolution back.
    public sealed class ScreenTest : EntityScript
    {
        int nextResolution;

        public override void OnCreate()
        {
            LogScreen();
        }

        void LogScreen()
        {
            Log.Info("[ScreenTest] mode " + Screen.Mode + ", screen " + Screen.Width + " x " + Screen.Height +
                     ", windowed " + Screen.WindowedWidth + " x " + Screen.WindowedHeight +
                     ", fullscreen " + (Screen.IsExclusiveFullscreen
                         ? Screen.FullscreenWidth + " x " + Screen.FullscreenHeight + " @ " + Screen.FullscreenRefreshRate
                         : "borderless") +
                     ", aspect lock " + Screen.AspectRatioLock + ", editor " + Screen.IsEditor);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (Input.IsPressed(KeyCode.F8))  { Screen.Mode = WindowMode.Windowed;   LogScreen(); }
            if (Input.IsPressed(KeyCode.F9))  { Screen.Mode = WindowMode.Maximized;  LogScreen(); }
            if (Input.IsPressed(KeyCode.F10)) { Screen.Mode = WindowMode.Fullscreen; LogScreen(); }
            if (Input.IsPressed(KeyCode.G))   { Screen.FullScreen = !Screen.FullScreen; LogScreen(); }
            if (Input.IsPressed(KeyCode.H))   LogScreen();

            if (Input.IsPressed(KeyCode.L))
            {
                Resolution[] list = Screen.Resolutions;
                Log.Info("[ScreenTest] " + list.Length + " resolutions:");
                foreach (Resolution r in list)
                    Log.Info("[ScreenTest]   " + r);
            }

            if (Input.IsPressed(KeyCode.Num1))
                Log.Info("[ScreenTest] windowed 1280 x 720: " + Screen.SetResolution(1280, 720, WindowMode.Windowed));
            if (Input.IsPressed(KeyCode.Num2))
                Log.Info("[ScreenTest] windowed 1600 x 900: " + Screen.SetResolution(1600, 900, WindowMode.Windowed));
            if (Input.IsPressed(KeyCode.Num3))
            {
                Resolution[] list = Screen.Resolutions;
                if (list.Length == 0) { Log.Warning("[ScreenTest] no resolutions"); }
                else
                {
                    Resolution r = list[nextResolution % list.Length];
                    nextResolution++;
                    Log.Info("[ScreenTest] exclusive fullscreen " + r + ": " +
                             Screen.SetResolution(r.Width, r.Height, WindowMode.Fullscreen, r.RefreshRate));
                }
            }
            if (Input.IsPressed(KeyCode.Num4))
                Log.Info("[ScreenTest] borderless fullscreen: " + Screen.SetBorderlessFullscreen());
            if (Input.IsPressed(KeyCode.Num1) || Input.IsPressed(KeyCode.Num2) ||
                Input.IsPressed(KeyCode.Num3) || Input.IsPressed(KeyCode.Num4))
                LogScreen();
        }
    }
}
