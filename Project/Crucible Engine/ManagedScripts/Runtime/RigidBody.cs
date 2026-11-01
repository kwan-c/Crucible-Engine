using System.Dynamic;
using System.Numerics;
using System.Runtime.CompilerServices;

namespace Crucible
{
    public sealed class RigidBody
    {
        private readonly EntityIdentity entity;
        internal RigidBody(EntityIdentity owner) { entity = owner; }

        public Vector3 Velocity
        {
            get { Vector3 v;    return GetVelocity((int)entity.Index, (int)entity.Generation, out v) != 0 ? v : Vector3.Zero; }
            set { SetVelocity((int)entity.Index, (int)entity.Generation, ref value); }
        }

        public float Mass
        {
            get { float mass; return GetMass((int)entity.Index, (int)entity.Generation, out mass) != 0 ? mass : 0f; }
            set { SetMass((int)entity.Index, (int)entity.Generation, ref value); }
        }

        public bool UseGravity
        {
            get { int useGravity; return GetActiveGravity((int)entity.Index, (int)entity.Generation, out useGravity) != 0 && useGravity != 0; }
            set { int flag = value ? 1 : 0; SetActiveGravity((int)entity.Index, (int)entity.Generation, ref flag); }
        }

        public float Drag
        {
            get { float drag; return GetDrag((int)entity.Index, (int)entity.Generation, out drag) != 0 ? drag : 0f; }
            set { SetDrag((int)entity.Index, (int)entity.Generation, ref value); }
        }

        public float AngularDrag
        {
            get { float angularDrag; return GetAngularDrag((int)entity.Index, (int)entity.Generation, out angularDrag) != 0 ? angularDrag : 0f; }
            set { SetAngularDrag((int)entity.Index, (int)entity.Generation, ref value); }
        }

        public void AddForce(Vector3 v)
        {
            AddRBForce((int)entity.Index, (int)entity.Generation, ref v);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetVelocity(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetVelocity(int index, int generation, ref Vector3 value);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMass(int index, int generation, out float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetMass(int index, int generation, ref float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetActiveGravity(int index, int generation, out int value);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetActiveGravity(int index, int generation, ref int value);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetDrag(int index, int generation, out float value);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetDrag(int index, int generation, ref float value);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetAngularDrag(int index, int generation, out float value);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetAngularDrag(int index, int generation, ref float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int AddRBForce(int index, int generation, ref Vector3 value);
    }
}