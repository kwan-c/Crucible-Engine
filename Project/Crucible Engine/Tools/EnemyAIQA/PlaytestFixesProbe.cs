using System;
using System.Collections.Generic;
using System.Reflection;
using Crucible;

namespace Crucible.Scripts
{
    // Added only to an isolated copy of the Zoo by Run.ps1 (-Probe PlaytestFixesProbe). Checks the
    // manual-playtest fixes through the shipping scripts, physics, graphs and HUD. Raw device input
    // cannot be injected here: aim-mode changes call MechTargetLock directly, and the hard-lock look
    // offset is set through MechPlayer's own fields (what mouse movement would add).
    public sealed class PlaytestFixesProbe : EntityScript
    {
        MechPlayer player;
        MechHud hud;
        ZooPlayerRespawn respawn;
        CameraOrbit orbit;
        Entity aimA, aimB, aimC, slope, lowWall;
        IEnumerator<float> steps;
        float wait;
        int pass, fail;
        readonly Vector3 site = new Vector3(-55f, 1.2f, -46f);
        readonly List<Enemy> actors = new List<Enemy>();
        readonly string[] labels = { "Sword Fodder", "Mace Fodder", "Throwing Axe Fodder", "Small Crossbow Fodder",
                                     "Heavy Cannon", "Heavy Crossbow", "Spinner Wind-up Test" };
        const int Sword = 0, Mace = 1, Axe = 2, SmallCrossbow = 3, Cannon = 4, HeavyCrossbow = 5, Spinner = 6;
        static readonly BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;

        public override void OnCreate() { steps = Run(); }

        public override void OnUpdate(float dt)
        {
            if (steps == null || (wait -= dt) > 0f) return;
            try
            {
                if (steps.MoveNext()) { wait = steps.Current; return; }
            }
            catch (Exception ex) { Check(false, "probe exception " + ex); }
            steps = null;
            Log.Info("[PlaytestQA] " + pass + " passed; END failures=" + fail);
            System.IO.File.WriteAllText(Environment.GetEnvironmentVariable("CRUCIBLE_QA_RESULT"), fail.ToString());
        }

        void Check(bool ok, string message)
        {
            if (ok) pass++; else fail++;
            Log.Info("[PlaytestQA] " + (ok ? "PASS " : "FAIL ") + message);
        }

        void Note(string message) { Log.Info("[PlaytestQA] NOTE " + message); }

        static string F(float v) { return v.ToString("0.000"); }

        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> Run()
        {
            yield return 1.5f;
            player = Entity.Find("Player").GetComponent<MechPlayer>();
            hud = Entity.Find("Player HUD").GetComponent<MechHud>();
            Entity respawnEntity = Entity.Find("Zoo Player Respawn");
            respawn = respawnEntity != null ? respawnEntity.GetComponent<ZooPlayerRespawn>() : null;
            orbit = player.Entity.FindChild("Camera Pivot").CameraOrbit;
            aimA = Entity.Find("QA aim A"); aimB = Entity.Find("QA aim B"); aimC = Entity.Find("QA aim C");
            slope = Entity.Find("QA slope"); lowWall = Entity.Find("QA low wall");
            foreach (Entity e in new[] { aimA, aimB, aimC })
            {
                CombatHealth h = e.GetComponent<CombatHealth>();
                h.SetMaximum(100000f, true);
                MechCombatTargets.Register(e, (amount, from) => h.ApplyDamage(amount), 0f);
            }
            Check(player != null && hud != null && respawn != null && orbit != null, "Zoo player, HUD, camera rig and the Zoo Player Respawn entity exist");
            player.hitStop = false;

            var sections = new Func<IEnumerator<float>>[] { AimModes, HealthBar, Respawn, MaceTiming, AxeRange,
                                                            RetreatAttacks, HeavyCrossbowBacksOff, AxeCircles, HitShakes, LootRoll,
                                                            CannonGround, Spinners, Toggles };
            foreach (var section in sections)
            {
                Note("---- " + section.Method.Name + " ----");
                IEnumerator<float> it = section();
                while (true)
                {
                    bool more;
                    try { more = it.MoveNext(); }
                    catch (Exception ex) { Check(false, section.Method.Name + " threw " + ex); break; }
                    if (!more) break;
                    yield return it.Current;
                }
                Cleanup();
                yield return .3f;
            }
        }

        void Cleanup()
        {
            foreach (var e in actors) if (e != null && e.Entity.IsAlive) Destroy(e.Entity);
            actors.Clear();
            foreach (Entity e in new[] { aimA, aimB, aimC }) Park(e);
            Park(lowWall);
            if (player.IsDead) player.Health.Refill();
            player.Health.SetMaximum(100000f, true);
            if (player.Targeting.Mode != MechAimMode.SoftLock) player.Targeting.SetMode(MechAimMode.SoftLock, Camera.Main);
        }

        void Park(Entity e) { if (e != null) { e.SetActive(true); e.Transform.WorldPosition = new Vector3(-120f, -20f - e.Index * .01f, -120f); } }

        void PutPlayer(Vector3 p) { player.Transform.WorldPosition = p; }

        Enemy Track(Entity entity)
        {
            Enemy e = entity.GetComponent<Enemy>();
            e.dropChancePercent = 0;
            e.Wake();
            actors.Add(e);
            return e;
        }

        Entity SpawnAt(int label, Vector3 at)
        {
            return Entity.Find("Combat Test - " + labels[label]).GetComponent<ZooCombatSpawnPad>().enemyPrefab
                .Instantiate(at + Vector3.Up * .4f, Quaternion.Identity);
        }

        IEnumerable<float> Until(Func<bool> predicate, float seconds)
        {
            float end = Time.ElapsedTime + seconds;
            while (!predicate() && Time.ElapsedTime < end) yield return 0f;
        }

        static float Flat(Vector3 v) { return (float)Math.Sqrt(v.X * v.X + v.Z * v.Z); }

        Vector3 CamPoint(float distance, float yawDegrees = 0f)
        {
            Transform view = Camera.Main.Entity.Transform;
            Vector3 forward = Quaternion.AngleAxis(yawDegrees, Vector3.Up) * view.Forward;
            return view.WorldPosition + forward * distance;
        }

        static float YawTo(Vector3 from, Vector3 to)
        {
            Vector3 d = to - from;
            return (float)(Math.Atan2(-d.X, -d.Z) * 180.0 / Math.PI);
        }

        static float AngleBetween(float a, float b) { return Math.Abs(((a - b) % 360f + 540f) % 360f - 180f); }

