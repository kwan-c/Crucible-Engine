using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;

namespace Crucible.Scripts
{
    // Run in the Capture mission: Run.ps1 -Probe CaptureObjectiveProbe -Scene "Game/Missions/Capture" -Label Capture
    // Holds the 1st area for its real hold time (40 s solo) and checks the fireworks
    // play and clean up, and that the wave golems are coordinated.
    public sealed class CaptureObjectiveProbe : EntityScript
    {
        const BindingFlags Any = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public;
        int failures;
        float age, completedAt = -1f;
        bool started, sawShows, finished, checkedEnemies;
        MissionController mission;
        ObjectiveFireworks fireworks;
        SubLevelGenerator generator;
        readonly List<Entity> roots = new List<Entity>();

        void Check(bool ok, string text)
        {
            if (!ok) failures++;
            Log.Info("[CaptureQA] " + (ok ? "PASS " : "FAIL ") + text);
        }

        void Finish()
        {
            finished = true;
            Log.Info("[CaptureQA] END failures=" + failures);
            System.IO.File.WriteAllText(Environment.GetEnvironmentVariable("CRUCIBLE_QA_RESULT"), failures.ToString());
        }

        IList Shows() { return (IList)typeof(ObjectiveFireworks).GetField("shows", Any).GetValue(fireworks); }

        public override void OnUpdate(float dt)
        {
            if (finished) return;
            age += dt;
            if (age > 150f) { Check(false, "finished in time (area state " + (mission != null && mission.Zones.Count > 0 ? mission.Zones[0].State.ToString() : "?") + ")"); Finish(); return; }

            if (mission == null)
            {
                Entity gen = Entity.Find("Sub Level Generator");
                if (gen == null) return;
                mission = gen.GetComponent<MissionController>();
                fireworks = gen.GetComponent<ObjectiveFireworks>();
                generator = gen.GetComponent<SubLevelGenerator>();
                return;
            }
            if (mission.Zones.Count == 0 || generator == null || generator.Players().Count == 0) return;

            MissionController.Zone area = mission.Zones[0];
            if (!started)
            {
                started = true;
                Check(EnemyHordeCoordinator.Active != null, "Capture has an active EnemyHordeCoordinator");
                Check(EnemySpecialCoordinator.Active != null, "Capture has an active EnemySpecialCoordinator");
                Check(fireworks != null, "ObjectiveFireworks is on the Sub Level Generator");
                Check(fireworks != null && fireworks.fireworks != null, "its fireworks prefab is assigned");
                Check(!area.IsExtract && area.Order == 1, "first zone is the 1st capture area");
                if (fireworks == null || fireworks.fireworks == null) { Finish(); return; }
            }

            Entity player = generator.Players()[0];
            if (area.State != HoldZoneState.Complete)
            {
                player.Transform.WorldPosition = area.Chunk.Transform.WorldPosition + new Vector3(0f, 1f, 0f);
                return;
            }

            if (completedAt < 0f)
            {
                completedAt = age;
                IList shows = Shows();
                Check(shows.Count == fireworks.copies, "area complete starts " + fireworks.copies + " fireworks (got " + shows.Count + ")");
                float ring = 0f;
                foreach (object show in shows)
                {
                    Entity root = (Entity)show.GetType().GetField("Root").GetValue(show);
                    roots.Add(root);
                    Vector3 d = root.Transform.WorldPosition - area.Chunk.Transform.WorldPosition;
                    ring = Math.Max(ring, (float)Math.Sqrt(d.X * d.X + d.Z * d.Z));
                    int emitting = 0;
                    foreach (string name in new[] { "Sparks 1", "Sparks 2", "Sparks 3" })
                    {
                        Entity child = root.FindChild(name);
                        ParticleEmitter fx = child != null ? child.GetComponent<ParticleEmitter>() : null;
                        if (fx != null && fx.IsEmitting && fx.BurstCount == 150 && fx.EmitLight) emitting++;
                    }
                    Check(emitting == 3, root.Name + ": all three tutorial Sparks emitters are playing");
                }
                Check(ring > 1f, "fireworks spread round the area (" + ring.ToString("0.0") + " m from its centre)");
                sawShows = true;
            }

            if (!checkedEnemies && age - completedAt > .2f)
            {
                checkedEnemies = true;
                int count = 0, coordinated = 0;
                foreach (Enemy enemy in Enemy.All)
                {
                    if (enemy.IsDead) continue;
                    count++;
                    if (enemy.useHordeCoordination && enemy.useSpecialCoordinator) coordinated++;
                }
                Log.Info("[CaptureQA] wave golems alive: " + count);
                Check(coordinated == count, "every wave golem opts into horde + special coordination (" + coordinated + "/" + count + ")");
            }

            if (sawShows && age - completedAt > fireworks.playSeconds + 2.5f)
            {
                Check(Shows().Count == 0, "fireworks stop after playSeconds");
                int alive = 0;
                foreach (Entity root in roots) if (root != null && root.IsAlive) alive++;
                Check(alive == 0, "every fireworks copy is destroyed after its sparks fade (" + alive + " left)");
                Finish();
            }
        }
    }
}
