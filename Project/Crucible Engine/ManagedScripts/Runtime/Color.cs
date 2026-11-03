/*************************************************************************
* \file:         Color.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         An RGBA colour for scripts (lights now, UI later).
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Channels are 0..1 (linear). Lights accept values above 1 for "brighter
*    than white" and ignore A. Unity's Color, the commonly used parts.
*
**************************************************************************/
using System;
using System.Runtime.InteropServices;

namespace Crucible
{
    /// <summary>A colour: red, green, blue and alpha (opacity), each 0..1.</summary>
    [StructLayout(LayoutKind.Sequential)]   // r, g, b, a in memory, for future bindings
    public struct Color : IEquatable<Color>
    {
        public float R, G, B, A;

        /// <summary>A colour; alpha defaults to 1 (opaque).</summary>
        public Color(float r, float g, float b, float a = 1f) { R = r; G = g; B = b; A = a; }

        public static readonly Color White   = new Color(1f, 1f, 1f);
        public static readonly Color Black   = new Color(0f, 0f, 0f);
        public static readonly Color Red     = new Color(1f, 0f, 0f);
        public static readonly Color Green   = new Color(0f, 1f, 0f);
        public static readonly Color Blue    = new Color(0f, 0f, 1f);
        public static readonly Color Yellow  = new Color(1f, 0.92f, 0.016f);
        public static readonly Color Cyan    = new Color(0f, 1f, 1f);
        public static readonly Color Magenta = new Color(1f, 0f, 1f);
        public static readonly Color Gray    = new Color(0.5f, 0.5f, 0.5f);
        public static readonly Color Clear   = new Color(0f, 0f, 0f, 0f);

        /// <summary>A colour from 0..255 channel values, e.g. FromBytes(255, 128, 0).</summary>
        public static Color FromBytes(int r, int g, int b, int a = 255)
        {
            return new Color(r / 255f, g / 255f, b / 255f, a / 255f);
        }

        /// <summary>Blends from a (t = 0) to b (t = 1); t is clamped to 0..1.</summary>
        public static Color Lerp(Color a, Color b, float t)
        {
            t = t < 0f ? 0f : (t > 1f ? 1f : t);
            return new Color(a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t, a.A + (b.A - a.A) * t);
        }

        public static Color operator *(Color c, float s) => new Color(c.R * s, c.G * s, c.B * s, c.A);
        public static Color operator *(Color a, Color b) => new Color(a.R * b.R, a.G * b.G, a.B * b.B, a.A * b.A);
        public static Color operator +(Color a, Color b) => new Color(a.R + b.R, a.G + b.G, a.B + b.B, a.A + b.A);

        public bool Equals(Color other) => R == other.R && G == other.G && B == other.B && A == other.A;
        public override bool Equals(object obj) => obj is Color other && Equals(other);
        public override int GetHashCode()
        {
            unchecked { return ((R.GetHashCode() * 397 ^ G.GetHashCode()) * 397 ^ B.GetHashCode()) * 397 ^ A.GetHashCode(); }
        }
        public static bool operator ==(Color a, Color b) => a.Equals(b);
        public static bool operator !=(Color a, Color b) => !a.Equals(b);

        public override string ToString() => $"RGBA({R}, {G}, {B}, {A})";
    }
}
