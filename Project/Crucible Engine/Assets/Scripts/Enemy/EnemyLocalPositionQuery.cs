using System;
using Crucible;

namespace Crucible.Scripts
{
    // Port of Gam3333's EnemyLocalPositionQuery: "could this golem go and stand there?"
    // Ranged positioning (and later the horde coordinator) ask it about each spot they
    // consider, and only ever send the golem to spots it says yes to.
    //
    // TryGoal(candidate) checks, cheapest first, as Unity's does:
    //   1. the nav grid: the cell is open and not a terrain wall (Navigation.IsWalkable);
    //   2. the ground: a ray down from 3 m above the golem's feet finds a floor there,
    //      flat enough (normal.y >= 0.7, about 45 degrees) and within 1.5 m of the
    //      height its feet are at now. The answer is that ground point, BaseOffset up
    //      (Unity's PivotHeight) - so the spot sits ON the ground, up a ramp or on a step.
    //      This is what lets a crossbow climb: each new spot may be up to 1.5 m higher
    //      than where it stands, and the next plan starts from there;
    //   3. the way there (Unity's ClearSegment): the grid has no blocked cell or wall on
    //      the straight line, a body-sized ball swept along it hits no wall or prop, and
    //      nothing solid already fills the spot.
    //
    // Every physics query uses MovementQueryMask (arena and props only), as Unity's did,
    // so other golems and the player are looked straight past: the crowd steps round them.
    //
    // Still not 1:1:
    //   - Unity's ray down collected every hit and took the nearest VALID one; ours takes
    //     the nearest one and judges it. Different only where a steep or too-high surface
    //     sits above good ground at the same spot (an overhang), which the grid's walls
    //     mostly rule out anyway.
    //   - The ball's height: Unity swept it 1 m above the pivot, tuned for its 2 m golem.
    //     Ours rides just above the step band (feet + 0.3 m + radius), the same as the
    //     FlowFieldAgent's own wall probe, so golems of any size skip the floor but still
    //     see knee-high crates.
    //   - A sweep that starts already touching a wall: Unity ignores that wall, Crucible's
    //     SphereCast reports it at distance 0. Treated as Unity does (not blocking), so a
    //     golem the crowd has pressed against a wall can still plan its way out.
    public sealed class EnemyLocalPositionQuery
    {
        const float RayStartAbove = 3f;     // metres above the feet the ground ray starts (Unity's)
        const float RayLength = 6f;         // so it finds ground from 3 m above to 3 m below the feet
        const float MaxStepHeight = 1.5f;   // ground further above or below the feet than this isn't "here"
        const float MinGroundNormalY = 0.7f;
        const float StepHeight = 0.3f;      // the ball rides above this (FlowFieldSurface's default step height)

        readonly Enemy owner;

        public EnemyLocalPositionQuery(Enemy owner) { this.owner = owner; }

        // True if the golem can go and stand at "candidate"; "point" is then where its
        // position will be once there (on the ground, BaseOffset up).
        public bool TryGoal(Vector3 candidate, out Vector3 point)
        {
            point = Vector3.Zero;
            if (!Navigation.IsWalkable(candidate)) return false;

            float baseOffset = owner.BaseOffset;
            float feet = owner.Position.Y - baseOffset;

            RaycastHit hit;
            Vector3 from = new Vector3(candidate.X, feet + RayStartAbove, candidate.Z);
            if (!Physics.Raycast(from, Vector3.Up * -1f, out hit, RayLength, Enemy.MovementQueryMask, owner.Entity)) return false;
            if (hit.Normal.Y < MinGroundNormalY) return false;              // too steep to stand on
            if (Math.Abs(hit.Point.Y - feet) > MaxStepHeight) return false; // a cliff top or a pit, not a step

            point = hit.Point + Vector3.Up * baseOffset;
            return ClearSegment(owner.Position, point);
        }

        // Unity's ClearSegment: the grid's line test, the body-sized sweep, then the spot itself.
        bool ClearSegment(Vector3 from, Vector3 to)
        {
            if (!Navigation.IsPathClear(from, to)) return false;

            float radius = owner.BodyRadius;
            Vector3 lift = Vector3.Up * (StepHeight + radius - owner.BaseOffset);   // position -> ball centre
            Vector3 delta = to - from;
            float length = delta.Length;

            RaycastHit hit;
            if (length > 0.001f
                && Physics.SphereCast(from + lift, radius, delta * (1f / length), out hit, length, Enemy.MovementQueryMask, owner.Entity)
                && hit.Distance > 0f)
                return false;

            return !Physics.CheckSphere(to + lift, radius, Enemy.MovementQueryMask, owner.Entity);
        }
    }
}
