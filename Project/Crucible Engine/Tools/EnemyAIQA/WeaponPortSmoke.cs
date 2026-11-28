using System;
using System.Collections.Generic;
using System.Reflection;
using Crucible;

namespace Crucible.Scripts
{
    public sealed class WeaponPortSmoke : EntityScript
    {
        readonly Vector3 arena = new Vector3(-55f, 1.5f, -46f);
        MechPlayer player;
        Entity front, behind;
        float frontDamage, behindDamage;
        int frontHits;
        float wait;
        int failures;
        int checks;
        IEnumerator<float> steps;
        bool completed;
        PropertyInfo currentTarget = typeof(MechTargetLock).GetProperty("CurrentTarget");
        MethodInfo spawnPad = typeof(ZooCombatSpawnPad).GetMethod("SpawnEnemy", BindingFlags.Instance | BindingFlags.NonPublic);
        FieldInfo spawned = typeof(ZooCombatSpawnPad).GetField("spawned", BindingFlags.Instance | BindingFlags.NonPublic);
        FieldInfo padField = typeof(ZooCombatSpawnPad).GetField("pad", BindingFlags.Instance | BindingFlags.NonPublic);

        public override void OnCreate() { steps = CheckAll(); }
        public override void OnUpdate(float deltaTime)
        {
            if (completed) return;
            wait -= deltaTime;
            if (wait > 0f) return;
            try
            {
                if (steps.MoveNext()) { wait = steps.Current; return; }
            }
            catch (Exception error) { Check(false, "exception " + error); }
            completed = true;
            if (checks < 174) { failures++; Log.Error("[WeaponPortQA] Only " + checks + " of at least 174 checks ran"); }
            Log.Info("[WeaponPortQA] " + checks + " checks; END failures=" + failures);
            System.IO.File.WriteAllText(Environment.GetEnvironmentVariable("CRUCIBLE_QA_RESULT"), failures.ToString());
        }

        void Check(bool passed, string description)
        {
            checks++;
            if (!passed) failures++;
            Log.Info("[WeaponPortQA] " + (passed ? "PASS " : "FAIL ") + description);
        }

        void PutPlayer(Vector3 point)
        {
            player.Transform.WorldPosition = point;
            player.Transform.WorldRotation = Quaternion.Identity;
        }

        void Select(Entity target)
        {
            currentTarget.GetSetMethod(true).Invoke(player.Targeting, new object[] { MechCombatTargets.Owning(target) });
        }

        void PutTargets(float frontDistance, float secondDistance, float secondSide)
        {
            frontDamage = behindDamage = 0f;
            frontHits = 0;
            front.Transform.WorldPosition = arena + new Vector3(0f, .3f, -frontDistance);
            behind.Transform.WorldPosition = arena + new Vector3(secondSide, .3f, -secondDistance);
        }

        MechPart GetPart(string name)
        {
            Entity e = Entity.Find(name);
            return e != null ? e.GetComponent<MechPart>() : null;
        }