        // ------------------------------------------------------------------------------------ //
        // 3. Aim modes and target switching
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> AimModes()
        {
            MechTargetLock T = player.Targeting;
            Check(T.Mode == MechAimMode.SoftLock, "starts in Soft Lock, as Unity's controller (startAimMode 1)");
            PutPlayer(site + new Vector3(0f, 1f, 0f));
            orbit.Yaw = 0f; orbit.Pitch = 8f;
            yield return .5f;
            float sensitivity = orbit.Sensitivity;

            aimA.Transform.WorldPosition = CamPoint(12f);
            yield return .25f;
            Check(T.CurrentTarget != null && T.CurrentTarget.Entity == aimA, "soft lock: the enemy under the hidden centre cursor is selected");
            Check(orbit.Sensitivity > 0f, "soft lock: the camera stays free (orbit sensitivity " + F(orbit.Sensitivity) + ")");

            T.SetMode(MechAimMode.FreeAim, Camera.Main);
            yield return .3f;
            Check(T.CurrentTarget == null && T.IsFreeAim, "free aim: no automatic lock with an enemy under the cursor");
            Check(Math.Abs(orbit.Sensitivity - sensitivity) < 1e-4f, "free aim: camera not following an enemy (free orbit)");

            T.SetMode(MechAimMode.SoftLock, Camera.Main);
            yield return .1f;
            Check(T.CurrentTarget != null && T.CurrentTarget.Entity == aimA, "re-entering soft lock picks the enemy under the cursor at once");

            aimB.Transform.WorldPosition = CamPoint(12f, 8f);
            yield return .3f;
            Check(T.CurrentTarget.Entity == aimA, "soft lock keeps its target while another enemy is merely on screen");

            // Switch: the hidden cursor moves onto B (here the enemies move; in play the mouse turns the camera).
            int changes = T.TargetChanges;
            aimA.Transform.WorldPosition = CamPoint(12f, -8f);
            aimB.Transform.WorldPosition = CamPoint(12f);
            float start = Time.ElapsedTime;
            yield return 0f;
            bool earlySwitch = T.CurrentTarget.Entity == aimB;
            foreach (var t in Until(() => T.CurrentTarget.Entity == aimB, 1f)) yield return t;
            float took = Time.ElapsedTime - start;
            Check(!earlySwitch && T.CurrentTarget.Entity == aimB && took >= player.targetSwitchDwell - .04f && took < .4f,
                  "soft switch to the enemy under the cursor after the dwell time (" + F(took) + " s)");
            Check(T.TargetChanges == changes + 1, "one switch, no intermediate flicker");

            // Two enemies overlapping under the cursor: the current one is kept, frame after frame.
            changes = T.TargetChanges;
            for (int i = 0; i < 60; i++)
            {
                float jitter = (i % 2 == 0 ? .15f : -.15f);
                aimA.Transform.WorldPosition = CamPoint(11f) + Camera.Main.Entity.Transform.Right * jitter;
                aimB.Transform.WorldPosition = CamPoint(13.5f) - Camera.Main.Entity.Transform.Right * jitter;
                yield return 0f;
            }
            Check(T.TargetChanges == changes && T.CurrentTarget.Entity == aimB, "overlapping enemies under the cursor: no flicker (" + (T.TargetChanges - changes) + " changes)");

            aimA.Transform.WorldPosition = CamPoint(12f, -10f);
            aimB.Transform.WorldPosition = CamPoint(12f, 10f);
            yield return .5f;
            Check(T.CurrentTarget.Entity == aimB, "cursor over empty space keeps the current target");

            // Different distances.
            aimA.Transform.WorldPosition = CamPoint(7f, -5f);
            aimB.Transform.WorldPosition = CamPoint(26f, 6f);
            yield return .2f;
            aimA.Transform.WorldPosition = CamPoint(7f);
            foreach (var t in Until(() => T.CurrentTarget.Entity == aimA, 1f)) yield return t;
            aimA.Transform.WorldPosition = CamPoint(7f, -6f);
            aimB.Transform.WorldPosition = CamPoint(26f);
            foreach (var t in Until(() => T.CurrentTarget.Entity == aimB, 1f)) yield return t;
            Check(T.CurrentTarget.Entity == aimB, "switches between a near (7 m) and a far (26 m) enemy");

            // Leaving range.
            aimB.Transform.WorldPosition = CamPoint(75f);
            yield return .15f;
            Check(T.CurrentTarget == null || T.CurrentTarget.Entity != aimB, "a target beyond lock range (75 m > 60 m) is dropped");

            // Death / destruction: the lock never stays on an inactive or unregistered entity.
            aimB.Transform.WorldPosition = CamPoint(12f);
            aimA.Transform.WorldPosition = CamPoint(12f, -12f);
            foreach (var t in Until(() => T.CurrentTarget != null && T.CurrentTarget.Entity == aimB, 1f)) yield return t;
            aimB.SetActive(false);
            yield return .1f;
            Check(T.CurrentTarget == null || T.CurrentTarget.Entity != aimB, "a dead (switched-off) target is dropped at once");
            aimB.SetActive(true);
            MechCombatTargets.Unregister(aimA);
            yield return .1f;
            Check(T.CurrentTarget == null || T.CurrentTarget.Entity != aimA, "an unregistered (destroyed) target is never kept");
            CombatHealth ha = aimA.GetComponent<CombatHealth>();
            MechCombatTargets.Register(aimA, (amount, from) => ha.ApplyDamage(amount), 0f);
            bool playerSelected = false;
            for (int i = 0; i < 20; i++) { if (T.CurrentTarget != null && MechCombatTargets.Same(T.CurrentTarget.Entity, player.Entity)) playerSelected = true; yield return 0f; }
            Check(!playerSelected, "the player itself is never selected");

            // Hard lock: camera tracks the target.
            // A near the orbit pivot can still obscure B after a yaw change. Use a downrange
            // target and verify the separation on screen before testing the dwell.
            aimA.Transform.WorldPosition = CamPoint(30f);
            aimB.Transform.WorldPosition = new Vector3(-120f, -20f, -120f);
            foreach (var t in Until(() => T.CurrentTarget != null && T.CurrentTarget.Entity == aimA, 1f)) yield return t;
            T.SetMode(MechAimMode.HardLock, Camera.Main);
            yield return 1.2f;
            float yawError = AngleBetween(orbit.Yaw, YawTo(player.Transform.WorldPosition, aimA.Transform.WorldPosition));
            Check(T.IsHardLocked && T.CurrentTarget.Entity == aimA, "hard lock keeps the visible selection");
            Check(yawError < 4f && orbit.Sensitivity == 0f, "hard lock: camera tracks the target (yaw error " + F(yawError) + " deg), free orbit off");

            // Hard-lock switch: the look offset (what mouse movement adds) turns the view away from the
            // tracked target; the same hidden cursor then rests on the other enemy.
            FieldInfo lookYaw = typeof(MechPlayer).GetField("lookYaw", Private), lookIdle = typeof(MechPlayer).GetField("lookIdle", Private);
            Check(lookYaw != null && lookIdle != null, "hard-lock look offset fields found");
            for (float t = 0f; t < .7f; t += Time.DeltaTime) { lookYaw.SetValue(player, 22f); lookIdle.SetValue(player, 0f); yield return 0f; }
            Check(T.CurrentTarget.Entity == aimA, "looking away alone does not drop or switch the hard-lock target");
            Ray centreRay;
            bool hasRay = Camera.Main.ViewportPointToRay(new Vector2(.5f, .5f), out centreRay);
            aimB.Transform.WorldPosition = centreRay.Origin + centreRay.Direction * 14f;
            Check(hasRay && MechCombatTargets.Owning(aimB) != null, "second hard-lock candidate is registered and placed on the rendered centre ray");
            Vector2 currentPixels; float currentDepth;
            Camera.Main.WorldToScreenPoint(aimA.Transform.WorldPosition, out currentPixels, out currentDepth);
            Vector2 viewSize = Camera.ScreenSize;
            float cursorRadius = T.TargetSwitchRadius * Math.Min(viewSize.X, viewSize.Y);
            Check((currentPixels - viewSize * .5f).Length > cursorRadius, "look clears the first target's cursor region before the switch");
            changes = T.TargetChanges;
            float hold = Time.ElapsedTime;
            while (Time.ElapsedTime - hold < .5f && T.CurrentTarget.Entity != aimB) { lookYaw.SetValue(player, 22f); lookIdle.SetValue(player, 0f); yield return 0f; }
            Vector2 pixels; float depth;
            Camera.Main.WorldToScreenPoint(aimB.Transform.WorldPosition, out pixels, out depth);
            Note("hard-lock B screen offset " + (pixels - Camera.ScreenSize*.5f) + ", depth " + F(depth) + ", cursor " + (T.CursorTarget == null ? "none" : T.CursorTarget.Entity.Name) + ", owner " + player.Transform.WorldPosition + ", valid " + T.IsTargetInRange(MechCombatTargets.Owning(aimB)));
            Check(T.CurrentTarget.Entity == aimB && T.TargetChanges == changes + 1, "hard lock switches to the enemy the player looks at (same cursor, dwell " + F(Time.ElapsedTime - hold) + " s)");
            float maxStep = 0f, previous = orbit.Yaw;
            for (float t = 0f; t < 1.6f; t += Time.DeltaTime)
            {
                yield return 0f;
                maxStep = Math.Max(maxStep, AngleBetween(orbit.Yaw, previous));
                previous = orbit.Yaw;
            }
            yawError = AngleBetween(orbit.Yaw, YawTo(player.Transform.WorldPosition, aimB.Transform.WorldPosition));
            Check(T.CurrentTarget.Entity == aimB && yawError < 4f, "after the switch the camera follows the new target (yaw error " + F(yawError) + " deg)");
            Check(maxStep < 12f, "no camera snap on the switch (largest per-frame yaw change " + F(maxStep) + " deg)");

            aimB.SetActive(false);
            yield return .3f;
            Check(T.CurrentTarget != null && T.CurrentTarget.Entity == aimA, "hard lock: target death recovers the nearest visible enemy");
            aimB.SetActive(true);

            T.SetMode(MechAimMode.FreeAim, Camera.Main);
            yield return .3f;
            Check(T.IsFreeAim && T.CurrentTarget == null && Math.Abs(orbit.Sensitivity - sensitivity) < 1e-4f, "back to Free Aim: no target, free camera restored");

            T.ToggleHardLock(Camera.Main);
            Check(T.Mode == MechAimMode.HardLock, "ToggleHardLock still enters hard lock (compatibility)");
            T.SetMode(MechAimMode.SoftLock, Camera.Main);

            MechInputClaims.Claim(this, KeyCode.G);
            bool claimed = MechInputClaims.IsClaimed(KeyCode.G);
            MechInputClaims.Release(this, KeyCode.G);
            Check(claimed && !MechInputClaims.IsClaimed(KeyCode.G), "a pad claims its key while occupied and releases it");

            // The Zoo pads interact on G, never F (F is the aim / lock-on mode key).
            FieldInfo padField = typeof(ZooWeaponDropPad).GetField("pad", Private);
            FieldInfo keyField = typeof(ZooPad).GetField("key", Private);
            Entity dropPad = Entity.Find("Weapon Drop Pad");
            object zooPad = dropPad != null ? padField.GetValue(dropPad.GetComponent<ZooWeaponDropPad>()) : null;
            Check(zooPad != null && (KeyCode)keyField.GetValue(zooPad) == KeyCode.G
                  && (KeyCode)typeof(ZooPad).GetConstructors()[0].GetParameters()[1].DefaultValue == KeyCode.G,
                  "Zoo pads use G by default, so F only cycles the aim mode");
        }

