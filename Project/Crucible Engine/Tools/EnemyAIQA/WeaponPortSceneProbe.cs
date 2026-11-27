using System;
using Crucible;

namespace Crucible.Scripts
{
    public sealed class WeaponPortSceneProbe : EntityScript
    {
        bool tutorial;
        float age;
        bool done;
        bool started;
        int failures;
        Enemy tutorialEnemy;
        MechPlayer player;
        bool detected, attacked;
        float startHp;
        public override void OnUpdate(float deltaTime)
        {
            if (done)
                return;
            age += deltaTime;
            if (age < 1f)
                return;
            if (started)
            {
                var front = Entity.Find("Player HUD")?.FindChild("Bar Front");
                var image = front?.GetComponent<UIImage>();
                bool matches = image != null && Math.Abs(image.HorizontalFill - player.Health.Normalized) < .005f;
                if (tutorial)
                {
                    detected |= tutorialEnemy.TargetDetected && tutorialEnemy.Target == player.Entity;
                    attacked |= tutorialEnemy.AttackInProgress;
                    bool damaged = player.Health.Current < startHp;
                    if (age < 8f && !(detected && attacked && damaged && matches))
                        return;
                    Check(detected, "placed Tutorial Mace wakes and acquires the player");
                    Check(attacked && damaged, "placed Tutorial Mace attacks and lowers player health");
                }
                Check(matches, "authored health bar follows current player health");
                Finish();
                return;
            }
            started = true;
            tutorial = Entity.Find("Fodder Mace Part 3") != null;
            try
            {
                player = Entity.Find("Player")?.GetComponent<MechPlayer>();
                Check(player != null && player.Weapons != null &&
                          Entity.Find("Player HUD")?.GetComponent<MechHud>() != null,
                      "player, loadout and HUD initialized");
                Check(Entity.Find("Zoo Pickup - Minigun") == null &&
                          Entity.Find("Combat Test - Spinner Wind-up Test") == null,
                      "Zoo-only content stays in the Zoo");
                if (tutorial)
                {
                    var names = new[] { "Fodder Part 5 (1)", "Fodder Part 5 (2)", "Fodder Part 5 (3)",
                                        "Fodder Part 5 (4)", "Fodder Mace Part 3" };
                    foreach (string name in names)
                    {
                        var enemy = Entity.Find(name)?.GetComponent<Enemy>();
                        bool pass = enemy != null && enemy.IsReady && enemy.equippedWeaponId == "mace" &&
                                    enemy.dropPrefab != null && enemy.Entity.FindChild("Weapon Model") != null;
                        if (!pass)
                            failures++;
                        Log.Info("[WeaponPortSceneQA] " + (pass ? "PASS " : "FAIL ") + name +
                                 " loads with mounted Mace and matching drop");
                    }
                    tutorialEnemy = Entity.Find("Fodder Mace Part 3").GetComponent<Enemy>();
                    Check(EnemyHordeCoordinator.Active != null, "Tutorial scene provides horde coordination");
                    tutorialEnemy.Wake();
                    player.Transform.WorldPosition =
                        tutorialEnemy.Position + tutorialEnemy.Facing * 3f + Vector3.Up * .8f;
                    startHp = player.Health.Current;
                }
                else
                {
                    var names = new[] { "Pickup sword 1",          "Pickup mace 2",           "Pickup throwing_axe 3",
                                        "Pickup small_crossbow 4", "Pickup large_crossbow 5", "Pickup cannon 6" };
                    foreach (string name in names)
                    {
                        var part = Entity.Find(name)?.GetComponent<MechPart>();
                        bool pass = part != null && part.Weapon != null && part.mountFrame && part.CanAnimate;
                        if (!pass)
                            failures++;
                        Log.Info("[WeaponPortSceneQA] " + (pass ? "PASS " : "FAIL ") + name +
                                 " loads its existing animated model");
                    }
                    player.Health.ApplyDamage(10f);
                    var cannon = Entity.Find("Pickup cannon 6")?.GetComponent<MechPart>();
                    if (player != null && cannon != null)
                    {
                        bool swap = player.Equip(cannon, MechSocket.ShoulderR) && cannon.HasMuzzle && cannon.IsMounted;
                        if (!swap)
                            failures++;
                        Log.Info("[WeaponPortSceneQA] " + (swap ? "PASS " : "FAIL ") +
                                 "Showcase Cannon equips on right shoulder");
                    }
                }
            }
            catch (Exception ex)
            {
                failures++;
                Log.Error("[WeaponPortSceneQA] " + ex);
                Finish();
            }
        }
        void Check(bool ok, string description)
        {
            if (!ok)
                failures++;
            Log.Info("[WeaponPortSceneQA] " + (ok ? "PASS " : "FAIL ") + description);
        }
        void Finish()
        {
            done = true;
            Log.Info("[WeaponPortSceneQA] END failures=" + failures);
            System.IO.File.WriteAllText(Environment.GetEnvironmentVariable("CRUCIBLE_QA_RESULT"), failures.ToString());
        }
    }
}
