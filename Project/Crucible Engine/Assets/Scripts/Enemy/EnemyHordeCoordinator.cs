using System;
using Crucible;

namespace Crucible.Scripts
{
    // Port of Gam3333's EnemyHordeCoordinator (with EnemyHordePressure and the pressure part of
    // EnemyCoordinationMember): it shares where melee golems should stand round their target,
    // never what they do - the graph still decides when to move and when to swing.
    //
    // Opt-in per level, as in Unity: put this script on one entity in the scene. With none,
    // melee golems chase and Arrive as before. Unity only had one in its behaviour playtest
    // scene; its mission scenes ran without, like ours do now.
    //
    // Continuous Pressure (Unity's default style, the only one so far): every melee golem
    // within localRadius of its target heads for the point on the attack ring along its OWN
    // bearing from the target - plus a small fixed sideways bias, different per golem - and
    // walks there at full speed, slowing only in the last 1.6 m. Nobody hands out places:
    // the surround falls out of every golem aiming at its own side of the player, and the
    // bodies that meet on the ring sliding round each other. The point is re-planned every
    // planningInterval, or at once when the target has moved a metre.
    //
    // Further out, or where the point is blocked (a wall, a ledge, no ground), it returns
    // false and the golem chases on the flow field as usual.
    //
    // Still to come (step 4): Unity's other style, Reserved Positions - 4 attack and 8
    // waiting slots round the target, with leases - and its melee attack pool.
    public sealed class EnemyHordeCoordinator : EntityScript
    {
        public float localRadius = 10f;          // metres from the target where golems start taking a ring point
        public float planningInterval = 0.2f;    // seconds a ring point is kept before it is worked out again

        // Unity's position goal: full speed until this close, then slow to a stop.
        public const float SlowingDistance = 1.6f;
        public const float StoppingDistance = 0.15f;

        // The one in the scene (Unity found it by FindAnyObjectByType). Null = no coordination.
        public static EnemyHordeCoordinator Active { get; private set; }

        public override void OnCreate()
        {
            if (Active != null && Active != this)
                Log.Warning("[EnemyHordeCoordinator] more than one in the scene - using " + Entity.Name);
            Active = this;
        }

        // A static outlives Play: without this, the next session would start with a dead one.
        public override void OnDestroy()
        {
            if (Active == this) Active = null;
        }

        // Unity's Eligible: a melee golem that is up, has noticed its target, and is within
        // localRadius of it; and the target is still alive.
        bool Eligible(Enemy member)
        {
            if (member == null || !member.IsReady || member.UsesRangedPositioning || !member.melee) return false;
            if (!member.TargetDetected || ReferenceEquals(member.Target, null) || member.DistanceToTarget > localRadius) return false;
            CombatHealth health = CombatHealth.Find(member.Target);
            return health != null && !health.IsDead && health.Entity.IsActive;
        }

        // Unity's TryPressurePosition: where "member" should go to keep up the pressure.
        public bool TryPressurePosition(Enemy member, out Vector3 position)
        {
            position = Vector3.Zero;
            if (!Eligible(member)) { member.ClearPressure(); return false; }

            // Keep the last plan for planningInterval while the target hasn't moved a metre.
            Vector3 target = member.Target.Transform.WorldPosition;
            if (member.HasPressureGoal && member.PressureTarget == member.Target && Time.ElapsedTime < member.PressurePlanAt
                && Flat(target - member.PressurePlannedTarget).Length < 1f)
            {
                position = member.PressureGoal;
                return true;
            }

            // Its own bearing from the target (straight in from where it is), and across it.
            Vector3 radial = Flat(member.Position - target);
            radial = radial.Length > 1e-3f ? radial.Normalized : member.Facing * -1f;
            Vector3 tangent = Vector3.Cross(Vector3.Up, radial);

            // On the ring at attack range - but never so close the bodies overlap - nudged
            // sideways by this golem's own bias, so golems on the same bearing don't all
            // aim at one point.
            float ring = Math.Max(member.BodyRadius + 0.4f, member.attackRange - 0.12f);
            Vector3 candidate = target + radial * ring + tangent * member.ApproachBias;

            Vector3 goal;
            if (!member.TryGoal(candidate, out goal)) { member.ClearPressure(); return false; }   // flow field instead

            member.SetPressureGoal(goal, member.Target, target, Time.ElapsedTime + planningInterval);
            position = goal;
            return true;
        }

        static Vector3 Flat(Vector3 v) { return new Vector3(v.X, 0f, v.Z); }
    }
}
