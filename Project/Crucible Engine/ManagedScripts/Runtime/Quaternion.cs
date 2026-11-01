using System;
using System.Runtime.InteropServices;

namespace Crucible
{
    // A rotation, stored as a unit quaternion.
    //
    // Conventions match the native engine (glm) and the Editor Inspector:
    //  - Memory/field order is X, Y, Z, W. Note the constructor takes (x, y, z, w),
    //    which differs from glm's (w, x, y, z) constructor. Native bindings must copy
    //    components by name, never reinterpret a glm::quat's memory.
    //  - Euler angles are in DEGREES and use the same order as glm::eulerAngles /
    //    glm::quat(vec3), so values typed in the Inspector mean the same thing here.
    //  - Right-handed, +Y up, -Z forward (see Graphics/Camera.h).
    //  - a * b applies b first, then a (same as glm).
    //
    // default(Quaternion) is (0,0,0,0), which is NOT a valid rotation: use Identity.
    [StructLayout(LayoutKind.Sequential)]   // fields laid out exactly x, y, z, w in memory
    public struct Quaternion : IEquatable<Quaternion>
    {
        public float X, Y, Z, W;

        public Quaternion(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }

        public static readonly Quaternion Identity = new Quaternion(0, 0, 0, 1);

        private const float Deg2Rad = (float)(System.Math.PI / 180.0);
        private const float Rad2Deg = (float)(180.0 / System.Math.PI);
        private const float Epsilon = 1e-6f;

        // ------------------------------------------------------------------
        // Construction
        // ------------------------------------------------------------------

        // Rotation of `degrees` around `axis` (the axis does not need to be normalized).
        public static Quaternion AngleAxis(float degrees, Vector3 axis)
        {
            var n = axis.Normalized;
            if (n.X == 0 && n.Y == 0 && n.Z == 0) return Identity;
            float half = degrees * Deg2Rad * 0.5f;
            float s = (float)System.Math.Sin(half);
            return new Quaternion(n.X * s, n.Y * s, n.Z * s, (float)System.Math.Cos(half));
        }

        // Rotation from Euler angles in degrees, identical to native glm::quat(glm::radians(euler)).
        public static Quaternion Euler(float x, float y, float z)
        {
            double hx = x * Deg2Rad * 0.5, hy = y * Deg2Rad * 0.5, hz = z * Deg2Rad * 0.5;
            double cx = System.Math.Cos(hx), sx = System.Math.Sin(hx);
            double cy = System.Math.Cos(hy), sy = System.Math.Sin(hy);
            double cz = System.Math.Cos(hz), sz = System.Math.Sin(hz);
            return new Quaternion(
                (float)(sx * cy * cz - cx * sy * sz),
                (float)(cx * sy * cz + sx * cy * sz),
                (float)(cx * cy * sz - sx * sy * cz),
                (float)(cx * cy * cz + sx * sy * sz));
        }

        public static Quaternion Euler(Vector3 degrees) => Euler(degrees.X, degrees.Y, degrees.Z);

        // Euler angles in degrees, identical to native glm::degrees(glm::eulerAngles(q)).
        public Vector3 EulerAngles
        {
            get
            {
                double x = X, y = Y, z = Z, w = W;
                // Pitch (around X)
                double py = 2.0 * (y * z + w * x);
                double px = w * w - x * x - y * y + z * z;
                double pitch = (System.Math.Abs(px) < Epsilon && System.Math.Abs(py) < Epsilon)
                    ? 2.0 * System.Math.Atan2(x, w)
                    : System.Math.Atan2(py, px);
                // Yaw (around Y)
                double yaw = System.Math.Asin(Clamp(-2.0 * (x * z - w * y), -1.0, 1.0));
                // Roll (around Z)
                double roll = System.Math.Atan2(2.0 * (x * y + w * z), w * w + x * x - y * y - z * z);
                return new Vector3((float)(pitch * Rad2Deg), (float)(yaw * Rad2Deg), (float)(roll * Rad2Deg));
            }
        }

