/*************************************************************************
* \file:         CameraOrbit.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Script access to the player's third-person camera pivot
*                (the CameraOrbit component, Player/PlayerComponents.h).
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    The player controller turns this pivot with the mouse / right stick
*    every frame. Scripts can tune the feel (aim-down-sights sensitivity,
*    pitch limits) or set the view directly (a recoil kick, snapping behind
*    the mech):
*
*        var orbit = GetComponent<CameraOrbit>();
*        orbit.Sensitivity = aiming ? 0.05f : 0.1f;
*        orbit.Pitch += recoil;
*
*    The player's own sensitivity setting multiplies these on top.
*
*    C++ side: Engine/src/Scripting/Bindings/CameraBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>The player's third-person camera pivot, turned by the mouse / right stick.</summary>
    public sealed class CameraOrbit
    {
        private readonly EntityIdentity entity;
        internal CameraOrbit(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>Degrees turned per pixel of mouse movement.</summary>
        public float Sensitivity
        {
            get { float v; return GetSensitivityInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetSensitivityInternal(I, G, ref value); }
        }

        /// <summary>Degrees per second with the right stick fully tilted.</summary>
        public float StickSpeed
        {
            get { float v; return GetStickSpeedInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetStickSpeedInternal(I, G, ref value); }
        }

        /// <summary>Lowest pitch in degrees (below 0 the camera dips under the pivot).</summary>
        public float MinPitch
        {
            get { float v; return GetMinPitchInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetMinPitchInternal(I, G, ref value); }
        }

        /// <summary>Highest pitch in degrees (keep it short of 90).</summary>
        public float MaxPitch
        {
            get { float v; return GetMaxPitchInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetMaxPitchInternal(I, G, ref value); }
        }

        /// <summary>Current angle round the pivot, degrees. + turns the view left.</summary>
        public float Yaw
        {
            get { float v; return GetYawInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetYawInternal(I, G, ref value); }
        }

        /// <summary>Current pitch, degrees. + raises the camera to look down. Kept within Min/MaxPitch by the controller.</summary>
        public float Pitch
        {
            get { float v; return GetPitchInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetPitchInternal(I, G, ref value); }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetSensitivityInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetSensitivityInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetStickSpeedInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetStickSpeedInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMinPitchInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetMinPitchInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMaxPitchInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetMaxPitchInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetYawInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetYawInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetPitchInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetPitchInternal(int index, int generation, ref float value);
    }
}
