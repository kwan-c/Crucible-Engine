using System;
using System.Collections.Generic;
using Crucible;

namespace Crucible.Scripts
{
    // Port of Gam3333's EnemyRangedPositioning: where a crossbow should stand to shoot.
    // Suggestions only - the graph decides when to move or fire, the FlowFieldAgent moves.
    //
    // Every rangedPlanningInterval it scores spots round the player: three rings (a
    // preferred distance between minimumRange and attackRange, the far edge, the near
    // edge) at angles of 0, +-20, +-40, +-65 and +-90 degrees from where it stands now.
    // A spot counts only if it can get there and has a clear shot from it. The score
    // prefers short walks and the preferred ring, and adds a cost for being within
    // rangedSpacing of another shooter (or its planned spot), so shooters spread out
    // without reserving slots. This is also what stops a crossbow standing behind the
    // maces round the player: it walks round to an angle with a clear shot.
    //
    // High ground is not asked for anywhere - it falls out of this. Each spot is checked by
    // EnemyLocalPositionQuery, which stands it on the ground up to 1.5 m above the golem's
    // feet; on a hill the higher spots tend to be the ones with a clear shot over the maces,
    // so the crossbow climbs, one plan at a time.
    public sealed class EnemyRangedPositioning
    {
        static readonly float[] Angles = { 0f, 20f, -20f, 40f, -40f, 65f, -65f, 90f, -90f };

        readonly Enemy owner;
        Entity target;
        Vector3 plannedTarget;
        float planAt;
        float checkCrowdAt;
        bool crowded;
        readonly List<Vector3> peerPositions = new List<Vector3>();
        readonly List<Vector3> peerGoals = new List<Vector3>();

        public bool HasGoal { get; private set; }
        public Vector3 Goal { get; private set; }

        public EnemyRangedPositioning(Enemy owner) { this.owner = owner; }

        public void Clear() { HasGoal = false; target = null; planAt = 0f; }

        // Another shooter after the same target closer than "spacing"?
        public bool Crowded(float spacing, float interval)
        {
            if (Time.ElapsedTime < checkCrowdAt) return crowded;
            checkCrowdAt = Time.ElapsedTime + Math.Max(.1f, interval);
            crowded = false;
            foreach (Enemy other in Enemy.All)
                if (RangedPeer(other) && Flat(other.Position - owner.Position).Length < spacing) { crowded = true; break; }
            return crowded;
        }

        bool RangedPeer(Enemy other)
        {
            return other != null && other != owner && other.Entity.IsActive && other.Target == owner.Target
                && other.IsRanged && other.IsReady;
        }

        public bool TryGetGoal(float minimum, float interval, float spacing, out Vector3 position)
        {
            position = Vector3.Zero;
            Entity current = owner.Target;
            if (ReferenceEquals(current, null)) { Clear(); return false; }

            // Keep the last plan for "interval" seconds while the target hasn't moved a metre.
            Vector3 targetPosition = current.Transform.WorldPosition;
            if (current == target && Time.ElapsedTime < planAt && Flat(plannedTarget - targetPosition).Length < 1f)
            {
                position = Goal;
                return HasGoal;
            }
            target = current;
            plannedTarget = targetPosition;
            planAt = Time.ElapsedTime + Math.Max(.1f, interval);
            peerPositions.Clear();
            peerGoals.Clear();
            foreach (Enemy other in Enemy.All)
            {
                if (!RangedPeer(other)) continue;
                peerPositions.Add(other.Position);
                if (other.HasRangedPosition) peerGoals.Add(other.RangedPosition);
            }

            float maximum = owner.attackRange - 0.25f;
            float near = Math.Min(minimum + 0.65f, maximum);
            float preferred = near + (maximum - near) * 0.55f;

            Vector3 radial = Flat(owner.Position - plannedTarget);
            radial = radial.Length > 0.03f ? radial.Normalized : owner.Facing * -1f;

            float bestScore = float.PositiveInfinity;
            Vector3 best = Vector3.Zero;
            for (int ring = 0; ring < 3; ++ring)
            {
                float radius = ring == 0 ? preferred : ring == 1 ? maximum : near;
                foreach (float angle in Angles)
                {
                    // The candidate is only a direction and distance; TryGoal turns it into
                    // a real place to stand, on the ground there (maybe a step higher).
                    Vector3 candidate = plannedTarget + Quaternion.AngleAxis(angle, Vector3.Up) * radial * radius;
                    Vector3 spot;
                    if (!owner.TryGoal(candidate, out spot) || !owner.HasClearRangedPathFrom(spot)) continue;

                    float score = Flat(spot - owner.Position).Length + Math.Abs(radius - preferred) * 0.4f;
                    foreach (Vector3 peer in peerPositions) score += Math.Max(0f, spacing - Flat(peer - spot).Length) * 4f;
                    foreach (Vector3 goal in peerGoals) score += Math.Max(0f, spacing - Flat(goal - spot).Length) * 2f;
                    if (HasGoal && Flat(spot - Goal).Length < 0.7f) score -= 0.8f;   // stick with the plan unless clearly better
                    if (score >= bestScore) continue;
                    bestScore = score;
                    best = spot;
                }
            }

            HasGoal = bestScore < float.PositiveInfinity;
            if (HasGoal) Goal = position = best;
            return HasGoal;
        }

        static Vector3 Flat(Vector3 v) { return new Vector3(v.X, 0f, v.Z); }
    }
}