        IEnumerator<float> CheckAll()
        {
            yield return .7f;
            player = Entity.Find("Player")?.GetComponent<MechPlayer>();
            front = Entity.Find("QA target A");
            behind = Entity.Find("QA target B");
            Check(player != null && player.Weapons != null && Camera.Main != null && front != null && behind != null,
                "Zoo player, active camera, loadout and both combat targets");
            if (player == null || front == null || behind == null) yield break;
            player.hitStop = false;
            var frontHealth = front.GetComponent<CombatHealth>();
            frontHealth.SetMaximum(100000f, true);
            MechCombatTargets.Register(front, (damage, from) => { frontHealth.ApplyDamage(damage); frontDamage += damage; frontHits++; }, .25f);
            var rearHealth = behind.GetComponent<CombatHealth>();
            rearHealth.SetMaximum(100000f, true);
            MechCombatTargets.Register(behind, (damage, from) => { rearHealth.ApplyDamage(damage); behindDamage += damage; }, .25f);
            PutPlayer(arena);
            yield return .15f;
            var camera = Camera.Main;
            front.Transform.WorldPosition = camera.Entity.Transform.WorldPosition + camera.Entity.Transform.Forward * 12f;
            yield return .1f;
            player.Targeting.ToggleHardLock(camera);
            Check(player.Targeting.IsHardLocked && player.Targeting.CurrentTarget.Entity == front,
                "normal hard lock acquires the visible combat target");
            player.Targeting.ToggleHardLock(camera);
            player.Targeting.EnterFreeAim();

            var names = new[] { "arm_sword", "arm_mace", "arm_throwingaxe", "arm_crossbow", "shoulder_crossbow",
                "shoulder_cannon_specialist", "Zoo Pickup - Blaster", "Zoo Pickup - Hammer", "Zoo Pickup - Spear",
                "Zoo Pickup - Supersword", "Zoo Pickup - Minigun", "Zoo Pickup - Hook", "Zoo Pickup - Shotgun",
                "Zoo Pickup - Scythe", "Zoo Pickup - Punchy Arm", "Zoo Pickup - Shield", "Zoo Pickup - Giant Rock",
                "Zoo Pickup - Metal Blades", "Zoo Pickup - Fishing Line", "Zoo Pickup - Mortar", "Zoo Pickup - Katyusha",
                "Zoo Pickup - Tentacles", "Zoo Pickup - Tesla Coil" };
            Entity firstPad = Entity.Find("Combat Test - Sword Fodder");
            if (firstPad != null)
            {
                PutPlayer(firstPad.Transform.WorldPosition + Vector3.Up * 1.25f);
                yield return .25f;
                var tracker = (ZooPad)padField.GetValue(firstPad.GetComponent<ZooCombatSpawnPad>());
                Check(tracker != null && tracker.Activated, "Zoo combat pad detects player overlap");
            }

            foreach (string name in names)
            {
                MechPart part = GetPart(name);
                if (part == null || part.Def == null || part.Weapon == null)
                {
                    Check(false, name + " pickup loads");
                    continue;
                }
                MechPartDef def = part.Def;
                MechSocket socket = def.Kind == MechPartKind.Arm ? MechSocket.ArmL : MechSocket.ShoulderL;
                Check(player.Equip(part, socket) && player.EquippedPart(socket) == def && part.IsMounted,
                    def.Id + " pickup equips and swaps (" + socket + ")");
                if (!part.IsMounted) continue;
                if (part.HasMuzzle)
                {
                    Vector3 muzzle = part.MuzzlePosition;
                    Check((muzzle - player.Transform.WorldPosition).Length < 18f, def.Id + " player muzzle follows the mount");
                }

                PutPlayer(arena);
                float distance = def.Attack == MechAttackType.Melee ? 1.4f : 9f;
                if (def.Mechanic == MechWeaponMechanic.RockRing) distance = def.Range - .45f;
                float secondDistance = def.Piercing ? distance + 3f : distance;
                float side = def.Piercing ? 0f : def.Mechanic == MechWeaponMechanic.Tesla ? 2f :
                    def.SplashRadius > 2f && def.Attack == MechAttackType.Splash ? 2.5f : 5f;
                PutTargets(distance, secondDistance, side);
                yield return .12f;
                Select(front);
                player.Health.Refill();
                if (def.Mechanic == MechWeaponMechanic.Scythe) player.Health.ApplyDamage(60f);
                float health = player.Health.Current;
                var weapon = part.Weapon;
                int reserve = weapon.RemainingAmmo;
                int loaded = weapon.MagazineRounds;
                float now = Time.ElapsedTime;
                bool fired = player.TryUseWeapon(weapon, now);
                Check(fired && !player.TryUseWeapon(weapon, now) && weapon.IsCoolingDown(now), def.Id + " fires and enforces cooldown");
                if (!fired) continue;
                yield return .06f;
                if (weapon.UsesAmmo)
                {
                    int cost = def.Mechanic == MechWeaponMechanic.Katyusha ? loaded : 1;
                    Check(weapon.RemainingAmmo == reserve - cost && weapon.AmmoText.Contains(" / "), def.Id + " ammo and HUD value");
                    if (def.Magazine == 1 || def.Mechanic == MechWeaponMechanic.Katyusha)
                        Check(weapon.IsReloading(now) && !weapon.IsReloading(now + def.Reload + .01f), def.Id + " automatic reload window");
                    else
                        Check(weapon.TryReload(now) && weapon.IsReloading(now), def.Id + " manual reload window");
                }

                if (def.Mechanic == MechWeaponMechanic.Shield)
                {
                    Check(!player.Health.ApplyDamage(25f) && player.Health.Current == health, "shield blocks an incoming hit");
                    yield return .65f;
                    Check(player.Health.ApplyDamage(25f), "shield stops blocking after its window");
                }
                else
                {
                    yield return def.Mechanic == MechWeaponMechanic.Hook ? .52f :
                        def.Mechanic == MechWeaponMechanic.RockRing ? .85f :
                        def.Mechanic == MechWeaponMechanic.Mortar ? 1.15f : .58f;
                    Check(frontDamage > 0f, def.Id + " damages real Zoo runtime target (" + frontDamage.ToString("F1") + ")");
                    if (def.Piercing)
                        Check(behindDamage > 0f && frontHits == 1, "large crossbow pierces and only hits the first target once");
                    if (def.Mechanic == MechWeaponMechanic.Tesla)
                        Check(behindDamage > 0f, "Tesla chains to a second target");
                    if (def.Mechanic == MechWeaponMechanic.Scythe)
                        Check(Math.Abs(player.Health.Current - health - def.Damage * .1f) < .1f, "Scythe heals 10% of the damage applied to CombatHealth (its rolled " + def.Damage.ToString("F1") + ")");
                    if (def.Mechanic == MechWeaponMechanic.PunchyArm)
                        Check(frontHits == 3, "Punchy Arm lands three timed hits (" + frontHits + ")");
                    if (def.Mechanic == MechWeaponMechanic.Hook)
                        Check((player.Transform.WorldPosition - arena).Length > 1f, "Hook pulls the character toward its hit");
                    if (def.Attack == MechAttackType.Splash && def.SplashRadius > 2f)
                        Check(behindDamage > 0f, def.Id + " blast damages a nearby target");
                }
                yield return .08f;
            }

            MechCombatTargets.Unregister(front);
            MechCombatTargets.Unregister(behind);
            front.SetActive(false);
            behind.SetActive(false);
            Select(null);
            player.Health.Refill();
            var enemies = new[] { "Sword Fodder", "Mace Fodder", "Throwing Axe Fodder", "Small Crossbow Fodder", "Heavy Cannon",
                "Heavy Crossbow", "Spinner Wind-up Test" };
            var ids = new[] { "sword", "mace", "throwing_axe", "small_crossbow", "cannon", "large_crossbow", "" };
            var pickupNames = new[] { "Part Sword", "Part Mace", "Part Throwing Axe", "Part Small Crossbow", "Part Cannon", "Part Large Crossbow", "" };
            var healthByType = new[] { 100f, 120f, 90f, 80f, 320f, 200f, 120f };
            var costByType = new[] { 1, 1, 1, 2, 10, 10, 2 };
            var distances = new[] { 2.0f, 4.5f, 3.4f, 7f, 14f, 19f, 5f };
            // The stationary player receives the Cannon's splash. Direct interception is checked separately.
            var damageByType = new[] { 15f, 12.5f, 15f, 20f, 27f, 19.6364f, 0f };
            var cannonSocket = Entity.Find("QA Cannon socket");
            for (int i = 0; i < enemies.Length; ++i)
            {
                PutPlayer(new Vector3(-86f, 1.5f, -80f));
                var pad = Entity.Find("Combat Test - " + enemies[i])?.GetComponent<ZooCombatSpawnPad>();
                Check(pad != null && pad.enemyPrefab != null, enemies[i] + " spawn pad resolves prefab");
                if (pad == null || pad.enemyPrefab == null) continue;
                spawnPad.Invoke(pad, null);
                Entity actor = (Entity)spawned.GetValue(pad);
                yield return .16f;
                var enemy = actor?.GetComponent<Enemy>();
                Check(enemy != null && enemy.IsReady && Math.Abs(enemy.MaximumHealth - healthByType[i]) < .1f &&
                    enemy.spawnCost == costByType[i] && enemy.equippedWeaponId == ids[i], enemies[i] + " live enemy data");
                if (enemy == null) continue;
                if (i == 3)
                {
                    Check(enemy.aimSpreadDegrees == 12f, "Small Crossbow retains inaccurate aim");
                    enemy.aimSpreadDegrees = 0f;
                }
                Vector3 site = arena + new Vector3(10f * (i % 4), 0f, -20f * (i / 4));
                actor.Transform.WorldPosition = site + new Vector3(0, -0.5f, 0);
                actor.Transform.WorldRotation = Quaternion.Identity;
                PutPlayer(site + new Vector3(0, 0, -distances[i]));
                player.Health.Refill();
                yield return .25f;
                var graph = actor.GetComponent<StateGraphAgent>();
                Check(graph != null && !string.IsNullOrEmpty(graph.CurrentState) && enemy.TargetDetected &&
                    MechCombatTargets.Same(enemy.Target, player.Entity), enemies[i] + " live graph acquires player (" + graph?.CurrentState + ")");
                var weapon = actor.FindChild("Weapon Model");
                bool followsTop = false;
                if (i < 4 && weapon != null && cannonSocket != null)
                {
                    var body = actor.FindChild("Body");
                    followsTop = body != null && weapon.Transform.Parent == body && cannonSocket.Transform.SetParent(body, false);
                    if (followsTop)
                    {
                        cannonSocket.Transform.Position = Vector3.Zero;
                        followsTop = (cannonSocket.Transform.WorldPosition - body.Transform.WorldPosition).Length > .1f;
                    }
                    cannonSocket.Transform.Position = weapon.Transform.Position;
                    cannonSocket.Transform.Rotation = weapon.Transform.Rotation;
                    cannonSocket.Transform.Scale = weapon.Transform.Scale;
                }
                if (i < 6)
                {
                    var muzzle = actor.FindChild("Weapon Muzzle");
                    Check(weapon != null && (i < 2 || muzzle != null), enemies[i] + " instantiates mounted weapon and applicable muzzle");
                    if (muzzle != null)
                    {
                        float span = (muzzle.Transform.WorldPosition - actor.Transform.WorldPosition).Length;
                        Check(span > .15f && span < 9f, enemies[i] + " muzzle transformed from GLB node (offset " + span.ToString("F2") + ")");
                    }
                }
                bool started = enemy.AttackInProgress;
                bool recovered = false, warning = false;
                float startHealth = player.Health.Maximum;
                float checkUntil = Time.ElapsedTime + (i == 6 ? 4.2f : 6.8f);
                while (Time.ElapsedTime < checkUntil)
                {
                    if (!actor.IsAlive) break;
                    if (followsTop)
                        followsTop = (weapon.Transform.WorldPosition - cannonSocket.Transform.WorldPosition).Length < .002f &&
                                     Vector3.Dot(weapon.Transform.Forward, cannonSocket.Transform.Forward) > .999f &&
                                     Vector3.Dot(weapon.Transform.Up, cannonSocket.Transform.Up) > .999f;
                    if (enemy.AttackInProgress) started = true;
                    if (started && !enemy.AttackInProgress) recovered = true;
                    var telegraph = actor.FindChild("Telegraph");
                    if (telegraph != null && telegraph.MeshRenderer.Visible) warning = true;
                    if (i < 6 && startHealth - player.Health.Current >= damageByType[i] - .06f && warning && started) break;
                    if (i == 6 && recovered && warning) break;
                    yield return .05f;
                }
                if (i < 4)
                {
                    Check(followsTop && started, enemies[i] + " follows the Cannon top socket through its attack");
                    if (cannonSocket != null) cannonSocket.Transform.SetParent(null, false);
                }
                Check(started && warning, enemies[i] + " graph starts a telegraphed attack");
                if (i == 6)
                    Check(recovered && Math.Abs(startHealth - player.Health.Current - enemy.chargeDamage) < .06f, "Spinner charges, damages once and recovers");
                else
                    Check(enemy.melee == (i < 2) && Math.Abs(startHealth - player.Health.Current - damageByType[i]) < .06f, enemies[i] + " attack damages player (" +
                        (startHealth - player.Health.Current).ToString("F1") + "; " + enemy.CurrentAction + "; distance " +
                        enemy.DistanceToTarget.ToString("F2") + ")");
                enemy.ApplyDamage(enemy.MaximumHealth + 1f);
                Check(enemy.IsDead, enemies[i] + " death uses shared health");
                yield return .18f;
                if (i < 6)
                {
                    MechPart drop = GetPart(pickupNames[i]);
                    Check(drop != null && drop.Def?.Id == ids[i], enemies[i] + " death drops the matching part");
                    if (drop != null)
                    {
                        MechSocket socket = drop.Def.Kind == MechPartKind.Arm ? MechSocket.ArmR : MechSocket.ShoulderR;
                        Check(player.Equip(drop, socket) && drop.IsMounted, enemies[i] + " dropped part equips on player");
                    }
                }
                if (i == 1) Check((enemy.Position - site).Length > .8f, "Mace approaches the player through existing navigation");
                yield return .14f;
            }
        }
    }
}