        // ------------------------------------------------------------------------------------ //
        // 5. Delayed-damage health bar
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> HealthBar()
        {
            MechTrailingBar bar = hud.TrailingBar;
            MechHealth health = player.Health;
            PutPlayer(site + new Vector3(0f, 1f, 0f));
            health.SetMaximum(1000f, true);
            yield return .1f;
            Check(Math.Abs(bar.Front - 1f) < 1e-3f && Math.Abs(bar.Trailing - 1f) < 1e-3f, "full health: both bars full");
            Check(Math.Abs(hud.trailingBarDelay - 1f) < 1e-4f && Math.Abs(hud.trailingBarDrainTime - .65f) < 1e-4f, "Unity's white-bar timing (1 s delay, 0.65 s drain)");

            health.ApplyDamage(100f);
            yield return 0f;
            yield return 0f;
            Check(Math.Abs(bar.Front - .9f) < 1e-3f && Math.Abs(bar.Trailing - 1f) < 1e-3f && health.Current == 900f,
                  "damage: real health and the front bar drop at once; the trailing bar holds (" + F(bar.Front) + " / " + F(bar.Trailing) + ")");
            yield return .5f;
            Check(Math.Abs(bar.Trailing - 1f) < 1e-3f, "trailing bar still held 0.5 s later");
            health.ApplyDamage(100f);
            yield return .8f;
            Check(Math.Abs(bar.Trailing - 1f) < 1e-3f && Math.Abs(bar.Front - .8f) < 1e-3f, "more damage restarts the delay (still held 1.3 s after the first hit)");
            yield return .5f;
            Check(bar.Trailing < 1f - 1e-3f && bar.Trailing > .8f + 1e-3f, "then it drains toward the real health (" + F(bar.Trailing) + ")");
            yield return .6f;
            Check(Math.Abs(bar.Trailing - .8f) < 1e-3f, "and catches up after delay + drain");
            Check(health.Current == 800f, "the animation never changes gameplay health (" + health.Current + ")");

            health.ApplyDamage(1f);
            yield return 0f; yield return 0f;
            Check(bar.Trailing - bar.Front > .0009f, "small damage (1 HP) shows a trailing segment");
            health.ApplyDamage(200f);
            yield return 0f; yield return 0f;
            health.Heal(50f);
            yield return 0f; yield return 0f;
            Check(Math.Abs(bar.Trailing - bar.Front) < 1e-4f, "healing snaps the trailing bar to the new health");
            health.ApplyDamage(500f);
            yield return 0f; yield return 0f;
            Check(bar.Trailing - bar.Front > .49f, "large damage (500 HP) holds the whole lost chunk");
            health.SetMaximum(1500f, false);
            yield return 0f; yield return 0f;
            Check(Math.Abs(bar.Trailing - bar.Front) < 1e-4f, "a maximum-health change (equipping a part) resets the trailing bar");
            health.SetMaximum(100000f, true);
        }

