using System;

namespace Crucible.Scripts
{
    // A tutorial checkpoint: shows the player where to walk, and disappears once they step in.
    //
    // Setup:
    //   1. Make an entity for the area and give it a Collider with Is Trigger ticked (no RigidBody).
    //      Size the collider to the area the player has to reach.
    //   2. Add this script to it.
    //   3. Optional: add a child entity as the marker (a glowing mesh, an arrow, and/or a WorldText
    //      with Billboard FaceCamera) and drag it onto "Marker". It bobs and spins so it stands out.
    //   4. Optional: chain checkpoints. Tick "Start Hidden" on every checkpoint after the first, and
    //      drag the next one onto "Next". Reaching one hides it and shows the next.
    //
    // Only the player (an entity with a PlayerController) sets it off.
    public sealed class Checkpoint : EntityScript
    {
        public Entity marker;              // child visual that bobs and spins (optional)
        public string message = "";        // shown on the marker's WorldText, if it has one (empty = leave as is)
        public Entity next;                // checkpoint to show once this one is reached (optional)
        public bool startHidden = false;   // tick for checkpoints that wait for an earlier one

        public float bobHeight = 0.25f;    // metres up and down
        public float bobSpeed = 2f;        // bobs per second-ish
        public float spinSpeed = 90f;      // degrees per second, 0 to stop spinning

        bool reached;
        Vector3 markerStart;
        float spin;

        public override void OnCreate()
        {
            if (!HasComponent<Collider>())
                Log.Warning("[Checkpoint] " + Entity + " has no Collider - add one with Is Trigger ticked.");
            else if (!Entity.Collider.IsTrigger)
                Log.Warning("[Checkpoint] " + Entity + "'s Collider is not a trigger - the player will bump into it.");

            if (marker != null)
            {
                markerStart = marker.Transform.Position;
                WorldText text = marker.GetComponent<WorldText>();
                if (text != null && message.Length > 0)
                    text.Text = message;
            }

            if (startHidden)
                Entity.SetActive(false);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (marker == null || reached)
                return;

            float bob = (float)Math.Sin(Time.ElapsedTime * bobSpeed * Math.PI) * bobHeight;
            marker.Transform.Position = markerStart + new Vector3(0f, bob, 0f);

            if (spinSpeed != 0f)
            {
                spin = (spin + spinSpeed * deltaTime) % 360f;
                marker.Transform.WorldRotation = Quaternion.Euler(0f, spin, 0f);
            }
        }

        public override void OnTriggerEnter(Entity other)
        {
            if (reached || other == null || !other.HasComponent<PlayerController>())
                return;

            reached = true;
            Log.Info("[Checkpoint] " + Entity.Name + " reached.");

            if (next != null)
                next.SetActive(true);

            Entity.SetActive(false);   // hides the area, the marker and anything else under it
        }
    }
}
