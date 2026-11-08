/*************************************************************************
* \file:         FlowFieldAgent.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.1
* \brief         Script access to an enemy's FlowFieldAgent: how it moves,
*                what it's told to do, and how far it has got. Unity's
*                NavMeshAgent. Part of Crucible.ScriptCore.dll.
*
* \details
*    An agent does nothing until it's told to (like a NavMeshAgent before
*    SetDestination). Tell it from a script, or from a state graph's nodes -
*    both make the same calls:
*
*        var agent = GetComponent<FlowFieldAgent>();
*        agent.Arrive(player);                     // chase, stop at StoppingDistance
*        if (agent.HasArrived) Attack();           // in range
*        agent.MoveTo(new Vector3(10, 0, 5));      // walk to a point
*        agent.FollowField();                      // the shared field: head for the player round walls
*        agent.Stop();
*
*    The engine's ECSNavigationSystem does the moving (steering, the crowd,
*    walls), so these calls are cheap: they only set what the agent wants.
*
*    C++ side: Engine/src/Scripting/Bindings/NavigationBindings.cpp.
*
* \history
*    -v1.0       first version
*    -v1.1       AvoidancePriority replaces SeparationRadius and
*                SeparationWeight (the crowd now works like Unity's);
*                FaceTarget(); MoveTo() takes optional stopping and slowing
*                distances
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>Enemy movement on the flow field (Unity's NavMeshAgent): MoveTo, Arrive, FollowField, Stop.</summary>
    public sealed class FlowFieldAgent
    {
        private readonly EntityIdentity entity;
        internal FlowFieldAgent(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this agent is on.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        // --- orders ------------------------------------------------------------- //

        /// <summary>Follow the shared flow field: head for the nearest player, round the walls.</summary>
        public void FollowField() { FollowFieldInternal(I, G); }

        /// <summary>
        /// Walk to a point (Unity's SetDestination) and stop StoppingDistance from it - or
        /// stoppingDistance from it, for this order only, when given (e.g. 0.3 for an exact
        /// firing spot, without changing the attack range that Arrive stops at).
        /// slowingDistance likewise replaces SlowingDistance for this order (e.g. 1.6 to walk
        /// at full speed until nearly there). Negative = use the setting.
        /// </summary>
        public void MoveTo(Vector3 point, float stoppingDistance = -1f, float slowingDistance = -1f)
        {
            MoveToInternal(I, G, ref point, stoppingDistance, slowingDistance);
        }

        /// <summary>
        /// Chase an entity, slowing from SlowingDistance and stopping at StoppingDistance -
        /// so it ends in attack range instead of pushing into it. Null stands it still.
        /// </summary>
        public void Arrive(Entity target)
        {
            ArriveInternal(I, G, target != null ? (int)target.Index : 0, target != null ? (int)target.Generation : 0);
        }

        /// <summary>Stand still. HasArrived reads true while stopped.</summary>
        public void Stop() { StopInternal(I, G); }

        /// <summary>
        /// Face this entity whatever it is told to do, e.g. strafing while it walks
        /// somewhere else (Unity: updateRotation = false, then turning it yourself).
        /// Null goes back to facing the way it walks (and not turning at all while stopped).
        /// Kept until changed - Stop doesn't clear it.
        /// </summary>
        public void FaceTarget(Entity target)
        {
            FaceTargetInternal(I, G, target != null ? (int)target.Index : 0, target != null ? (int)target.Generation : 0);
        }

        // --- how it's going ----------------------------------------------------- //

        /// <summary>
        /// Within StoppingDistance of its point or target (plus 0.5 m slack, as the crowd can
        /// hold it just outside). True while stopped. False until the frame after a new order.
        /// </summary>
        public bool HasArrived
        {
            get { int v; return HasArrivedInternal(I, G, out v) != 0 && v != 0; }
        }

        /// <summary>Flat metres to its point, target or the nearest player; 0 while stopped.</summary>
        public float RemainingDistance
        {
            get { float v; return GetRemainingDistanceInternal(I, G, out v) != 0 ? v : 0f; }
        }

        /// <summary>Its current velocity in m/s, flat (Y is always 0).</summary>
        public Vector3 Velocity
        {
            get { Vector3 v; return GetVelocityInternal(I, G, out v) != 0 ? v : Vector3.Zero; }
        }

        // --- settings (the Inspector's; none may be negative) ------------------- //

        /// <summary>Top speed, m/s.</summary>
        public float Speed { get { float v; return GetSpeedInternal(I, G, out v) != 0 ? v : 0f; } set { SetSpeedInternal(I, G, ref value); } }
        /// <summary>How fast it changes velocity, m/s^2. Low = wide, drifting turns.</summary>
        public float Acceleration { get { float v; return GetAccelerationInternal(I, G, out v) != 0 ? v : 0f; } set { SetAccelerationInternal(I, G, ref value); } }
        /// <summary>How fast it turns to face where it goes, degrees per second.</summary>
        public float AngularSpeed { get { float v; return GetAngularSpeedInternal(I, G, out v) != 0 ? v : 0f; } set { SetAngularSpeedInternal(I, G, ref value); } }
        /// <summary>Metres from the entity's position down to its feet. 1 for a 2 m capsule.</summary>
        public float BaseOffset { get { float v; return GetBaseOffsetInternal(I, G, out v) != 0 ? v : 0f; } set { SetBaseOffsetInternal(I, G, ref value); } }
        /// <summary>Metres from the target where it stops (attack range).</summary>
        public float StoppingDistance { get { float v; return GetStoppingDistanceInternal(I, G, out v) != 0 ? v : 0f; } set { SetStoppingDistanceInternal(I, G, ref value); } }
        /// <summary>Metres from the target where it starts slowing down.</summary>
        public float SlowingDistance { get { float v; return GetSlowingDistanceInternal(I, G, out v) != 0 ? v : 0f; } set { SetSlowingDistanceInternal(I, G, ref value); } }
        /// <summary>Metres: which other agents it reacts to at all.</summary>
        public float NeighbourRadius { get { float v; return GetNeighbourRadiusInternal(I, G, out v) != 0 ? v : 0f; } set { SetNeighbourRadiusInternal(I, G, ref value); } }
        /// <summary>
        /// 0-99, Unity's NavMeshAgent.avoidancePriority: LOWER is more important. When two
        /// agents' bodies overlap, the less important one gives way (at equal priority, the
        /// one still on its way gives way to one that has arrived). Values outside 0-99 are
        /// clamped, as in Unity.
        /// </summary>
        public int AvoidancePriority { get { int v; return GetAvoidancePriorityInternal(I, G, out v) != 0 ? v : 50; } set { SetAvoidancePriorityInternal(I, G, value); } }
        /// <summary>How much it matches its neighbours' heading. 0 = off.</summary>
        public float AlignmentWeight { get { float v; return GetAlignmentWeightInternal(I, G, out v) != 0 ? v : 0f; } set { SetAlignmentWeightInternal(I, G, ref value); } }
        /// <summary>Body radius, metres. Match the surface's padding.</summary>
        public float Radius { get { float v; return GetRadiusInternal(I, G, out v) != 0 ? v : 0f; } set { SetRadiusInternal(I, G, ref value); } }
        /// <summary>Metres it looks ahead for walls and crates to swerve round.</summary>
        public float AvoidanceDistance { get { float v; return GetAvoidanceDistanceInternal(I, G, out v) != 0 ? v : 0f; } set { SetAvoidanceDistanceInternal(I, G, ref value); } }
        /// <summary>How hard it swerves, against following its order (1). 0 = off.</summary>
        public float AvoidanceWeight { get { float v; return GetAvoidanceWeightInternal(I, G, out v) != 0 ? v : 0f; } set { SetAvoidanceWeightInternal(I, G, ref value); } }

        public override string ToString() { return "FlowFieldAgent on " + Entity; }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int FollowFieldInternal(int index, int generation);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int MoveToInternal(int index, int generation, ref Vector3 point, float stoppingDistance, float slowingDistance);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int ArriveInternal(int index, int generation, int targetIndex, int targetGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int StopInternal(int index, int generation);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int FaceTargetInternal(int index, int generation, int targetIndex, int targetGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int HasArrivedInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetRemainingDistanceInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetVelocityInternal(int index, int generation, out Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetSpeedInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetSpeedInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetAccelerationInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetAccelerationInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetAngularSpeedInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetAngularSpeedInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBaseOffsetInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetBaseOffsetInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetStoppingDistanceInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetStoppingDistanceInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetSlowingDistanceInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetSlowingDistanceInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetNeighbourRadiusInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetNeighbourRadiusInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetAvoidancePriorityInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetAvoidancePriorityInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetAlignmentWeightInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetAlignmentWeightInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetRadiusInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetRadiusInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetAvoidanceDistanceInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetAvoidanceDistanceInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetAvoidanceWeightInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetAvoidanceWeightInternal(int index, int generation, ref float value);
    }
}
