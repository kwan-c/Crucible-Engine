using System.Dynamic;
using System.Numerics;
using System.Runtime.CompilerServices;

namespace Crucible
{
    public sealed class RigidBody
    {
        private readonly EntityIdentity entity;
        internal RigidBody(EntityIdentity owner) { entity = owner; }

        /// <summary>The entity this component is on (Unity's .gameObject), e.g. target.Entity.Name.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

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

        public bool IsKinematic
        {
            get { int isKinematic; return GetIsKinematic((int)entity.Index, (int)entity.Generation, out isKinematic) != 0 && isKinematic != 0; }
            set { int flag = value ? 1 : 0; SetIsKinematic((int)entity.Index, (int)entity.Generation, ref flag); }
        }

        // public class Constraints
        // {
        //     public bool FreezeRotationX
        //     {
                
        //     }

        //     public bool FreezePosition
        //     {
                
        //     }
        // }

        public void AddForce(Vector3 v)
        {
            AddRBForce((int)entity.Index, (int)entity.Generation, ref v);
        }
        public void AddTorque(Vector3 v)
        {
            AddRBTorque((int)entity.Index, (int)entity.Generation, ref v);
        }
        public void ApplyImpulse(Vector3 v)
        {
            ApplyRBImpulse((int)entity.Index, (int)entity.Generation, ref v);
        }

        /// <summary>
        /// Unity's freezeRotation: locks all three rotation axes, so collisions and pushes
        /// can't spin or tip the body (it still moves). A FlowFieldAgent turns a locked
        /// enemy itself. Unlocking lets it fall over - e.g. when it dies.
        /// </summary>
        public bool FreezeRotation
        {
            get { int v; return GetFreezeRotationInternal((int)entity.Index, (int)entity.Generation, out v) != 0 && v != 0; }
            set { SetFreezeRotationInternal((int)entity.Index, (int)entity.Generation, value ? 1 : 0); }
        }

        /// <summary>
        /// Unity's AddExplosionForce: pushes this body away from a blast at explosionPosition,
        /// full force at the centre fading to nothing at explosionRadius (0 = no fading).
        /// upwardsModifier lowers the blast by that many metres, so the body is thrown up.
        /// Pushes as an impulse (a one-off kick). Only dynamic bodies move. False if nothing was pushed.
        /// </summary>
        public bool AddExplosionForce(float explosionForce, Vector3 explosionPosition, float explosionRadius, float upwardsModifier = 0f)
        {
            return AddExplosionForceInternal((int)entity.Index, (int)entity.Generation, explosionForce,
                                             ref explosionPosition, explosionRadius, upwardsModifier) != 0;
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
        private static extern int GetIsKinematic(int index, int generation, out int value);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetIsKinematic(int index, int generation, ref int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int AddRBForce(int index, int generation, ref Vector3 value);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int AddRBTorque(int index, int generation, ref Vector3 value);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int ApplyRBImpulse(int index, int generation, ref Vector3 value);

        // Added by Hugo (7 Nov 2026), C++ side in PhysicsBindings.cpp: rotation locks and explosions.
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetFreezeRotationInternal(int index, int generation, out int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetFreezeRotationInternal(int index, int generation, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int AddExplosionForceInternal(int index, int generation, float force, ref Vector3 position,
                                                            float radius, float upwardsModifier);
    }
}