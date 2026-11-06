using System;

namespace Crucible.Scripts
{
    // Tests the physics callbacks.
    //   Collisions: put this on a falling crate (Collider + RigidBody) above a floor with a Collider.
    //   Triggers:   put it on an entity whose Collider has Is Trigger ticked, then move something
    //               with a Collider (a crate, the mech) through it - or put it on the thing moving.
    //   Every OnCollisionEnter / Exit and OnTriggerEnter / Exit is logged. Hits faster than
    //   "Hard Hit Speed" are called out, and "Mass" is set at start (tests the RigidBody setter).
    public sealed class CollisionTest : EntityScript
    {
        public float hardHitSpeed = 3f;   // m/s
        public float mass = 2f;           // 0 or less is ignored (rejected by the setter)

        int touching;
        int inside;

        public override void OnCreate()
        {
            if (HasComponent<RigidBody>())
            {
                RigidBody.Mass = mass;
                Log.Info("[CollisionTest] " + Entity + " mass set to " + RigidBody.Mass);
            }
        }

        public override void OnCollisionEnter(Collision hit)
        {
            ++touching;
            Log.Info("[CollisionTest] Enter: " + hit + ", normal " + hit.Normal + (hit.ImpactSpeed >= hardHitSpeed ? "  <- HARD HIT" : "") +
                     " (touching " + touching + ")");
        }

        public override void OnCollisionExit(Collision hit)
        {
            touching = Math.Max(0, touching - 1);
            Log.Info("[CollisionTest] Exit: " + hit.Other + (hit.Other.IsAlive ? "" : " (destroyed)") + " (touching " + touching + ")");
        }

        public override void OnTriggerEnter(Entity other)
        {
            ++inside;
            Log.Info("[CollisionTest] Trigger enter: " + other + " (" + inside + " inside)");
        }

        public override void OnTriggerExit(Entity other)
        {
            inside = Math.Max(0, inside - 1);
            Log.Info("[CollisionTest] Trigger exit: " + other + " (" + inside + " inside)");
        }
    }
}
