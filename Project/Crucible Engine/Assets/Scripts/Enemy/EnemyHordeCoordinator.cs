using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    // Shares destinations and, in the optional reserved mode, attack turns.
    // EnemyController is the only class that sends those destinations to the agent.
    public sealed class EnemyHordeCoordinator : EntityScript
    {
        public float localRadius = 10f;
        public float planningInterval = .2f;
        public float pressureSpacing = 1.8f;
        public bool reservedPositions;
        public int attackPositions = 4;
        public int waitingPositions = 8;
        public float waitingRadius = 5.2f;
        public int maximumMeleeAttacks = 2;
        public float meleeStartSpacing = .18f;
        public const float SlowingDistance = 1.6f;
        public const float StoppingDistance = .15f;
        static readonly float[] angles = { 0f, 28f, -28f, 55f, -55f, 85f, -85f, 120f, -120f, 180f };
        readonly List<Vector3> neighbours = new List<Vector3>();
        readonly EnemyAttackReservations pool = new EnemyAttackReservations(e => e.useHordeCoordination && e.melee);
        static EnemyHordeCoordinator instance;
        public static EnemyHordeCoordinator Active { get { return instance != null && instance.Entity.IsActive
            && instance.Enabled && instance.Entity.IsEnabled<EntityScript>() ? instance : null; } }

        public override void OnCreate() { if (instance == null || !instance.Entity.IsAlive) instance = this; }
        public override void OnDestroy() { if (instance == this) instance = null; pool.Clear(); }
        public override void OnUpdate(float dt)
        {
            if (reservedPositions) pool.Prune(Time.ElapsedTime);
            else pool.Clear();
        }
        internal bool TryAdmit(Enemy e)
        {
            return !reservedPositions || e.DistanceToTarget > localRadius
                || pool.TryAcquire(e, maximumMeleeAttacks, meleeStartSpacing, float.MaxValue);
        }
        internal void Release(Enemy e) { pool.Release(e); }
        internal bool NeedsWaitingPosition(Enemy e)
        {
            return reservedPositions && !e.previewSpinnerWindup && e.DistanceToTarget <= localRadius
                && pool.AtCapacity(e, maximumMeleeAttacks);
        }

        public bool TryPressurePosition(Enemy member, out Vector3 position)
        {
            position = Vector3.Zero;
            if (member == null) return false;
            if (!member.useHordeCoordination || !member.melee || !member.TargetDetected || !member.IsReady
                || member.Target == null || member.DistanceToTarget > localRadius || member.AttackInProgress)
            { member.ClearPressure(); return false; }
            Vector3 target = member.Target.Transform.WorldPosition;
            bool waiting = NeedsWaitingPosition(member);
            if (member.PressureTarget == member.Target && member.PressureWaiting == waiting
                && Time.ElapsedTime < member.PressurePlanAt && Flat(target - member.PressurePlannedTarget).Length < 1f)
            { position = member.PressureGoal; return member.HasPressureGoal; }

            neighbours.Clear();
            foreach (Enemy other in Enemy.All)
            {
                if (other == member || !other.melee || !other.useHordeCoordination || other.Target != member.Target
                    || !other.IsReady || other.DistanceToTarget > localRadius) continue;
                neighbours.Add(other.HasPressureGoal && other.PressureTarget == member.Target
                    && !other.AttackInProgress ? other.PressureGoal : other.Position);
            }
            float radius = Math.Max(member.BodyRadius + .4f, member.attackRange - .12f);
            Vector3 radial = Flat(member.Position - target);
            radial = radial.Length > .01f ? radial.Normalized : member.Facing * -1f;
            bool slots = reservedPositions && !member.previewSpinnerWindup;
            bool found = false;
            if (!waiting) found = FindSpot(member, target, radial, radius, slots ? attackPositions : 0, out position);
            if (!found && slots) found = FindSpot(member, target, radial, Math.Max(radius + 1f, waitingRadius), waitingPositions, out position);
            member.SetPressureGoal(position, member.Target, target, Time.ElapsedTime + Math.Max(.1f, planningInterval), found, waiting);
            return found;
        }

        bool FindSpot(Enemy member, Vector3 target, Vector3 radial, float radius, int slots, out Vector3 position)
        {
            position = Vector3.Zero;
            int count = slots == 0 ? angles.Length : Math.Max(2, Math.Min(24, slots));
            float step = 360f / count;
            float nearest = (float)Math.Round(Math.Atan2(radial.X, radial.Z) * 180 / Math.PI / step) * step;
            float sign = member.ApproachBias < 0f ? -1f : 1f;
            for (int i = 0; i < count; i++)
            {
                float angle = slots == 0 ? angles[i] * sign + member.ApproachBias * 12f
                    : nearest + ((i + 1) / 2) * (i % 2 == 0 ? -step : step);
                Vector3 dir = Quaternion.AngleAxis(angle, Vector3.Up) * (slots == 0 ? radial : new Vector3(0, 0, 1));
                Vector3 candidate = target + dir * radius;
                bool occupied = false;
                foreach (Vector3 other in neighbours)
                    if (Flat(other - candidate).Length < pressureSpacing) { occupied = true; break; }
                if (!occupied && member.TryGoal(candidate, out position)
                    && (!member.previewSpinnerWindup || member.HasChargeLaneFrom(position))) return true;
            }
            return false;
        }
        static Vector3 Flat(Vector3 v) { return new Vector3(v.X, 0f, v.Z); }
    }
}
