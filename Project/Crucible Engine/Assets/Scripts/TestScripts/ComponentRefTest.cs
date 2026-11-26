using System;

namespace Crucible.Scripts
{
    // Tests component fields: `public Transform target;` assigned in the Inspector.
    //   1. Attach to an entity. Drag any entity onto "Target". Drag an entity WITH a
    //      Light onto "Lamp" - one without a Light is refused (the tooltip says why).
    //   2. Press Play. Target bobs up and down around where it started.
    //        F6  Lamp on / off
    //        F7  log what the fields point at (target.Entity.Name, ...)
    //   Then: delete Lamp's Light component in Edit mode - the field reads "Lamp (no Light)"
    //   and Play logs a warning and leaves the field null.
    public sealed class ComponentRefTest : EntityScript
    {
        public Transform target;
        public Light lamp;
        public float height = 0.5f;
        public float speed = 2f;

        private Vector3 start;
        private float time;

        public override void OnCreate()
        {
            if (target == null) Log.Warning("[ComponentRefTest] Drag an entity onto Target.");
            else start = target.Position;
            if (lamp == null) Log.Warning("[ComponentRefTest] Drag an entity with a Light onto Lamp.");
            Report();
        }

        public override void OnUpdate(float deltaTime)
        {
            time += deltaTime;
            if (target != null && target.Entity.IsAlive)
                target.Position = start + new Vector3(0f, (float)Math.Sin(time * speed) * height, 0f);

            if (Input.IsPressed(KeyCode.F6) && lamp != null)
                lamp.Enabled = !lamp.Enabled;
            if (Input.IsPressed(KeyCode.F7))
                Report();
        }

        private void Report()
        {
            Log.Info("[ComponentRefTest] target = " + (target != null ? target.Entity.Name : "null") +
                     ", lamp = " + (lamp != null ? lamp.Entity.Name + " (on: " + lamp.Enabled + ")" : "null"));
        }
    }
}