        // ------------------------------------------------------------------------------------ //
        // 4. Zoo respawn
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> Respawn()
        {
            respawn.respawnDelay = 1.2f;
            PutPlayer(site + new Vector3(0f, 1f, 0f));
            player.Health.SetMaximum(100000f, true);
            Entity spawned = SpawnAt(Mace, site + new Vector3(0f, 0f, -5f));
            yield return .15f;
            Enemy mace = Track(spawned);
            foreach (var t in Until(() => mace.AttackInProgress, 8f)) yield return t;
            Check(mace.AttackInProgress, "an enemy is attacking the player before death");

            MechPartDef[] before = new MechPartDef[7];
            for (int s = 0; s < 7; s++) before[s] = player.EquippedPart((MechSocket)s);
            player.ApplyKnockback(new Vector3(1f, 0f, 0f), 10f, .6f);
            float maximum = player.Health.Maximum;
            int respawns = player.RespawnCount;
            player.Health.ApplyDamage(1e9f);
            yield return 0f; yield return 0f;
            Check(player.IsDead, "the player dies");
            yield return .15f;
            Check(!mace.AttackInProgress, "the pending enemy attack on the dead player is cancelled");
            Check(!mace.TargetDetected, "enemies stop sensing a dead player (no reference kept to attack)");
            Check(Math.Abs(hud.TrailingBar.Front) < 1e-4f && Math.Abs(hud.TrailingBar.Trailing) < 1e-4f, "HUD: death resets the trailing bar");
            float died = Time.ElapsedTime;
            foreach (var t in Until(() => !player.IsDead, 4f)) yield return t;
            float delay = Time.ElapsedTime - died;
            Check(!player.IsDead && player.RespawnCount == respawns + 1 && Math.Abs(delay - 1.2f) < .3f,
                  "respawns after the configured delay (" + F(delay) + " s of 1.2)");
            Check(player.Health.Current == player.Health.Maximum && player.Health.Maximum > 0f,
                  "full health from the loadout's own maximum (" + player.Health.Current + " / " + player.Health.Maximum + ")");
            float fromHorde = Flat(player.Transform.WorldPosition - mace.Position);
            Check(fromHorde >= respawn.safeEnemyDistance, "spawned away from the horde (" + F(fromHorde) + " m from the nearest enemy)");
            bool same = true;
            for (int s = 0; s < 7; s++) same &= player.EquippedPart((MechSocket)s) == before[s];
            Check(same, "current loadout kept (Zoo testing workflow)");
            bool rearmed = true;
            float now = Time.ElapsedTime;
            foreach (MechWeapon w in player.Weapons) if (w != null && (w.IsCoolingDown(now) || w.IsReloading(now))) rearmed = false;
            Check(rearmed, "weapon cooldowns and reloads cleared");
            float knock = (float)typeof(MechPlayer).GetField("externalRemaining", Private).GetValue(player);
            Check(knock <= 0f, "knockback cleared");
            Check(player.Targeting.CurrentTarget == null && player.Targeting.Mode == MechAimMode.SoftLock, "target lock cleared, aim mode kept");
            Check(!MechHitStop.IsFrozen(player.Entity), "no hit stop carried over");
            yield return .6f;
            Check(player.IsGrounded && player.CurrentLegPose == MechPlayer.LegPose.Neutral, "movement state and legs animation restored (grounded, Neutral)");
            Check(Math.Abs(hud.TrailingBar.Front - 1f) < 1e-4f && Math.Abs(hud.TrailingBar.Trailing - 1f) < 1e-4f, "HUD health bar full again");
            bool visible = (bool)typeof(MechHud).GetField("healthVisible", Private).GetValue(hud);
            Check(visible, "HUD health meter visible again");
            respawn.respawnDelay = 3f;
        }

        // ------------------------------------------------------------------------------------ //
        // 9. Mace (and Sword) contact timing; 8. attack sound scheduling
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> MaceTiming()
        {
            foreach (int label in new[] { Mace, Sword })
            {
                PutPlayer(site + new Vector3(0f, 1f, 0f));
                player.Health.SetMaximum(100000f, true);
                Entity spawned = SpawnAt(label, site + new Vector3(0f, 0f, -6f));
                yield return .15f;
                Enemy e = Track(spawned);
                Note(labels[label] + ": release " + F(e.ReleaseTime) + " s, weapon swing at " + F(e.WeaponSwingTime) + " s, contact " + F(e.weaponContactSeconds) + " s into its clip");
                float expectedSwing = label == Mace ? .42f - .29f : .42f - .25f;
                Check(Math.Abs(e.WeaponSwingTime - expectedSwing) < .005f && Math.Abs(e.ReleaseTime - .42f) < .005f,
                      labels[label] + ": weapon swing scheduled so its contact frame meets the 0.42 s release");
                foreach (var t in Until(() => e.AttackInProgress, 10f)) yield return t;
                float t0 = Time.ElapsedTime - e.AttackAge, swing = -1f, hit = -1f, hp = player.Health.Current;
                int damageEvents = 0, sounds = e.AttackSoundsPlayed;
                while (e.AttackInProgress)
                {
                    if (swing < 0f && e.WeaponSwingStarted) swing = Time.ElapsedTime - t0;
                    if (player.Health.Current < hp) { damageEvents++; if (hit < 0f) hit = Time.ElapsedTime - t0; hp = player.Health.Current; }
                    yield return 0f;
                }
                yield return .1f;
                if (player.Health.Current < hp) damageEvents++;
                Check(swing >= 0f && Math.Abs(swing - expectedSwing) < .05f, labels[label] + ": weapon swing starts at " + F(swing) + " s");
                Check(hit >= 0f && Math.Abs(hit - .42f) < .05f && damageEvents == 1, labels[label] + ": one hit, at the contact frame (" + F(hit) + " s, " + damageEvents + " hit)");
                Check(hit - swing > .2f, labels[label] + ": the hit lands mid-swing (" + F(hit - swing) + " s into the weapon clip of 0.375 s)");
                Check(e.AttackSoundsPlayed == sounds + 1, labels[label] + ": one attack sound per attack");

                if (label != Mace) continue;
                // Dodge before contact: step out during the wind-up.
                foreach (var t in Until(() => e.AttackInProgress && e.AttackAge > .15f, 6f)) yield return t;
                hp = player.Health.Current;
                PutPlayer(e.Position + e.Facing * -1f * 9f + Vector3.Up);
                foreach (var t in Until(() => !e.AttackInProgress, 2f)) yield return t;
                Check(player.Health.Current == hp, "Mace: stepping out before contact dodges the hit");
                // Death before contact: no damage, the pending sound is cut.
                PutPlayer(site + new Vector3(0f, 1f, 0f));
                foreach (var t in Until(() => e.AttackInProgress && e.AttackAge > .15f && e.AttackAge < .35f, 8f)) yield return t;
                hp = player.Health.Current;
                int cancelled = e.AttackSoundsCancelled;
                bool pending = e.AttackSoundPending;
                e.ApplyDamage(1e6f);
                yield return .5f;
                Check(player.Health.Current == hp, "Mace: killed before contact deals no damage");
                Check(!pending || e.AttackSoundsCancelled == cancelled + 1, "Mace: its pending attack sound is cut when it dies (pending=" + pending + ")");
            }
        }

        // ------------------------------------------------------------------------------------ //
        // 10. Throwing Axe close range
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> AxeRange()
        {
            PutPlayer(site + new Vector3(0f, 1f, 0f));
            Entity spawned = SpawnAt(Axe, site + new Vector3(0f, 0f, -18f));
            yield return .15f;
            Enemy e = Track(spawned);
            Check(Math.Abs(e.minimumRange - 2.5f) < 1e-4f && Math.Abs(e.attackRange - 7f) < 1e-4f && Math.Abs(e.preferredRange - 5f) < 1e-4f
                  && Math.Abs(e.arriveRadius - 10f) < 1e-4f, "Throwing Axe bands: retreat 2.5 m, prefer 5 m, admit 7 m, arrive 10 m");
            Check(Math.Abs(e.weaponReach - 32f) < 1e-4f && e.IsRanged, "the axe stays ranged and keeps its 32 m projectile travel");
            int releases = e.Releases;
            float distanceAtRelease = -1f;
            float end = Time.ElapsedTime + 14f;
            while (Time.ElapsedTime < end && distanceAtRelease < 0f)
            {
                if (e.Releases > releases) distanceAtRelease = e.DistanceToTarget;
                yield return 0f;
            }
            Check(distanceAtRelease > 0f && distanceAtRelease <= 7.75f, "first throw from close range (" + F(distanceAtRelease) + " m)");
        }

