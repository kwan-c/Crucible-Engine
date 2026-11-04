/*************************************************************************
* \file:         Light.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Script access to an entity's Light component.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's Light. Get it with GetComponent<Light>() or Entity.Light:
*
*        var flash = GetComponent<Light>();
*        flash.Enabled = firing;                          // muzzle flash
*        flash.Intensity = 8f + (float)rng.NextDouble() * 4f;  // flicker
*        flash.Color = Color.Lerp(Color.White, Color.Red, alarm);
*
*    Changes are drawn the same frame. A light sits at its entity's position
*    and shines along Transform.Forward (local -Z): aim a spotlight with
*    light.Entity.Transform.LookAt(target).
*
*    C++ side: Engine/src/Scripting/Bindings/LightBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>What kind of light. Same values as LightType in Graphics/LightTypes.h.</summary>
    public enum LightType
    {
        /// <summary>The sun: parallel rays along Forward, everywhere. Position does not matter.</summary>
        Directional = 0,
        /// <summary>A bulb: shines in every direction from its position, up to Range.</summary>
        Point = 1,
        /// <summary>A torch: a cone along Forward, up to Range.</summary>
        Spot = 2,
    }

    /// <summary>An entity's Light component. Unity's Light.</summary>
    public sealed class Light
    {
        private readonly EntityIdentity entity;
        internal Light(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this light is on (move or aim it through Entity.Transform).</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        /// <summary>Directional, Point or Spot.</summary>
        public LightType Type
        {
            get { int v; return GetTypeInternal(I, G, out v) != 0 ? (LightType)v : LightType.Point; }
            set { SetTypeInternal(I, G, (int)value); }
        }

        /// <summary>The light's colour. Alpha is ignored; channels above 1 are allowed.</summary>
        public Color Color
        {
            get { Vector3 v; return GetColorInternal(I, G, out v) != 0 ? new Color(v.X, v.Y, v.Z) : Color.White; }
            set { Vector3 v = new Vector3(value.R, value.G, value.B); SetColorInternal(I, G, ref v); }
        }

        /// <summary>Brightness. Directional: radiance; Point / Spot: falls off with distance.</summary>
        public float Intensity
        {
            get { float v; return GetIntensityInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetIntensityInternal(I, G, ref value); }
        }

        /// <summary>Point / Spot: fades to nothing at this distance, in metres.</summary>
        public float Range
        {
            get { float v; return GetRangeInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetRangeInternal(I, G, ref value); }
        }

        /// <summary>Spot: the outer edge of the cone, in degrees from its centre line.</summary>
        public float SpotAngle
        {
            get { float v; return GetSpotAngleInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetSpotAngleInternal(I, G, ref value); }
        }

        /// <summary>Spot: full brightness inside this angle, fading out to SpotAngle.</summary>
        public float InnerSpotAngle
        {
            get { float v; return GetInnerSpotAngleInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetInnerSpotAngleInternal(I, G, ref value); }
        }

        /// <summary>Switches the light on or off.</summary>
        public bool Enabled
        {
            get { int v; return GetEnabledInternal(I, G, out v) != 0 && v != 0; }
            set { SetEnabledInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>Directional only: the first enabled directional light with this on casts shadows.</summary>
        public bool CastShadows
        {
            get { int v; return GetCastShadowsInternal(I, G, out v) != 0 && v != 0; }
            set { SetCastShadowsInternal(I, G, value ? 1 : 0); }
        }

        public override string ToString() { return Type + " light on " + Entity; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetTypeInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetTypeInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetColorInternal(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetColorInternal(int index, int generation, ref Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetIntensityInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetIntensityInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetRangeInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetRangeInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetSpotAngleInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetSpotAngleInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetInnerSpotAngleInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetInnerSpotAngleInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetEnabledInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetEnabledInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetCastShadowsInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetCastShadowsInternal(int index, int generation, int value);
    }
}
