using System;
using System.Runtime.InteropServices;

namespace Crucible
{
    [StructLayout(LayoutKind.Sequential)]   // fields laid out exactly x, y, z in memory
    public struct Vector3
    {
        public float X, Y, Z;
        public Vector3(float x, float y, float z) { X = x; Y = y; Z = z; }

        public static readonly Vector3 Zero = new Vector3(0, 0, 0);
        public static readonly Vector3 One  = new Vector3(1, 1, 1);
        public static readonly Vector3 Up   = new Vector3(0, 1, 0);

        public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        public static Vector3 operator *(Vector3 v, float s)   => new Vector3(v.X * s, v.Y * s, v.Z * s);
        public float Length => (float)Math.Sqrt(X * X + Y * Y + Z * Z);
        public Vector3 Normalized { get { var l = Length; return l > 1e-6f ? this * (1f / l) : Zero; } }
        public static float Dot(Vector3 a, Vector3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        public static Vector3 Cross(Vector3 a, Vector3 b) =>
            new Vector3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
        public override string ToString() => $"({X}, {Y}, {Z})";
    }
}