        // ------------------------------------------------------------------------------------ //
        // 11. Ranged enemies attack while retreating
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> RetreatAttacks()
        {
            // The Heavy Crossbow is the exception now (retreatInsteadOfFiring): see HeavyCrossbowBacksOff.
            foreach (int label in new[] { Axe, SmallCrossbow, Cannon })
            {
                for (int planner = 1; planner >= (label == SmallCrossbow ? 0 : 1); planner--)
                {
                    player.Health.SetMaximum(100000f, true);
                    PutPlayer(site + new Vector3(0f, 1f, 0f));
                    Entity spawned = SpawnAt(label, site + new Vector3(0f, 0f, -14f));
                    yield return .15f;
                    Enemy e = Track(spawned);
                    e.useRangedPositioning = planner == 1;
                    foreach (var t in Until(() => e.TargetDetected, 4f)) yield return t;
                    // The player keeps pressing in: always inside the enemy's minimum range.
                    float keep = Math.Max(Math.Max(e.minimumAttackDistance + 1f, e.minimumRange * .6f), 2.2f);
                    int releases = e.Releases, inside = 0;
                    float retreated = 0f, end = Time.ElapsedTime + 12f;
                    Vector3 start = e.Position;
                    while (Time.ElapsedTime < end && inside < 1)
                    {
                        Vector3 away = player.Transform.WorldPosition - e.Position;
                        away = new Vector3(away.X, 0f, away.Z);
                        away = away.Length > .1f ? away.Normalized : new Vector3(0f, 0f, 1f);
                        if (!e.AttackInProgress) PutPlayer(new Vector3(e.Position.X, site.Y + 1f, e.Position.Z) + away * keep);
                        retreated = Math.Max(retreated, Flat(e.Position - start));
                        if (e.Releases > releases)
                        {
                            if (e.DistanceToTarget < e.minimumRange) inside++;
                            releases = e.Releases;
                        }
                        yield return 0f;
                    }
                    Check(inside >= 1, labels[label] + (planner == 1 ? "" : " (ranged positioning off)") + ": attacks while inside its "
                          + F(e.minimumRange) + " m minimum range and retreating (moved " + F(retreated) + " m)");
                    Destroy(e.Entity);
                    actors.Remove(e);
                    yield return .2f;
                }
            }
        }

        // ------------------------------------------------------------------------------------ //
        // 11b. Heavy Crossbow holds fire and backs off slowly; shoots again once clear
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> HeavyCrossbowBacksOff()
        {
            player.Health.SetMaximum(100000f, true);
            PutPlayer(site + new Vector3(0f, 1f, 0f));
            Entity spawned = SpawnAt(HeavyCrossbow, site + new Vector3(0f, 0f, -8f));
            yield return .15f;
            Enemy e = Track(spawned);
            Check(e.retreatInsteadOfFiring && !e.strafeWhileAttacking, "Heavy Crossbow prefab: retreatInsteadOfFiring on");
            foreach (var t in Until(() => e.TargetDetected, 4f)) yield return t;

            // 1. The player stands still 8 m away (inside the 12 m minimum): no shots, it walks away slowly.
            int releases = e.Releases, insideShots = 0, backingFrames = 0, frames = 0;
            float fastest = 0f, startDistance = e.DistanceToTarget;
            Vector3 last = e.Position;
            float end = Time.ElapsedTime + 4f;
            while (Time.ElapsedTime < end)
            {
                if (e.Releases > releases) { if (e.DistanceToTarget < e.minimumRange && e.CurrentAction != "Cornered - firing") insideShots++; releases = e.Releases; }
                if (e.IsBackingOff) backingFrames++;
                frames++;
                if (Time.DeltaTime > 0f) fastest = Math.Max(fastest, Flat(e.Position - last) / Time.DeltaTime);
                last = e.Position;
                yield return 0f;
            }
            Note("Heavy Crossbow: from " + F(startDistance) + " m to " + F(e.DistanceToTarget) + " m in 4 s, top speed " + F(fastest)
                 + " m/s, action '" + e.CurrentAction + "'");
            Check(insideShots == 0, "Heavy Crossbow: no shot while inside its minimum range (" + insideShots + ")");
            Check(backingFrames > frames / 2 && e.DistanceToTarget > startDistance + 2f, "Heavy Crossbow: backs off from the player ("
                  + F(startDistance) + " -> " + F(e.DistanceToTarget) + " m)");

            // 2. Once clear (minimumRange + retreatClearance), it shoots again - standing still to do it.
            int before = e.Releases;
            bool movedDuringWindup = false;
            Vector3? attackStart = null;
            float windupSlide = 0f;
            end = Time.ElapsedTime + 12f;
            last = e.Position;
            while (Time.ElapsedTime < end && e.Releases == before)
            {
                if (e.AttackInProgress) { if (attackStart == null) attackStart = e.Position; float slid = Flat(e.Position - attackStart.Value); windupSlide = Math.Max(windupSlide, slid); if (e.AttackAge > .2f && Flat(e.Position - last) > .005f) movedDuringWindup = true; }
                last = e.Position;
                yield return 0f;
            }
            Check(e.Releases > before && e.DistanceToTarget >= e.minimumRange, "Heavy Crossbow: fires again once clear ("
                  + F(e.DistanceToTarget) + " m)");
            Check(!movedDuringWindup && windupSlide < .3f, "Heavy Crossbow: stands still while it attacks (slid " + F(windupSlide) + " m from the attack start)");
            Destroy(e.Entity);
            actors.Remove(e);
            yield return .2f;
        }

        // ------------------------------------------------------------------------------------ //
        // 11c. Throwing Axe circles the player and throws on the move
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> AxeCircles()
        {
            player.Health.SetMaximum(100000f, true);
            Vector3 centre = site + new Vector3(0f, 1f, 0f);
            PutPlayer(centre);
            Entity spawned = SpawnAt(Axe, site + new Vector3(0f, 0f, -5f));
            yield return .15f;
            Enemy e = Track(spawned);
            Check(e.strafeWhileAttacking && !e.retreatInsteadOfFiring, "Throwing Axe prefab: strafeWhileAttacking on");
            foreach (var t in Until(() => e.TargetDetected, 4f)) yield return t;

            int releases = e.Releases, attackFrames = 0, movingAttackFrames = 0;
            float swept = 0f, standing = 0f;
            Vector3 last = e.Position;
            double lastAngle = Math.Atan2(e.Position.Z - centre.Z, e.Position.X - centre.X);
            float end = Time.ElapsedTime + 8f;
            while (Time.ElapsedTime < end)
            {
                PutPlayer(centre);
                float step = Flat(e.Position - last);
                if (e.AttackInProgress) { attackFrames++; if (Time.DeltaTime > 0f && step / Time.DeltaTime > .3f) movingAttackFrames++; }
                if (Time.DeltaTime > 0f && step / Time.DeltaTime < .1f) standing += Time.DeltaTime;
                double angle = Math.Atan2(e.Position.Z - centre.Z, e.Position.X - centre.X);
                double d = angle - lastAngle;
                while (d > Math.PI) d -= 2 * Math.PI;
                while (d < -Math.PI) d += 2 * Math.PI;
                swept += (float)Math.Abs(d);
                lastAngle = angle;
                last = e.Position;
                yield return 0f;
            }
            int throws = e.Releases - releases;
            float degrees = swept * 180f / (float)Math.PI;
            Note("Throwing Axe: " + throws + " throws, swept " + F(degrees) + " deg round the player, stood still " + F(standing)
                 + " s of 8, moving in " + movingAttackFrames + "/" + attackFrames + " attack frames, at " + F(e.DistanceToTarget) + " m");
            Check(throws >= 3, "Throwing Axe: keeps throwing while it circles (" + throws + " in 8 s)");
            Check(degrees >= 90f, "Throwing Axe: circles the player (" + F(degrees) + " degrees)");
            Check(attackFrames > 0 && movingAttackFrames > attackFrames / 2, "Throwing Axe: moves while it throws ("
                  + movingAttackFrames + "/" + attackFrames + " frames)");
            Check(standing < 3f, "Throwing Axe: rarely stands still (" + F(standing) + " s of 8)");
            Destroy(e.Entity);
            actors.Remove(e);
            yield return .2f;
        }

