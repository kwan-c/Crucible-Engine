using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    // Tests Prefab.Instantiate, Entity.Destroy and FindChild.
    //   1. Attach to any entity. Drag a .cprefab from the Asset Browser onto "Prefab".
    //   2. Optional: type the name of one of the prefab's child entities into "Child Name".
    //   3. Press Play, then:
    //        G  spawn a copy next to this entity (it destroys itself after "Lifetime" seconds)
    //        H  spawn a copy as a child of this entity
    //        K  destroy the newest copy right away (end of this frame)
    public sealed class SpawnTest : EntityScript
    {
        public Prefab prefab;
        public float lifetime = 3f;      // 0 = live until K or Stop
        public float spacing = 1.5f;     // how far apart copies are placed
        public string childName = "";

        readonly List<Entity> spawned = new List<Entity>();
        int count;

        public override void OnCreate()
        {
            if (prefab == null)
                Log.Warning("[SpawnTest] No prefab assigned - drag a .cprefab onto Prefab.");
            else
                Log.Info("[SpawnTest] Ready to spawn " + prefab.Name + ". G = spawn, H = spawn as child, K = destroy newest.");
        }

        public override void OnUpdate(float deltaTime)
        {
            spawned.RemoveAll(e => !e.IsAlive);   // timed destroys happen on their own

            if (prefab == null)
                return;

            if (Input.IsPressed(KeyCode.G))
            {
                ++count;
                Vector3 position = Transform.Position + new Vector3(spacing * count, 0f, 0f);
                Report(prefab.Instantiate(position, Quaternion.Identity), "at " + position);
            }

            if (Input.IsPressed(KeyCode.H))
            {
                ++count;
                Report(prefab.Instantiate(Entity), "as a child of " + Entity);
            }

            if (Input.IsPressed(KeyCode.K) && spawned.Count > 0)
            {
                Entity newest = spawned[spawned.Count - 1];
                Destroy(newest);
                // Still alive until the end of this frame:
                Log.Info("[SpawnTest] Destroying " + newest + " (alive until end of frame: " + newest.IsAlive + ")");
            }
        }

        void Report(Entity copy, string where)
        {
            if (copy == null)
            {
                Log.Error("[SpawnTest] Instantiate returned null - see the Console for why.");
                return;
            }
            spawned.Add(copy);
            Log.Info("[SpawnTest] Spawned " + copy + " " + where + ". Script on it: " +
                     (copy.GetComponent<EntityScript>() != null ? copy.GetComponent<EntityScript>().GetType().Name : "none"));

            if (childName.Length > 0)
            {
                Entity child = copy.FindChild(childName);
                Log.Info("[SpawnTest] FindChild(\"" + childName + "\") -> " + (child != null ? child.ToString() : "not found"));
            }

            if (lifetime > 0f)
                Destroy(copy, lifetime);
        }
    }
}
