/*************************************************************************
* \file:         Time.cs
* \author:       <author>
* \par:          Email: <email>
* \date          2 November 2026
* \version       1.0
* \brief         Frame timing for scripts. Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's Time, the parts that exist here:
*
*        if (Time.ElapsedTime - lastShot >= cooldown) { Fire(); lastShot = Time.ElapsedTime; }
*
*    ElapsedTime and FrameCount count Playing time since Play was pressed; Pause
*    stops them and Stop resets them. DeltaTime is the same value OnUpdate
*    receives.
*
*    C++ side: Engine/src/Scripting/Bindings/TimeBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>Frame timing. Unity's Time.</summary>
    public static class Time
    {
        /// <summary>Seconds since the previous frame (what OnUpdate gets).</summary>
        public static float DeltaTime
        {
            get { float v; return GetDeltaTimeInternal(out v) != 0 ? v : 0f; }
        }

        /// <summary>Seconds of Playing time since Play started. Pause excluded.</summary>
        public static float ElapsedTime
        {
            get { double v; return GetTimeInternal(out v) != 0 ? (float)v : 0f; }
        }

        /// <summary>Same as ElapsedTime, as a double, for long sessions where float loses precision.</summary>
        public static double ElapsedTimeAsDouble
        {
            get { double v; return GetTimeInternal(out v) != 0 ? v : 0.0; }
        }

        /// <summary>Playing frames since Play started.</summary>
        public static int FrameCount
        {
            get { int v; return GetFrameCountInternal(out v) != 0 ? v : 0; }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetDeltaTimeInternal(out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetTimeInternal(out double value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetFrameCountInternal(out int value);
    }
}