        // ------------------------------------------------------------------------------------ //
        // 11e. Camera shake on confirmed hits and on the mech's own explosions
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> HitShakes()
        {
            FieldInfo amplitude = typeof(MechPlayer).GetField("shakeAmplitude", Private);
            FieldInfo time = typeof(MechPlayer).GetField("shakeTime", Private);
            Func<float> left = () => (float)time.GetValue(player) > 0f ? (float)amplitude.GetValue(player) : 0f;
            PutPlayer(site + new Vector3(0f, 1f, 0f));
            yield return .4f;                                  // any earlier shake has faded
            Check(left() == 0f, "no shake running before the test");
            player.ConfirmHit(aimA, MechHitKind.Projectile);
            float hit = left();
            Check(Math.Abs(hit - player.hitShake * player.unityScale) < 1e-4f, "a confirmed hit shakes the camera (" + F(hit) + ")");
            yield return .4f;
            player.ConfirmExplosion(player.Transform.WorldPosition + new Vector3(60f, 0f, 0f), 0);
            Check(left() == 0f, "an explosion beyond " + F(player.explosionShakeRange) + " m does not shake");
            player.ConfirmExplosion(player.Transform.WorldPosition + new Vector3(4.5f, 0f, 0f), 3);
            float blast = left(), expected = Math.Min(.2f, player.explosionShake * 1.4f) * .9f * player.unityScale;
            Check(Math.Abs(blast - expected) < 1e-3f, "a close 3-kill explosion shakes harder, by distance (" + F(blast) + " of " + F(expected) + ")");
        }

        // ------------------------------------------------------------------------------------ //
        // 11d. Loot stat roll and the Zoo weapon drop pad
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> LootRoll()
        {
            MechPartDef axe = MechPartCatalogue.Find("throwing_axe");
            MechPartDef a = MechPartStatRoll.Create(axe, 1234), b = MechPartStatRoll.Create(axe, 1234);
            Check(a.Damage == b.Damage && a.FireRate == b.FireRate && a.RangeGolems == b.RangeGolems, "same seed, same roll");
            bool inBounds = true, changed = false, magazineKept = true;
            for (int seed = 1; seed <= 200; seed++)
            {
                MechPartDef r = MechPartStatRoll.Create(axe, seed);
                inBounds &= r.Damage >= axe.Damage * .9f - 1e-3f && r.Damage <= axe.Damage * 1.1f + 1e-3f
                         && r.Reload >= axe.Reload * .9f - 1e-3f && r.Reload <= axe.Reload * 1.1f + 1e-3f
                         && r.Ammo >= 45 && r.Ammo <= 55 && r.Id == axe.Id && r.Name == axe.Name && r.Base == axe;
                magazineKept &= r.Magazine == 1;
                changed |= r.Damage != axe.Damage;
            }
            Check(inBounds && changed, "200 rolls of the axe stay within +-10%, ammo a whole number in 45..55, identity kept");
            Check(magazineKept, "a one-round magazine stays one round");
            Check(MechPartStatRoll.Create(a, 99).Base == axe, "re-rolling a rolled part rolls from the catalogue, not the roll");

            Entity padEntity = Entity.Find("Weapon Drop Pad");
            ZooWeaponDropPad pad = padEntity != null ? padEntity.GetComponent<ZooWeaponDropPad>() : null;
            Check(pad != null && pad.drop1 != null && pad.drop6 != null, "the Zoo has a Weapon Drop Pad with its six drops");
            if (pad == null) yield break;
            Entity button = padEntity.FindChild("Button");
            Check(button != null && button.HasComponent<MeshRenderer>() && button.MeshRenderer.Visible && !button.HasComponent<Collider>()
                  && button.Transform.Scale.Y < .3f, "the pad shows a small orange button to step on (no collider of its own)");
            var drops = new List<MechPart>();
            MethodInfo drop = typeof(ZooWeaponDropPad).GetMethod("Drop", Private);
            var loose = (List<Entity>)typeof(ZooWeaponDropPad).GetField("loose", Private).GetValue(pad);
            for (int i = 0; i < 3; i++) drop.Invoke(pad, null);
            yield return .2f;
            foreach (Entity d in loose)
            {
                MechPart p = d != null && d.IsAlive ? d.GetComponent<MechPart>() : null;
                if (p != null && !p.IsMounted && p.Def != null && p.Def.IsRolled) drops.Add(p);
            }
            Check(drops.Count == 3, "three presses drop three loose parts with rolled stats (" + drops.Count + ")");
            bool grounded = true, armed = true;
            foreach (MechPart p in drops)
            {
                Note("dropped " + MechPartStatRoll.Describe(p.Def).Replace("\n", " "));
                grounded &= p.Transform.WorldPosition.Y < padEntity.Transform.WorldPosition.Y + 2.5f;
                armed &= p.Weapon != null && p.Weapon.Def == p.Def && (!p.Weapon.UsesAmmo || p.Weapon.RemainingAmmo == p.Def.Ammo);
            }
            Check(grounded, "the drops settle on the ground by the pad");
            Check(armed, "each drop's weapon uses its rolled stats (ammo = rolled ammo)");
            bool startingAuthored = true;
            foreach (MechSocket socket in MechLoadout.WeaponSockets)
            {
                MechPartDef def = player.EquippedPart(socket);
                startingAuthored &= def == null || !def.IsRolled;
            }
            Check(startingAuthored, "the mech's starting parts keep the catalogue's stats");
            foreach (MechPart p in drops) if (p != null && p.Entity.IsAlive) Destroy(p.Entity);
        }

        // ------------------------------------------------------------------------------------ //
        // 12. Heavy Cannon ground-targeted splash
        // ------------------------------------------------------------------------------------ //

        bool GroundUnder(Vector3 point, out RaycastHit hit)
        {
            return Physics.Raycast(point + Vector3.Up * .6f, new Vector3(0f, -1f, 0f), out hit, 1.5f, Enemy.MovementQueryMask, player.Entity);
        }