        // Rotation whose forward (-Z) axis points along `forward`, keeping `up` as close
        // to +Y as possible. Same as glm::quatLookAtRH(normalize(forward), up).
        public static Quaternion LookRotation(Vector3 forward, Vector3 up)
        {
            var back = (forward * -1f).Normalized;              // local +Z
            if (back.X == 0 && back.Y == 0 && back.Z == 0) return Identity;
            var right = Vector3.Cross(up, back).Normalized;     // local +X
            if (right.X == 0 && right.Y == 0 && right.Z == 0)
            {
                // forward is parallel to up: pick any perpendicular right axis
                right = Vector3.Cross(System.Math.Abs(back.Y) < 0.99f ? Vector3.Up : new Vector3(1, 0, 0), back).Normalized;
            }
            var newUp = Vector3.Cross(back, right);             // local +Y
            return FromBasis(right, newUp, back);
        }

        public static Quaternion LookRotation(Vector3 forward) => LookRotation(forward, Vector3.Up);

        // Shortest rotation that turns direction `from` into direction `to`.
        public static Quaternion FromToRotation(Vector3 from, Vector3 to)
        {
            var a = from.Normalized;
            var b = to.Normalized;
            if ((a.X == 0 && a.Y == 0 && a.Z == 0) || (b.X == 0 && b.Y == 0 && b.Z == 0)) return Identity;
            float d = Vector3.Dot(a, b);
            if (d >= 1f - Epsilon) return Identity;
            if (d <= -1f + Epsilon)
            {
                // Opposite directions: rotate 180 degrees around any perpendicular axis.
                var axis = Vector3.Cross(new Vector3(1, 0, 0), a);
                if (axis.Length < Epsilon) axis = Vector3.Cross(Vector3.Up, a);
                return AngleAxis(180f, axis);
            }
            var c = Vector3.Cross(a, b);
            return new Quaternion(c.X, c.Y, c.Z, 1f + d).Normalized;
        }

        // Builds a rotation from three orthonormal axes (the columns of a rotation matrix).
        private static Quaternion FromBasis(Vector3 right, Vector3 up, Vector3 back)
        {
            float m00 = right.X, m10 = right.Y, m20 = right.Z;
            float m01 = up.X,    m11 = up.Y,    m21 = up.Z;
            float m02 = back.X,  m12 = back.Y,  m22 = back.Z;
            float trace = m00 + m11 + m22;
            Quaternion q;
            if (trace > 0f)
            {
                float s = (float)System.Math.Sqrt(trace + 1f) * 2f;
                q = new Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
            }
            else if (m00 > m11 && m00 > m22)
            {
                float s = (float)System.Math.Sqrt(1f + m00 - m11 - m22) * 2f;
                q = new Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
            }
            else if (m11 > m22)
            {
                float s = (float)System.Math.Sqrt(1f + m11 - m00 - m22) * 2f;
                q = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
            }
            else
            {
                float s = (float)System.Math.Sqrt(1f + m22 - m00 - m11) * 2f;
                q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
            }
            return q.Normalized;
        }

        // ------------------------------------------------------------------
        // Properties
        // ------------------------------------------------------------------

        public float LengthSquared => X * X + Y * Y + Z * Z + W * W;
        public float Length => (float)System.Math.Sqrt(LengthSquared);

        // Unit-length copy. Returns Identity for a zero quaternion.
        public Quaternion Normalized
        {
            get
            {
                float l = Length;
                if (l < Epsilon) return Identity;
                float inv = 1f / l;
                return new Quaternion(X * inv, Y * inv, Z * inv, W * inv);
            }
        }

        // Reverse rotation. For unit quaternions this is the conjugate.
        public Quaternion Inverse
        {
            get
            {
                float lsq = LengthSquared;
                if (lsq < Epsilon) return Identity;
                float inv = 1f / lsq;
                return new Quaternion(-X * inv, -Y * inv, -Z * inv, W * inv);
            }
        }

        // Local axes of this rotation in world/parent space.
        public Vector3 Forward => this * new Vector3(0, 0, -1);
        public Vector3 Back    => this * new Vector3(0, 0, 1);
        public Vector3 Right   => this * new Vector3(1, 0, 0);
        public Vector3 Up      => this * new Vector3(0, 1, 0);

