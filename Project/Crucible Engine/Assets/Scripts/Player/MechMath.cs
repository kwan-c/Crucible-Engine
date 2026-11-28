/*************************************************************************
* \file:         MechMath.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          7 November 2026
* \version       1.0
* \brief         Small maths helpers the mech player scripts need that the
*                engine's Vector3 / Quaternion do not have (Unity's Mathf,
*                Vector3.MoveTowards, ProjectOnPlane, ClampMagnitude).
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    public static class MechMath
    {
        public static float Clamp(float value, float min, float max) { return value < min ? min : value > max ? max : value; }
        public static float Clamp01(float value) { return Clamp(value, 0f, 1f); }
        public static float Lerp(float a, float b, float t) { return a + (b - a) * Clamp01(t); }
        public static float InverseLerp(float a, float b, float value) { return Math.Abs(b - a) < 1e-6f ? 0f : Clamp01((value - a) / (b - a)); }

        /// <summary>Unity's Mathf.SmoothStep(0, 1, t).</summary>
        public static float SmoothStep01(float t) { t = Clamp01(t); return t * t * (3f - 2f * t); }

        /// <summary>Frame-rate independent blend factor: 1 - e^(-sharpness * dt).</summary>
        public static float Damp(float sharpness, float deltaTime) { return 1f - (float)Math.Exp(-sharpness * deltaTime); }

        public static float SqrLength(Vector3 v) { return v.X * v.X + v.Y * v.Y + v.Z * v.Z; }

        /// <summary>Drops the vertical part (Unity's ProjectOnPlane(v, up)).</summary>
        public static Vector3 Flat(Vector3 v) { return new Vector3(v.X, 0f, v.Z); }

        public static Vector3 ClampLength(Vector3 v, float maxLength)
        {
            float length = v.Length;
            return length > maxLength && length > 1e-6f ? v * (maxLength / length) : v;
        }

        public static Vector2 ClampLength(Vector2 v, float maxLength)
        {
            float length = v.Length;
            return length > maxLength && length > 1e-6f ? v * (maxLength / length) : v;
        }

        /// <summary>Unity's Vector3.MoveTowards.</summary>
        public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDelta)
        {
            Vector3 delta = target - current;
            float distance = delta.Length;
            if (distance <= maxDelta || distance < 1e-6f) return target;
            return current + delta * (maxDelta / distance);
        }

        public static bool Finite(float value) { return !float.IsNaN(value) && !float.IsInfinity(value); }

        /// <summary>
        /// "RRGGBB" or "RRGGBBAA" (a leading '#' is allowed) as a colour. The Inspector cannot edit
        /// Color fields, so designer-facing colours are hex strings. False keeps the fallback.
        /// </summary>
        public static bool TryParseColour(string hex, out Color colour)
        {
            colour = Color.White;
            if (string.IsNullOrEmpty(hex)) return false;
            hex = hex.Trim().TrimStart('#');
            if (hex.Length != 6 && hex.Length != 8) return false;
            uint value;
            if (!uint.TryParse(hex, System.Globalization.NumberStyles.HexNumber, System.Globalization.CultureInfo.InvariantCulture, out value)) return false;
            if (hex.Length == 6) value = (value << 8) | 0xFFu;
            colour = new Color(((value >> 24) & 0xFF) / 255f, ((value >> 16) & 0xFF) / 255f, ((value >> 8) & 0xFF) / 255f, (value & 0xFF) / 255f);
            return true;
        }

        /// <summary>The colour from hex, or fallback when hex is empty or malformed.</summary>
        public static Color ColourOr(string hex, Color fallback) { Color c; return TryParseColour(hex, out c) ? c : fallback; }
    }
}
