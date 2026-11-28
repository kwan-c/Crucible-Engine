/*************************************************************************
* \file:         ZooWeaponDropPad.cs
* \brief         Zoo loot test: step on the pad and press G to drop a random
*                weapon, as if an enemy had just died and dropped it. G, not
*                F: F cycles the aim / lock-on mode.
*
* \details
*    The dropped part is a normal pickup prefab (Prefabs/Parts), so it rolls
*    its stats +-10% exactly like an enemy's drop (MechPart.rollLootStats ->
*    MechPartStatRoll, Unity's PartStatRoll), lands on the ground and can be
*    swapped in with 1 / 2. The roll is written to the console and shown on
*    the pad's label; there is no in-game stat comparison UI yet.
*
*        Weapon Drop Pad   Collider (Is Trigger, taller than the button), Script: ZooWeaponDropPad
*        |- Button         MeshRenderer: the small orange cube to step on (no collider)
*        |- Label          WorldText
*        '- Spawn Point    empty entity where drops appear (else above the pad)
*
*    Script fields can't be lists yet, so the pool is six Prefab slots;
*    empty slots are skipped. At most maximumLoose drops lie around: the
*    oldest one nobody picked up is removed to make room.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class ZooWeaponDropPad : EntityScript
    {
        public Prefab drop1;
        public Prefab drop2;
        public Prefab drop3;
        public Prefab drop4;
        public Prefab drop5;
        public Prefab drop6;
        public int maximumLoose = 6;
        public float spawnHeight = 1f;          // above the spawn point; MechPart settles it onto the ground
        public float scatterRadius = 1.5f;      // random offset so drops don't stack on one spot

        static readonly Random random = new Random();
        readonly List<Prefab> pool = new List<Prefab>(6);
        readonly List<Entity> loose = new List<Entity>();
        ZooPad pad;
        Entity spawnPoint;
        Entity pendingReport;

        public override void OnCreate()
        {
            pad = new ZooPad(this);   // G (ZooPad's default), not F: F cycles the aim / lock-on mode
            spawnPoint = Entity.FindChild("Spawn Point");
            foreach (Prefab prefab in new[] { drop1, drop2, drop3, drop4, drop5, drop6 })
                if (prefab != null) pool.Add(prefab);
            pad.SetText(pool.Count > 0 ? "Step on me and press G\nto drop a random weapon" : "No drop prefabs assigned");
        }

        public override void OnTriggerEnter(Entity other) { pad.Enter(other); }
        public override void OnTriggerExit(Entity other) { pad.Exit(other); }

        public override void OnUpdate(float deltaTime)
        {
            if (pendingReport != null) Report();
            if (pad.WasUsed(KeyCode.G)) Drop();
        }

        void Drop()
        {
            if (pool.Count == 0) return;
            Prune();
            while (loose.Count >= Math.Max(1, maximumLoose))
            {
                Destroy(loose[0]);
                loose.RemoveAt(0);
            }

            Vector3 origin = spawnPoint != null ? spawnPoint.Transform.WorldPosition : Transform.WorldPosition;
            double angle = random.NextDouble() * Math.PI * 2.0;
            float distance = (float)Math.Sqrt(random.NextDouble()) * Math.Max(0f, scatterRadius);
            Vector3 position = origin + new Vector3((float)Math.Cos(angle) * distance, spawnHeight, (float)Math.Sin(angle) * distance);
            // Same as an enemy's drop (Enemy.OnDied): a random yaw.
            Quaternion rotation = Quaternion.Euler(0f, (float)(random.NextDouble() * 360.0), 0f);

            Entity drop = pool[random.Next(pool.Count)].Instantiate(position, rotation);
            if (drop == null) return;
            loose.Add(drop);
            pendingReport = drop;
            Report();
        }

        // The part rolls in its OnCreate; if that hasn't run yet this frame, report next frame.
        void Report()
        {
            MechPart part = pendingReport != null && pendingReport.IsAlive ? pendingReport.GetComponent<MechPart>() : null;
            if (part == null || part.Def == null) { if (pendingReport == null || !pendingReport.IsAlive) pendingReport = null; return; }
            pendingReport = null;
            string roll = MechPartStatRoll.Describe(part.Def);
            Log.Info("[ZooWeaponDropPad] dropped " + roll);
            pad.SetText("Press G to drop a random weapon\n\nLast: " + roll);
        }

        // Forget drops that were picked up (now mounted) or destroyed. One spawned this frame may
        // not have its script yet: keep it.
        void Prune()
        {
            for (int i = loose.Count - 1; i >= 0; i--)
            {
                Entity e = loose[i];
                if (e == null || !e.IsAlive) { loose.RemoveAt(i); continue; }
                MechPart part = e.GetComponent<MechPart>();
                if (part != null && part.IsMounted) loose.RemoveAt(i);
            }
        }
    }
}
