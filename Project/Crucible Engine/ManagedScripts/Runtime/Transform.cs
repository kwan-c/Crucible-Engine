using System.Runtime.CompilerServices;

namespace Crucible
{
    public sealed class Transform
    {
        private readonly EntityIdentity entity;
        internal Transform(EntityIdentity owner) { entity = owner; }

        public Vector3 Position
        {
            get { Vector3 v;    return GetPosition((int)entity.Index, (int)entity.Generation, out v) != 0 ? v : Vector3.Zero; }
            set { SetPosition((int)entity.Index, (int)entity.Generation, ref value); }
        }

        public Vector3 Scale
        {
            get { Vector3 v;    return GetScale((int)entity.Index, (int)entity.Generation, out v) != 0 ? v : Vector3.One; }
            set { SetScale((int)entity.Index, (int)entity.Generation, ref value); }
        }

        public Quaternion Rotation
        {
            get { Quaternion v; return GetRotation((int)entity.Index, (int)entity.Generation, out v) != 0 ? v : Quaternion.Identity; }
            set { SetRotation((int)entity.Index, (int)entity.Generation, ref value); }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetPosition(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetPosition(int index, int generation, ref Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetScale(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetScale(int index, int generation, ref Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetRotation(int index, int generation, out Quaternion value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetRotation(int index, int generation, ref Quaternion value);
    }
}