        // Splits the rotation into an angle (degrees, 0..180) and a unit axis.
        public void ToAngleAxis(out float degrees, out Vector3 axis)
        {
            var q = Normalized;
            if (q.W < 0) q = new Quaternion(-q.X, -q.Y, -q.Z, -q.W);  // shortest form
            degrees = 2f * (float)System.Math.Acos(Clamp(q.W, -1.0, 1.0)) * Rad2Deg;
            float s = (float)System.Math.Sqrt(System.Math.Max(0.0, 1.0 - q.W * q.W));
            axis = s < Epsilon ? new Vector3(1, 0, 0) : new Vector3(q.X / s, q.Y / s, q.Z / s);
        }

        // ------------------------------------------------------------------
        // Operations
        // ------------------------------------------------------------------

        public static float Dot(Quaternion a, Quaternion b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;

        // Angle in degrees between two rotations.
        public static float Angle(Quaternion a, Quaternion b)
        {
            float d = System.Math.Abs(Dot(a.Normalized, b.Normalized));
            return d >= 1f - Epsilon ? 0f : 2f * (float)System.Math.Acos(d) * Rad2Deg;
        }

        // Spherical interpolation along the shortest path. t is clamped to 0..1.
        public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
        {
            t = t < 0f ? 0f : (t > 1f ? 1f : t);
            float cos = Dot(a, b);
            if (cos < 0f) { b = new Quaternion(-b.X, -b.Y, -b.Z, -b.W); cos = -cos; }
            if (cos > 1f - 1e-4f) return Lerp(a, b, t);   // nearly equal: avoid dividing by ~0
            double angle = System.Math.Acos(cos);
            double sin = System.Math.Sin(angle);
            float wa = (float)(System.Math.Sin((1.0 - t) * angle) / sin);
            float wb = (float)(System.Math.Sin(t * angle) / sin);
            return new Quaternion(a.X * wa + b.X * wb, a.Y * wa + b.Y * wb, a.Z * wa + b.Z * wb, a.W * wa + b.W * wb);
        }

        // Cheaper normalized linear interpolation along the shortest path. t is clamped to 0..1.
        public static Quaternion Lerp(Quaternion a, Quaternion b, float t)
        {
            t = t < 0f ? 0f : (t > 1f ? 1f : t);
            if (Dot(a, b) < 0f) b = new Quaternion(-b.X, -b.Y, -b.Z, -b.W);
            return new Quaternion(
                a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t,
                a.Z + (b.Z - a.Z) * t, a.W + (b.W - a.W) * t).Normalized;
        }

        // Rotates `from` towards `to` by at most `maxDegrees`.
        public static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxDegrees)
        {
            float angle = Angle(from, to);
            if (angle < Epsilon) return to;
            return Slerp(from, to, System.Math.Min(1f, maxDegrees / angle));
        }

        // Combine rotations: (a * b) applies b first, then a.
        public static Quaternion operator *(Quaternion a, Quaternion b)
        {
            return new Quaternion(
                a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
                a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
                a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
                a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);
        }

        // Rotate a vector.
        public static Vector3 operator *(Quaternion q, Vector3 v)
        {
            var u = new Vector3(q.X, q.Y, q.Z);
            var t = Vector3.Cross(u, v) * 2f;
            return v + t * q.W + Vector3.Cross(u, t);
        }

        // ------------------------------------------------------------------
        // Equality
        // ------------------------------------------------------------------

        // Exact component equality. Use Approximately for rotations computed with floats.
        public static bool operator ==(Quaternion a, Quaternion b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
        public static bool operator !=(Quaternion a, Quaternion b) => !(a == b);
        public bool Equals(Quaternion other) => this == other;
        public override bool Equals(object obj) => obj is Quaternion other && this == other;
        public override int GetHashCode()
        {
            unchecked
            {
                int h = X.GetHashCode();
                h = h * 397 ^ Y.GetHashCode();
                h = h * 397 ^ Z.GetHashCode();
                return h * 397 ^ W.GetHashCode();
            }
        }

        // True when a and b represent the same rotation within `degrees` (q and -q count as equal).
        public static bool Approximately(Quaternion a, Quaternion b, float degrees = 0.01f) => Angle(a, b) <= degrees;

        public override string ToString() => $"({X}, {Y}, {Z}, {W})";

        private static double Clamp(double v, double lo, double hi) => v < lo ? lo : (v > hi ? hi : v);
    }
}
