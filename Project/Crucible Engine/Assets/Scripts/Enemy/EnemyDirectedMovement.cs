using System;

namespace Crucible.Scripts
{
    // The graph owns ordinary intents. From HoldForSpecial until ReleaseSpecialMovement,
    // a dynamic rigid body owns the motion. Physics supplies each measured segment.
    public sealed partial class Enemy
    {
        bool movementHeld;
        Vector3 directedPrevious, directedStart, directedHeading;
        float directedSpeed, directedRemaining, directedDistance, stalledSeconds;

        bool CanHoldForSpecial()
        {
            return !movementHeld && pushLeft <= 0f && !IsFrozen && agent != null
                && Entity.IsEnabled<FlowFieldAgent>() && HasComponent<RigidBody>() && !RigidBody.IsKinematic;
        }

        void HoldForSpecial()
        {
            Move(Intent.Hold);
            movementHeld = true;
            UpdateLegs();
            SetPlanarSpeed(Vector3.Zero);
        }

        void SetPlanarSpeed(Vector3 velocity)
        {
            RigidBody.Velocity = new Vector3(velocity.X, RigidBody.Velocity.Y, velocity.Z);
        }

        // Both the visible warning and the motor use this same fixed heading.
        void StartDirectedMovement(Vector3 heading, float speed, float seconds)
        {
            directedStart = directedPrevious = Position;
            directedHeading = heading;
            directedSpeed = speed;
            directedRemaining = seconds;
            directedDistance = speed * seconds;
            stalledSeconds = 0f;
        }

        bool LaneSupported(Vector3 from, Vector3 heading, float distance)
        {
            float feet = from.Y - physicalBaseOffset;
            float radius = Math.Max(.1f, BodyRadius);
            Vector3 centre = new Vector3(from.X, feet + radius + .25f, from.Z);
            RaycastHit hit;
            if (Physics.SphereCast(centre, radius, heading, out hit, distance, MovementQueryMask, Entity)
                && hit.Normal.Y < .7f) return false;
            bool baked = Navigation.IsBaked;
            if (baked && !Navigation.IsPathClear(from, from + heading * distance)) return false;
            int samples = Math.Max(1, (int)Math.Ceiling(distance / .65f));
            for (int i = 1; i <= samples; i++)
            {
                Vector3 at = from + heading * (distance * i / samples);
                if ((baked && !Navigation.IsWalkable(at)) || !Physics.Raycast(new Vector3(at.X, feet + .65f, at.Z),
                    Vector3.Up * -1f, out hit, 1.3f, MovementQueryMask, Entity) || hit.Normal.Y < .7f)
                    return false;
                feet = hit.Point.Y;
            }
            return true;
        }

        // Called after reading the last physics segment. Check support ahead of the leading edge
        // before setting velocity. The body still collides normally throughout the physics step.
        bool AdvanceDirectedMovement(float dt, out Vector3 from, out Vector3 to)
        {
            from = directedPrevious;
            to = directedPrevious = Position;
            float moved = Vector3.Dot(to - from, directedHeading);
            float left = directedDistance - Vector3.Dot(to - directedStart, directedHeading);
            if (directedRemaining <= 0f || left <= .02f) return false;
            stalledSeconds = moved > .005f ? 0f : stalledSeconds + dt;
            float step = Math.Min(left, directedSpeed * Math.Min(dt, directedRemaining));
            // Physics can catch up by six 60 Hz steps between script updates.
            float ahead = Math.Min(left, directedSpeed * Math.Max(dt, .1f)) + Math.Min(.6f, BodyRadius);
            if (stalledSeconds > .22f || !LaneSupported(to, directedHeading, ahead)) return false;
            SetPlanarSpeed(directedHeading * Math.Min(directedSpeed, step / Math.Max(dt, 1e-5f)));
            directedRemaining -= dt;
            return true;
        }

        void ReleaseSpecialMovement()
        {
            if (!movementHeld) return;
            movementHeld = false;
            if (Entity.IsAlive && HasComponent<RigidBody>() && !RigidBody.IsKinematic && pushLeft <= 0f)
                SetPlanarSpeed(Vector3.Zero);
            if (Entity.IsAlive) UpdateLegs();
        }
    }
}