        IEnumerator<float> CannonGround()
        {
            PutPlayer(site + new Vector3(0f, 1f, 0f));
            player.Health.SetMaximum(100000f, true);
            Entity spawned = SpawnAt(Cannon, site + new Vector3(0f, 0f, -16f));
            yield return .15f;
            Enemy cannon = Track(spawned);
            Check(cannon.groundTargetSplash && Math.Abs(cannon.minimumAttackDistance - 3f) < 1e-4f, "Heavy Cannon: ground-targeted splash on, 3 m minimum firing distance");
            Entity disc = spawned.FindChild("Telegraph Splash");
            Check(disc != null, "Heavy Cannon prefab has its splash disc");

            // 1. Standing still: splash only.
            foreach (var t in Until(() => cannon.AttackInProgress, 12f)) yield return t;
            Vector3 aim = cannon.CommittedAim, feet = player.Transform.WorldPosition;
            RaycastHit ground;
            bool onGround = GroundUnder(aim, out ground) && Math.Abs(ground.Point.Y - aim.Y) < .15f;
            float lead = Flat(aim - feet);
            Vector3 toCannon = cannon.Position - feet;
            float side = Vector3.Dot(new Vector3(aim.X - feet.X, 0f, aim.Z - feet.Z), new Vector3(toCannon.X, 0f, toCannon.Z).Normalized);
            Check(cannon.CommittedToGround && onGround, "commits to a point on the ground (aim y " + F(aim.Y) + ", ground " + F(ground.Point.Y) + ")");
            Check(lead > .9f && lead < 1.8f && side > .8f, "the point is just in front of the feet, toward the cannon (" + F(lead) + " m)");
            yield return .1f;
            Check(disc != null && disc.MeshRenderer.Visible, "splash disc shown at the committed point during the wind-up");
            float hp = player.Health.Current;
            foreach (var t in Until(() => !cannon.AttackInProgress, 3f)) yield return t;
            yield return .6f;
            float taken = hp - player.Health.Current;
            Check(Math.Abs(taken - 27f) < .05f, "standing still: ground impact applies splash only (" + F(taken) + " of 27)");
            Check(disc == null || !disc.MeshRenderer.Visible, "splash disc hidden once the shot is away");

            // 2. Moving out before impact.
            foreach (var t in Until(() => cannon.AttackInProgress, 6f)) yield return t;
            hp = player.Health.Current;
            PutPlayer(player.Transform.WorldPosition + Quaternion.AngleAxis(90f, Vector3.Up) * (cannon.Facing * 7f));
            foreach (var t in Until(() => !cannon.AttackInProgress, 3f)) yield return t;
            yield return .6f;
            Check(player.Health.Current == hp, "moving 7 m out before impact: outside the radius, no damage");

            // 3. Direct interception: stepping into the shell's path.
            PutPlayer(site + new Vector3(0f, 1f, 0f));
            yield return .3f;
            foreach (var t in Until(() => cannon.AttackInProgress, 6f)) yield return t;
            int releases = cannon.Releases;
            foreach (var t in Until(() => cannon.Releases > releases || !cannon.AttackInProgress, 3f)) yield return t;
            hp = player.Health.Current;
            Vector3 muzzle = spawned.FindChild("Weapon Muzzle").Transform.WorldPosition;
            Vector3 mid = muzzle + (cannon.CommittedAim - muzzle) * .55f;
            PutPlayer(new Vector3(mid.X, mid.Y, mid.Z));
            yield return .7f;
            taken = hp - player.Health.Current;
            Check(Math.Abs(taken - 72f) < .05f, "intercepting the shell: direct 45 + splash 27 (" + F(taken) + ")");

            // 4. Shield blocks it.
            PutPlayer(site + new Vector3(0f, 1f, 0f));
            MechPart shield = Entity.Find("Zoo Pickup - Shield")?.GetComponent<MechPart>();
            bool equipped = shield != null && player.Equip(shield, MechSocket.ShoulderR);
            yield return .3f;
            foreach (var t in Until(() => cannon.AttackInProgress, 6f)) yield return t;
            releases = cannon.Releases;
            foreach (var t in Until(() => cannon.Releases > releases || !cannon.AttackInProgress, 3f)) yield return t;
            hp = player.Health.Current;
            MechWeapon shieldWeapon = equipped ? shield.Weapon : null;
            bool raised = shieldWeapon != null && player.TryUseWeapon(shieldWeapon, Time.ElapsedTime);
            yield return .45f;
            Check(raised && player.Health.Current == hp, "a raised Shield blocks the shell and its splash");

            // 5. Jumping: the impact is the ground below, not the airborne player's feet.
            yield return 1f;
            for (float t = 0f; t < 6f && !cannon.AttackInProgress; t += Time.DeltaTime)
            {
                PutPlayer(site + new Vector3(0f, 4.2f, 0f));   // 2.5 m above standing height
                yield return 0f;
            }
            aim = cannon.CommittedAim;
            onGround = GroundUnder(aim, out ground) && Math.Abs(ground.Point.Y - aim.Y) < .15f;
            Check(cannon.CommittedToGround && onGround && aim.Y < site.Y, "airborne player: the shell is committed to the ground below (" + F(aim.Y) + ")");

            // Use a new attack for each arrangement. A committed aim from the jumping fixture
            // must never be sampled after teleporting the participants to different terrain.
            Destroy(cannon.Entity); actors.Remove(cannon);
            RaycastHit rampPlayer, rampShooter;
            Vector3 ramp = slope.Transform.WorldPosition;
            bool playerOnRamp = Physics.Raycast(ramp + new Vector3(0f, 9f, 3.5f), Vector3.Up * -1f, out rampPlayer, 16f, Enemy.MovementQueryMask);
            bool shooterOnRamp = Physics.Raycast(ramp + new Vector3(0f, 9f, -3.5f), Vector3.Up * -1f, out rampShooter, 16f, Enemy.MovementQueryMask);
            Check(playerOnRamp && shooterOnRamp && rampPlayer.Entity == slope && rampShooter.Entity == slope, "both test positions are on the authored slope (surface " + (rampPlayer.Entity?.Name ?? "none") + ")");
            if (playerOnRamp && shooterOnRamp)
            {
                PutPlayer(rampPlayer.Point + Vector3.Up * 1.8f);
                spawned = SpawnAt(Cannon, rampShooter.Point + Vector3.Up);
                yield return .15f;
                cannon = Track(spawned); cannon.useRangedPositioning = false; spawned.GetComponent<FlowFieldAgent>().Speed = 0f;
                foreach (var t in Until(() => cannon.AttackInProgress, 6f)) yield return t;
                aim = cannon.CommittedAim;
                RaycastHit onSlope;
                bool slopeOk = GroundUnder(aim, out onSlope) && onSlope.Entity == slope && Math.Abs(onSlope.Point.Y - aim.Y) < .15f
                               && onSlope.Normal.Y < .995f && onSlope.Normal.Y > .9f;
                Check(cannon.CommittedToGround && slopeOk, "standing on a slope: committed point " + aim + ", surface " + (onSlope.Entity?.Name ?? "none") + ", normal y " + F(onSlope.Normal.Y) + ", firing " + cannon.AttackInProgress);
                Destroy(cannon.Entity); actors.Remove(cannon);
            }

            PutPlayer(site + Vector3.Up);
            spawned = SpawnAt(Cannon, site + new Vector3(0f, 0f, -14f));
            yield return .15f;
            cannon = Track(spawned); cannon.useRangedPositioning = false; spawned.GetComponent<FlowFieldAgent>().Speed = 0f;
            foreach (var t in Until(() => cannon.CommittedToGround, 6f)) yield return t;
            Vector3 goal = cannon.CommittedAim;
            Vector3 wallMuzzle = spawned.FindChild("Weapon Muzzle").Transform.WorldPosition;
            Vector3 eye = cannon.Position + Vector3.Up * .5f;
            Vector3 centre = player.Transform.WorldPosition + player.Transform.WorldRotation * player.Entity.CharacterController.Center;
            // At the wall plane, put its top strictly between the muzzle-to-ground and eye-to-body rays.
            Vector3 toward = new Vector3(wallMuzzle.X-centre.X, 0f, wallMuzzle.Z-centre.Z).Normalized;
            Vector3 plane = goal + toward * 1.0f;
            float groundFraction = Vector3.Dot(plane-goal,toward) / Vector3.Dot(wallMuzzle-goal,toward);
            float bodyFraction = Vector3.Dot(plane-centre,toward) / Vector3.Dot(eye-centre,toward);
            float groundLineY = goal.Y + (wallMuzzle.Y-goal.Y)*groundFraction;
            float bodyLineY = centre.Y + (eye.Y-centre.Y)*bodyFraction;
            lowWall.Transform.WorldRotation = Quaternion.LookRotation(toward);
            lowWall.Transform.WorldPosition = new Vector3(plane.X, (bodyLineY+groundLineY)*.5f - .7f, plane.Z);
            cannon.CancelAttack();
            yield return .15f;
            Vector3 line = goal-wallMuzzle, bodyLine = centre-eye;
            RaycastHit block, bodyBlock;
            bool hidesGround = Physics.Raycast(wallMuzzle,line.Normalized,out block,line.Length,Enemy.MovementQueryMask,cannon.Entity) && block.Entity == lowWall;
            bool hidesBody = Physics.Raycast(eye,bodyLine.Normalized,out bodyBlock,bodyLine.Length,Enemy.MovementQueryMask,cannon.Entity);
            Check(hidesGround && !hidesBody, "low wall blocks the ground ray and leaves the body visible (ground " + (block.Entity?.Name ?? "none") + ", body " + (bodyBlock.Entity?.Name ?? "clear") + ")");
            foreach (var t in Until(() => cannon.AttackInProgress, 8f)) yield return t;
            Check(cannon.AttackInProgress && !cannon.CommittedToGround && (cannon.CommittedAim-centre).Length < .6f,
                  "ground hidden by a low wall: commits to a body shot, aim " + cannon.CommittedAim + ", body " + centre);
        }

