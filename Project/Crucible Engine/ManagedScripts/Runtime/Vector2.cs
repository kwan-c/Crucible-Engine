using System;
using System.Runtime.InteropServices;

namespace Crucible
{
    [StructLayout(LayoutKind.Sequential)]   // fields laid out exactly x, y in memory
    public struct Vector2
    {
        public float X, Y;
        public Vector2(float x, float y) { X = x; Y = y; }

        public static readonly Vector2 Zero = new Vector2(0, 0);
        public static readonly Vector2 One  = new Vector2(1, 1);
        public static readonly Vector2 Up   = new Vector2(0, 1);

        public static Vector2 operator +(Vector2 a, Vector2 b) => new Vector2(a.X + b.X, a.Y + b.Y);
        public static Vector2 operator -(Vector2 a, Vector2 b) => new Vector2(a.X - b.X, a.Y - b.Y);
        public static Vector2 operator *(Vector2 v, float s)   => new Vector2(v.X * s, v.Y * s);
        public float Length => (float)Math.Sqrt(X * X + Y * Y);
        public Vector2 Normalized { get { var l = Length; return l > 1e-6f ? this * (1f / l) : Zero; } }
        public static float Dot(Vector2 a, Vector2 b) => a.X * b.X + a.Y * b.Y;
        public override string ToString() => $"({X}, {Y})";
    }
}