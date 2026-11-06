using System;

namespace Crucible.Scripts
{
    // Tests the Collider and MeshRenderer API.
    //   1. Attach to an entity with a Collider, a MeshRenderer and a RigidBody (a crate
    //      above the floor, Collider layer Moving). Turn on physics debug draw to see the collider.
    //   2. Press Play, then:
    //        T  toggle Is Trigger      - the crate falls through the floor while on
    //        V  toggle MeshRenderer.Visible - the crate hides but still collides
    //        K  SetEnabled<Collider> on / off - no collisions at all while off
    //        J  cycle the shape: Box -> Sphere -> Capsule
    //        9 / 0  shrink / grow the collider (Size, or Radius and Height)
    //        Y  toggle Bounciness 0 <-> 0.9 (drop it again with R to see the bounce)
    //        R  put it back where it started
    //   Collision and trigger callbacks are logged, so T should switch between the two.
    public sealed class ColliderTest : EntityScript
    {
        Collider col;
        MeshRenderer mesh;
        Vector3 start;

        public override void OnCreate()
        {
            col = GetComponent<Collider>();
            mesh = GetComponent<MeshRenderer>();
            start = Transform.Position;
            if (col == null) Log.Warning("[ColliderTest] " + Entity + " has no Collider component.");
            if (mesh == null) Log.Warning("[ColliderTest] " + Entity + " has no MeshRenderer component.");
            if (col != null) Log.Info("[ColliderTest] " + Describe());
        }

        string Describe()
        {
            return col.Shape + " size " + col.Size + " (world " + col.WorldSize + "), radius " + col.Radius +
                   ", height " + col.Height + ", center " + col.Center + ", friction " + col.Friction +
                   ", bounciness " + col.Bounciness + ", layer " + col.Layer + ", trigger " + col.IsTrigger +
                   ", enabled " + Entity.IsEnabled<Collider>();
        }

        public override void OnUpdate(float deltaTime)
        {
            if (mesh != null && Input.IsPressed(KeyCode.V))
            {
                mesh.Visible = !mesh.Visible;
                Log.Info("[ColliderTest] Visible " + mesh.Visible);
            }
            if (col == null)
                return;

            if (Input.IsPressed(KeyCode.T))
            {
                col.IsTrigger = !col.IsTrigger;
                Log.Info("[ColliderTest] IsTrigger " + col.IsTrigger);
            }
            if (Input.IsPressed(KeyCode.K))
            {
                bool on = !Entity.IsEnabled<Collider>();
                Entity.SetEnabled<Collider>(on);
                Log.Info("[ColliderTest] Collider enabled " + on);
            }
            if (Input.IsPressed(KeyCode.J))
            {
                col.Shape = (ColliderShape)(((int)col.Shape + 1) % 3);
                Log.Info("[ColliderTest] " + Describe());
            }
            if (Input.IsPressed(KeyCode.Num9)) Resize(0.8f);
            if (Input.IsPressed(KeyCode.Num0)) Resize(1.25f);
            if (Input.IsPressed(KeyCode.Y))
            {
                col.Bounciness = col.Bounciness > 0f ? 0f : 0.9f;
                Log.Info("[ColliderTest] Bounciness " + col.Bounciness);
            }
            if (Input.IsPressed(KeyCode.R))
            {
                Transform.Position = start;
                Log.Info("[ColliderTest] Reset to " + start);
            }

            // Bad values must be refused, not applied.
            if (Input.IsPressed(KeyCode.B))
            {
                Vector3 before = col.Size;
                col.Size = new Vector3(0f, 1f, 1f);
                col.Radius = -1f;
                Log.Info("[ColliderTest] Bad values refused: " + (col.Size.X == before.X && col.Radius > 0f));
            }
        }

        void Resize(float factor)
        {
            Vector3 s = col.Size;
            col.Size = new Vector3(s.X * factor, s.Y * factor, s.Z * factor);
            col.Radius = col.Radius * factor;
            col.Height = col.Height * factor;
            Log.Info("[ColliderTest] " + Describe());
        }

        public override void OnCollisionEnter(Collision collision) { Log.Info("[ColliderTest] OnCollisionEnter " + collision.Other); }
        public override void OnTriggerEnter(Entity other)           { Log.Info("[ColliderTest] OnTriggerEnter " + other); }
    }
}
