using System;

namespace Crucible.Scripts
{
    // Attach to any entity, drag ANOTHER entity onto "Target" in the Inspector, press Play.
    //   - OnCreate logs who the target is (or that it's unassigned).
    //   - The target bobs up and down - proof this script is moving a different entity.
    //   - Press T to log both entities' positions, and whether the target is still alive.
    public sealed class EntityRefTest : EntityScript
    {
        public Entity target;
        public float bobHeight = 0.5f;
        public float bobSpeed = 2f;

        Vector3 targetStart;
        float time;

        public override void OnCreate()
        {
            if (target == null)
            {
                Log.Warning("[EntityRefTest] " + Entity + ": no target assigned - drag an entity onto Target.");
                return;
            }
            targetStart = target.Transform.Position;
            Log.Info("[EntityRefTest] " + Entity + " is controlling " + target + ", which starts at " + targetStart);
            if (target == Entity)
                Log.Warning("[EntityRefTest] Target is this entity itself.");
        }

        public override void OnUpdate(float deltaTime)
        {
            if (target == null)
                return;

            if (target.IsAlive)
            {
                time += deltaTime;
                float y = (float)Math.Sin(time * bobSpeed) * bobHeight;
                target.Transform.Position = targetStart + new Vector3(0f, y, 0f);
            }

            if (Input.IsPressed(KeyCode.T))
                Log.Info("[EntityRefTest] Me: " + Transform.Position + " | " + target + ": " +
                         target.Transform.Position + ", alive: " + target.IsAlive);
        }
    }
}
