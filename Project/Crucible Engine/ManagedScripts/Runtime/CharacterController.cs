/*************************************************************************
* \file:         CharacterController.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Script access to an entity's CharacterController component.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's CharacterController: a capsule that walks, slides along walls,
*    climbs steps and is only moved by you. Get it with
*    GetComponent<CharacterController>() or Entity.CharacterController.
*
*        var mech = GetComponent<CharacterController>();
*        mech.SimpleMove(new Vector3(x, 0, z) * speed);   // keeps walking until changed
*        if (Input.IsPressed(KeyCode.Space) && mech.IsGrounded) mech.Jump(1.5f);
*
*    SimpleMove and Jump are used by this frame's physics step (physics runs
*    after scripts). IsGrounded and Velocity are what the last step reported.
*    Setting Transform.Position teleports it (a respawn).
*
*    C++ side: Engine/src/Scripting/Bindings/PhysicsBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>A walking capsule moved by script. Unity's CharacterController.</summary>
    public sealed class CharacterController
    {
        private readonly EntityIdentity entity;
        internal CharacterController(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>
        /// Walk at this velocity in m/s. The up part is ignored (gravity and Jump own it).
        /// It keeps walking until told otherwise, so SimpleMove(Vector3.Zero) stops it.
        /// Unlike Unity, do not multiply by deltaTime.
        /// </summary>
        public void SimpleMove(Vector3 velocity) { SimpleMoveInternal(I, G, ref velocity); }

        /// <summary>Jump this many metres high, if it is on the ground at the next physics step.</summary>
        public void Jump(float height) { JumpInternal(I, G, height); }

        /// <summary>Was it standing on something after the last physics step?</summary>
        public bool IsGrounded
        {
            get { int v; return IsGroundedInternal(I, G, out v) != 0 && v != 0; }
        }

        /// <summary>How fast it really moved in the last step, after walls (m/s).</summary>
        public Vector3 Velocity
        {
            get { Vector3 v; return GetVelocityInternal(I, G, out v) != 0 ? v : Vector3.Zero; }
        }

        /// <summary>The capsule's full height in metres. Changing it rebuilds the capsule.</summary>
        public float Height
        {
            get { float v; return GetHeightInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetHeightInternal(I, G, ref value); }
        }

        /// <summary>The capsule's radius in metres. Changing it rebuilds the capsule.</summary>
        public float Radius
        {
            get { float v; return GetRadiusInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetRadiusInternal(I, G, ref value); }
        }

        /// <summary>Steepest slope it can walk up, in degrees.</summary>
        public float SlopeLimit
        {
            get { float v; return GetSlopeLimitInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetSlopeLimitInternal(I, G, ref value); }
        }

        /// <summary>Highest step it climbs without jumping, in metres.</summary>
        public float StepOffset
        {
            get { float v; return GetStepOffsetInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetStepOffsetInternal(I, G, ref value); }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SimpleMoveInternal(int index, int generation, ref Vector3 velocity);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int JumpInternal(int index, int generation, float height);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsGroundedInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetVelocityInternal(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetHeightInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetHeightInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetRadiusInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetRadiusInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetSlopeLimitInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetSlopeLimitInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetStepOffsetInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetStepOffsetInternal(int index, int generation, ref float value);
    }
}