        // ------------------------------------------------------------------------------------ //
        // 14. Spinner fairness
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> Spinners()
        {
            PutPlayer(site + new Vector3(0f, 1f, 0f));
            player.Health.SetMaximum(1e7f, true);
            float[] distances = { 6.5f, 7.5f, 9f, 12.5f };
            float[] angles = { 0f, 70f, -70f, 160f };
            var spinners = new List<Enemy>();
            for (int i = 0; i < 4; i++)
            {
                Vector3 at = site + Quaternion.AngleAxis(angles[i], Vector3.Up) * new Vector3(0f, 0f, -distances[i]);
                Entity s = SpawnAt(Spinner, at);
                yield return .12f;
                Enemy e = Track(s);
                e.useSpecialCoordinator = true;
                spinners.Add(e);
            }
            int n = spinners.Count;
            int[] charges = new int[n];
            var coordinator = EnemySpecialCoordinator.Active;
            var reservations = typeof(EnemySpecialCoordinator).GetField("pool", Private).GetValue(coordinator);
            var queue = (System.Collections.IList)reservations.GetType().GetField("entries", Private).GetValue(reservations);
            Type entry = reservations.GetType().GetNestedType("Entry", BindingFlags.NonPublic);
            FieldInfo ticket = entry.GetField("Ticket"), owner = entry.GetField("Owner");
            long[] tickets = new long[n];
            float[] ticketStart = new float[n];
            float longestWait = 0f;
            bool fifo = true;
            int contestedGrants = 0;
            float[] outOfRange = new float[n], cooling = new float[n], notReach = new float[n], waiting = new float[n], busy = new float[n];
            Enemy.SpinnerPhase[] last = new Enemy.SpinnerPhase[n];
            float end = Time.ElapsedTime + 40f;
            while (Time.ElapsedTime < end)
            {
                PutPlayer(site + new Vector3(0f, 1f, 0f));   // the player stands its ground; knockback undone
                float dt = Time.DeltaTime;
                long[] pending = new long[n];
                foreach (object request in queue)
                {
                    int index = spinners.IndexOf((Enemy)owner.GetValue(request));
                    if (index >= 0) pending[index] = (long)ticket.GetValue(request);
                }
                for (int i = 0; i < n; i++)
                {
                    Enemy e = spinners[i];
                    if (e.SpinnerStage == Enemy.SpinnerPhase.Windup && last[i] != Enemy.SpinnerPhase.Windup)
                    {
                        charges[i]++;
                        for (int j = 0; j < n; j++)
                        {
                            if (j == i || pending[j] == 0 || pending[j] != tickets[j] || spinners[j].Target != e.Target ||
                                Flat(spinners[j].Position - e.Position) > coordinator.localRadius) continue;
                            contestedGrants++;
                            if (tickets[i] == 0 || pending[j] < tickets[i]) fifo = false;
                        }
                    }
                    if (pending[i] > 0 && pending[i] == tickets[i])
                        longestWait = Math.Max(longestWait, Time.ElapsedTime - ticketStart[i]);
                    else ticketStart[i] = Time.ElapsedTime;
                    last[i] = e.SpinnerStage;
                    if (e.AttackInProgress) busy[i] += dt;
                    else if (e.CooldownRemaining > 0f) cooling[i] += dt;
                    else if (e.DistanceToTarget > e.attackRange + .25f) outOfRange[i] += dt;
                    else if (!e.TargetInReach()) notReach[i] += dt;
                    else waiting[i] += dt;   // eligible, but not admitted (another Spinner's turn or start spacing)
                }
                tickets = pending;
                yield return 0f;
            }
            int least = int.MaxValue, most = 0;
            for (int i = 0; i < n; i++)
            {
                least = Math.Min(least, charges[i]); most = Math.Max(most, charges[i]);
                Note("Spinner " + i + " (started " + F(distances[i]) + " m): " + charges[i] + " charges; seconds attacking " + F(busy[i])
                     + ", cooldown " + F(cooling[i]) + ", out of range " + F(outOfRange[i]) + ", not facing/clear " + F(notReach[i])
                     + ", eligible but waiting for a turn " + F(waiting[i]));
            }
            Check(least >= 2, "4 Spinners over 40 s: every Spinner charged at least twice (fewest " + least + ", most " + most + ")");
            Note("Charge count spread " + (most-least) + "; includes time spent out of range and on cooldown");
            float turnBudget = n * (spinners[0].spinnerWindup + spinners[0].chargeDuration + coordinator.minimumStartSpacing + .65f);
            Check(contestedGrants > 0 && fifo && longestWait < turnBudget, "continuously eligible tickets are served in order (competing tickets at grants "
                + contestedGrants + ", longest wait " + F(longestWait) + " s, budget " + F(turnBudget) + " s)");
        }

        // ------------------------------------------------------------------------------------ //
        // 15. Coordination toggles: each off still attacks
        // ------------------------------------------------------------------------------------ //

        IEnumerator<float> Toggles()
        {
            player.Health.SetMaximum(1e7f, true);
            foreach (int label in new[] { Sword, SmallCrossbow, Spinner })
            {
                PutPlayer(site + new Vector3(0f, 1f, 0f));
                Entity s = SpawnAt(label, site + new Vector3(4f, 0f, -12f));
                yield return .15f;
                Enemy e = Track(s);
                string toggle = label == Sword ? "Use Horde Coordination" : label == SmallCrossbow ? "Use Ranged Positioning" : "Use Special Coordinator";
                if (label == Sword) e.useHordeCoordination = false;
                if (label == SmallCrossbow) e.useRangedPositioning = false;
                if (label == Spinner) e.useSpecialCoordinator = false;
                int releases = e.Releases;
                foreach (var t in Until(() => e.Releases > releases, 15f)) yield return t;
                Check(e.Releases > releases, labels[label] + " with " + toggle + " off still approaches and attacks");
                Destroy(e.Entity);
                actors.Remove(e);
                yield return .2f;
            }
        }
    }
}
