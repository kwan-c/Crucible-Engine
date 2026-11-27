using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class EnemySpawnProbe : EntityScript
    {
        readonly List<string> roster = new List<string>();
        EnemySpawner small, elite;
        float age;
        int failures;
        bool killed, finished;
        void Check(bool ok, string text)
        {
            if (!ok)
                failures++;
            Log.Info("[SpawnQA] " + (ok ? "PASS " : "FAIL ") + text);
        }
        public override void OnUpdate(float dt)
        {
            if (finished)
                return;
            age += dt;
            if (age < .25f)
                return;
            if (small == null)
            {
                small = Entity.Find("Enemy Spawner Mace").GetComponent<EnemySpawner>();
                elite = Entity.Find("Enemy Spawner Cannon").GetComponent<EnemySpawner>();
                Check(small.enemiesPerWave == 3 && small.spawnStagger == 3f && small.waveInterval == 30f,
                      "existing authored wave cadence");
                // Shorten the wait between the first and second waves in this isolated copy.
                small.waveInterval = elite.waveInterval = 5f;
                small.spawnStagger = elite.spawnStagger = .3f;
            }
            foreach (Enemy enemy in Enemy.All)
                if (!enemy.IsDead && !roster.Contains(enemy.equippedWeaponId))
                    roster.Add(enemy.equippedWeaponId);
            if (!killed && age >= 6.3f)
            {
                Check(EnemySpecialCoordinator.Active != null && EnemyHordeCoordinator.Active != null,
                      "wave actors share the scene coordinators");
                foreach (string id in new[] { "sword", "mace", "throwing_axe", "small_crossbow", "cannon",
                                              "large_crossbow" })
                    Check(roster.Contains(id), "normal waves instantiate " + id);
                Check(roster.Count == 6, "only the approved armed roster entered the two waves");
                foreach (Enemy enemy in Enemy.All)
                    if (!enemy.IsDead)
                        enemy.ApplyDamage(10000);
                killed = true;
            }
            if (!killed || age < 6.7f)
                return;
            Check(small.ActiveCount + elite.ActiveCount == 0, "wave tracking removes deaths before ragdoll despawn");
            finished = true;
            Log.Info("[SpawnQA] END failures=" + failures);
            System.IO.File.WriteAllText(Environment.GetEnvironmentVariable("CRUCIBLE_QA_RESULT"), failures.ToString());
        }
    }
}
