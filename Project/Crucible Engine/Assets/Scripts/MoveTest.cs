using System;

namespace Crucible.Scripts
{
    // Tests CharacterController, Physics.Raycast and Time.
    //   1. Attach to an entity with a CharacterController component (and no Collider),
    //      standing above a floor that has a Collider.
    //   2. Press Play, then:
    //        I J K L  walk (world axes)      Space  jump when grounded
    //        R        raycast straight down and along +Z from this entity, log the hits
    //   Every 2 seconds it logs Time.ElapsedTime and Time.FrameCount.
    public sealed class MoveTest : EntityScript
    {
        public float speed = 5f;          // m/s
        public float jumpHeight = 1.5f;   // metres
        public float rayLength = 50f;

        CharacterController mech;
        bool wasGrounded;
        float nextTimeLog = 2f;

        public override void OnCreate()
        {
            mech = GetComponent<CharacterController>();
            if (mech == null)
                Log.Warning("[MoveTest] " + Entity + " has no CharacterController - add one (and remove its Collider).");
            else
                Log.Info("[MoveTest] Height " + mech.Height + ", radius " + mech.Radius + ", slope limit " +
                         mech.SlopeLimit + ", step offset " + mech.StepOffset);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (mech != null)
            {
                float x = 0f, z = 0f;
                if (Input.IsDown(KeyCode.L)) x += 1f;
                if (Input.IsDown(KeyCode.J)) x -= 1f;
                if (Input.IsDown(KeyCode.I)) z += 1f;
                if (Input.IsDown(KeyCode.K)) z -= 1f;
                mech.SimpleMove(new Vector3(x, 0f, z).Normalized * speed);   // m/s - no deltaTime

                if (Input.IsPressed(KeyCode.Space) && mech.IsGrounded)
                    mech.Jump(jumpHeight);

                bool grounded = mech.IsGrounded;
                if (grounded != wasGrounded)
                {
                    Log.Info("[MoveTest] " + (grounded ? "Landed" : "Left the ground") + ", velocity " + mech.Velocity);
                    wasGrounded = grounded;
                }
            }

            if (Input.IsPressed(KeyCode.R))
            {
                Cast(new Vector3(0f, -1f, 0f), "down");
                Cast(new Vector3(0f, 0f, 1f), "+Z");
            }

            if (Time.ElapsedTime >= nextTimeLog)
            {
                Log.Info("[MoveTest] Time.ElapsedTime " + Time.ElapsedTime.ToString("0.00") + " s, frame " +
                         Time.FrameCount + ", DeltaTime " + Time.DeltaTime.ToString("0.0000") +
                         " (OnUpdate got " + deltaTime.ToString("0.0000") + ")");
                nextTimeLog += 2f;
            }
        }

        void Cast(Vector3 direction, string label)
        {
            RaycastHit hit;
            if (Physics.Raycast(Transform.Position, direction, out hit, rayLength, Entity))
                Log.Info("[MoveTest] Ray " + label + ": hit " + hit.Entity + " at " + hit.Point + ", normal " +
                         hit.Normal + ", " + hit.Distance.ToString("0.00") + " m");
            else
                Log.Info("[MoveTest] Ray " + label + ": nothing within " + rayLength + " m");
        }
    }
}
