using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    // Run in the Capture mission: Run.ps1 -Probe LevelManagerProbe -Scene "Game/Missions/Capture" -Label LevelManager -TimeoutSeconds 200
    // Checks the Level Manager prefab reads the real game time, player, objectives and horde,
    // and that its tuning multipliers change nothing.
    public sealed class LevelManagerProbe : EntityScript
    {
        int failures, step;
        float age, mark, clockAtMark;
        bool finished;
        LevelManager level;
        MissionController mission;
        SubLevelGenerator generator;
        UICanvas pause;

        void Check(bool ok, string text)
        {
            if (!ok) failures++;
            Log.Info("[LevelQA] " + (ok ? "PASS " : "FAIL ") + text);
        }

        void Finish()
        {
            finished = true;
            Log.Info("[LevelQA] " + (level != null ? level.Summary() : "no level manager"));
            Log.Info("[LevelQA] END failures=" + failures);
            System.IO.File.WriteAllText(Environment.GetEnvironmentVariable("CRUCIBLE_QA_RESULT"), failures.ToString());
        }

        static bool Near(float a, float b) { return Math.Abs(a - b) < 0.01f; }

        public override void OnUpdate(float dt)
        {
            if (finished) return;
            age += dt;
            if (age > 160f) { Check(false, "finished in time (step " + step + ")"); Finish(); return; }

            if (mission == null)
            {
                Entity gen = Entity.Find("Sub Level Generator");
                if (gen == null) return;
                mission = gen.GetComponent<MissionController>();
                generator = gen.GetComponent<SubLevelGenerator>();
                return;
            }
            if (mission.Zones.Count == 0 || generator.Players().Count == 0 || SpawnDirector.Instance == null) return;
            Entity player = generator.Players()[0];
            MissionController.Zone area = mission.Zones[0];

            switch (step)
            {
                case 0:
                    level = LevelManager.Active;
                    Check(level != null, "Capture has an active LevelManager (from Prefabs/Level/Level Manager)");
                    if (level == null) { Finish(); return; }
                    Check(level.Entity.Name == "Level Manager", "it is the Level Manager entity");
                    Check(EnemyHordeCoordinator.Active != null && EnemySpecialCoordinator.Active != null, "Capture's Enemy Coordination entity is back");
                    Check(!level.AppliesTuning && level.dropRateMultiplier == 1f && level.hordeSizeMultiplier == 1f
                        && level.enemyDamageMultiplier == 1f && level.enemyHealthMultiplier == 1f, "tuning starts neutral and is not applied");
                    Check(level.Mission == mission && level.Spawner == SpawnDirector.Instance, "found MissionController and SpawnDirector by itself");
                    Entity menu = Entity.Find("PauseMenuParent");
                    pause = menu != null ? menu.GetComponent<UICanvas>() : null;
                    Check(pause != null && !level.Paused, "pause menu found and the level is not paused");
                    mark = age; clockAtMark = level.ElapsedGameSeconds;
                    step++;
                    break;

                case 1:   // the clock runs with game time
                    if (age - mark < 2f) return;
                    float ran = level.ElapsedGameSeconds - clockAtMark;
                    Check(Math.Abs(ran - (age - mark)) < 0.1f, "game clock advanced " + ran.ToString("0.00") + " s in " + (age - mark).ToString("0.00") + " s");
                    MechPlayer mech = player.GetComponent<MechPlayer>();
                    Check(level.Player == player && level.PlayerState == LevelPlayerState.Alive && level.PlayerCount == 1 && level.PlayersAlive == 1,
                        "player found, alive (" + level.PlayersAlive + "/" + level.PlayerCount + ")");
                    Check(mech != null && Near(level.PlayerHealth, mech.Health.Current) && Near(level.PlayerMaximumHealth, mech.Health.Maximum),
                        "player health " + level.PlayerHealth + "/" + level.PlayerMaximumHealth);
                    float before = mech.Health.Current;
                    mech.Health.ApplyDamage(10f);
                    level.Refresh();
                    Check(Near(level.PlayerHealth, before - 10f) && Near(level.PlayerHealthFraction, mech.Health.Normalized), "player damage shows at once (" + level.PlayerHealth + ")");
                    Check(level.ObjectivesTotal == mission.AreasTotal + 1 && level.ObjectivesCompleted == 0 && level.AreasTotal == mission.AreasTotal
                        && level.CurrentArea == 1, "objectives 0/" + level.ObjectivesTotal + " (" + level.AreasTotal + " areas + extract), current area 1");
                    // pause: the clock stops while the pause menu is open
                    pause.Enabled = true;
                    mark = age; clockAtMark = -1f;
                    step++;
                    break;

                case 2:
                    if (clockAtMark < 0f) { if (age - mark > .2f) { clockAtMark = level.ElapsedGameSeconds; mark = age; } return; }
                    if (age - mark < 1f) return;
                    Check(level.Paused && Near(level.ElapsedGameSeconds, clockAtMark), "pause menu open: paused, clock stays at " + level.ElapsedGameSeconds.ToString("0.00"));
                    pause.Enabled = false;
                    step++;
                    break;

                case 3:   // hold the 1st area until it completes (real 40 s hold)
                    player.Transform.WorldPosition = area.Chunk.Transform.WorldPosition + new Vector3(0f, 1f, 0f);
                    if (area.State == HoldZoneState.Holding && level.CurrentAreaProgress > 0.1f && mark >= 0f)
                    {
                        Check(Near(level.CurrentAreaProgress, Math.Min(1f, area.Progress)), "area 1 hold progress " + (level.CurrentAreaProgress * 100f).ToString("0") + "%");
                        mark = -1f;
                    }
                    if (area.State != HoldZoneState.Complete) return;
                    level.Refresh();
                    Check(level.AreasCompleted == 1 && level.ObjectivesCompleted == 1 && level.CurrentArea == 2
                        && Near(level.ObjectiveProgress, 1f / level.ObjectivesTotal), "area 1 captured: objectives " + level.ObjectivesCompleted + "/" + level.ObjectivesTotal + ", current area " + level.CurrentArea);
                    CheckHorde();
                    Finish();
                    break;
            }
        }

        void CheckHorde()
        {
            int alive = 0;
            foreach (Enemy e in Enemy.All) if (e.Entity.IsAlive && e.Entity.IsActive && !e.IsDead) alive++;
            SpawnDirector director = SpawnDirector.Instance;
            Check(level.ActiveEnemies == alive && alive > 0, "enemies alive " + level.ActiveEnemies + " (Enemy.All says " + alive + ")");
            Check(level.TotalSpawned == director.TotalSpawned && level.TotalSpawned > 0 && level.CurrentWave == director.CurrentWave,
                "spawned " + level.TotalSpawned + ", wave '" + level.CurrentWave + "' with " + level.WaveResourceLeft + " left");

            int armed = 0, right = 0;
            var maxBefore = new Dictionary<Enemy, float>();
            foreach (LevelManager.EnemyInfo info in level.Enemies)
            {
                maxBefore[info.Enemy] = info.Enemy.MaximumHealth;
                if (info.WeaponId.Length == 0) continue;
                armed++;
                MechPartDef def = MechPartCatalogue.Find(info.WeaponId);
                if (def != null && Near(info.AttackDamage, def.Damage * info.Enemy.weaponDamageMultiplier) && Near(info.CurrentHealth, info.Enemy.CurrentHealth)
                    && Near(info.DropChancePercent, info.Enemy.dropChancePercent)) right++;
            }
            Check(armed > 0 && right == armed, "per-enemy HP, drop chance and weapon damage match the enemies (" + right + "/" + armed + ")");

            // Observation only: turning every multiplier up changes nothing.
            level.dropRateMultiplier = level.hordeSizeMultiplier = level.enemyDamageMultiplier = level.enemyHealthMultiplier = 3f;
            level.Refresh();
            bool same = true;
            foreach (LevelManager.EnemyInfo info in level.Enemies)
            {
                float m;
                if (maxBefore.TryGetValue(info.Enemy, out m) && !Near(m, info.Enemy.MaximumHealth)) same = false;
            }
            Check(same && level.TotalSpawned == director.TotalSpawned, "multipliers at 3x leave enemy HP and spawning alone (not applied)");
        }
    }
}
