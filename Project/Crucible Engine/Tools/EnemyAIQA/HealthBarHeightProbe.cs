using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    // Run in the Zoo: Run.ps1 -Probe HealthBarHeightProbe -Label HealthBar
    // Spawns every Zoo golem, lets it settle, then checks that its health bar floats
    // above the top of its body, head and weapon models (their mesh bounds, in the world).
    public sealed class HealthBarHeightProbe : EntityScript
    {
        readonly string[] labels = { "Sword Fodder", "Mace Fodder", "Throwing Axe Fodder", "Small Crossbow Fodder",
                                     "Heavy Cannon", "Heavy Crossbow", "Spinner Wind-up Test" };
        // Local mesh bounds (min y, max y and the x/z extents) of the models the golems use, read from the .glb files.
        static readonly Dictionary<string, float[]> bounds = new Dictionary<string, float[]>
        {   // minX, minY, minZ, maxX, maxY, maxZ
            { "Body",         new[] { -1.79f, -0.08f, -2.06f, 1.79f, 2.53f, 2.05f } },   // ENEMY_Fodder
            { "Head",         new[] { -0.83f,  0.22f, -0.79f, 0.83f, 1.50f, 0.68f } },   // ENEMYPART_Head
            { "Weapon Mount", new[] { -0.83f,  0.22f, -0.79f, 0.83f, 1.50f, 0.68f } },   // ENEMYPART_Head
        };
        // The weapon models draw one node subtree of their .glb, rebased to that node (MeshRenderer.modelRoot).
        static readonly Dictionary<string, float[]> weapons = new Dictionary<string, float[]>
        {
            { "Golem Sword",          new[] { -0.60f,  2.09f, -6.22f, 0.53f,  3.62f, 0.44f } },   // golem2_v3 SwordArmRig
            { "Golem Mace",           new[] {  2.33f, -5.76f, -1.79f, 5.85f, -1.35f, 1.83f } },   // golem2_v3 MaceArmRig
            { "Golem Throwing Axe",   new[] { -0.97f, -3.93f, -1.18f, 0.98f,  1.06f, 0.98f } },   // golem2_v3 ThrowingAxeRig
            { "Golem Crossbow",       new[] { -1.22f, -1.05f, -0.76f, 1.18f,  2.04f, 0.33f } },   // golem2_v3 CrossBowArmRig
            { "Golem Heavy Crossbow", new[] { -2.17f, -1.95f, -0.37f, 2.24f,  2.49f, 0.18f } },   // golem2_v3 CrossBowRig
            { "Golem Cannon",         new[] { -0.61f, -2.53f,  0.06f, 0.61f,  1.49f, 1.72f } },   // SHOULDER_Cannon Mount_Shoulder
        };
        const float MinimumGap = 0.15f;

        readonly List<Entity> spawned = new List<Entity>();
        int frame, failures;
        bool done;

        void Check(bool ok, string text)
        {
            if (!ok) failures++;
            Log.Info("[BarQA] " + (ok ? "PASS " : "FAIL ") + text);
        }

        public override void OnUpdate(float dt)
        {
            if (done) return;
            frame++;
            if (frame == 30)
            {
                for (int i = 0; i < labels.Length; i++)
                {
                    Entity pad = Entity.Find("Combat Test - " + labels[i]);
                    ZooCombatSpawnPad spawner = pad != null ? pad.GetComponent<ZooCombatSpawnPad>() : null;
                    Check(spawner != null && spawner.enemyPrefab != null, labels[i] + " pad has a prefab");
                    if (spawner == null || spawner.enemyPrefab == null) continue;
                    spawned.Add(spawner.enemyPrefab.Instantiate(new Vector3(-55f + 9f * i, 1.2f, -46f), Quaternion.Identity));
                }
                return;
            }
            if (frame < 150) return;

            foreach (Entity golem in spawned)
            {
                float top = float.MinValue;
                string tallest = "";
                Measure(golem, golem, ref top, ref tallest);
                Entity bar = golem.FindChild("Health Bar");
                float barY = bar.Transform.WorldPosition.Y;
                float rootY = golem.Transform.WorldPosition.Y;
                Log.Info("[BarQA] " + golem.Name + ": root y " + rootY.ToString("0.00") + ", top of " + tallest + " +" + (top - rootY).ToString("0.00")
                    + ", bar +" + (barY - rootY).ToString("0.00") + " (healthBarHeight " + golem.GetComponent<Enemy>().healthBarHeight + ")");
                Check(barY - top >= MinimumGap, golem.Name + ": bar is above the model (" + (barY - top).ToString("0.00") + " m clear)");
                Check(barY - top <= 1.4f, golem.Name + ": bar is not floating far above it (" + (barY - top).ToString("0.00") + " m)");
            }
            done = true;
            Log.Info("[BarQA] END failures=" + failures);
            System.IO.File.WriteAllText(Environment.GetEnvironmentVariable("CRUCIBLE_QA_RESULT"), failures.ToString());
        }

        void Measure(Entity golem, Entity e, ref float top, ref string tallest)
        {
            float[] b = null;
            if (bounds.ContainsKey(e.Name)) b = bounds[e.Name];
            else if (e.Name == "Weapon Model" && weapons.ContainsKey(golem.Name)) b = weapons[golem.Name];
            if (b != null && e.HasComponent<MeshRenderer>() && e.MeshRenderer.Visible)
            {
                Vector3 s = ChainScale(e);
                for (int c = 0; c < 8; c++)
                {
                    Vector3 corner = new Vector3((c & 1) != 0 ? b[3] : b[0], (c & 2) != 0 ? b[4] : b[1], (c & 4) != 0 ? b[5] : b[2]);
                    Vector3 world = e.Transform.WorldPosition + e.Transform.WorldRotation * new Vector3(corner.X * s.X, corner.Y * s.Y, corner.Z * s.Z);
                    if (world.Y > top) { top = world.Y; tallest = e.Name; }
                }
            }
            for (int i = 0; i < e.Transform.ChildCount; i++) Measure(golem, e.Transform.GetChild(i), ref top, ref tallest);
        }

        static Vector3 ChainScale(Entity e)
        {
            Vector3 s = new Vector3(1f, 1f, 1f);
            for (Entity p = e; p != null && p.IsAlive; p = p.Transform.Parent)
            {
                Vector3 l = p.Transform.Scale;
                s = new Vector3(s.X * Math.Abs(l.X), s.Y * Math.Abs(l.Y), s.Z * Math.Abs(l.Z));
            }
            return s;
        }
    }
}
