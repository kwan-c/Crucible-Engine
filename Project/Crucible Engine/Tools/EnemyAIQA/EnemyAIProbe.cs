using System;
using System.Collections.Generic;
using Crucible;

namespace Crucible.Scripts
{
    // Added only to an isolated copy of the Zoo by Run.ps1. All movement and contacts run
    // through the shipping physics, graph and Mono systems.
    public sealed class EnemyAIProbe : EntityScript
    {
        MechPlayer player;
        IEnumerator<float> steps;
        float wait;
        int pass, fail;
        readonly Vector3 site = new Vector3(-55f, 1.2f, -46f);
        readonly List<Enemy> actors = new List<Enemy>();
        readonly string[] labels = { "Sword Fodder", "Mace Fodder",    "Throwing Axe Fodder", "Small Crossbow Fodder",
                                     "Heavy Cannon", "Heavy Crossbow", "Spinner Wind-up Test" };
        public override void OnCreate()
        {
            steps = Run();
        }
        public override void OnUpdate(float dt)
        {
            if (steps == null || (wait -= dt) > 0f)
                return;
            try
            {
                if (steps.MoveNext())
                {
                    wait = steps.Current;
                    return;
                }
            }
            catch (Exception ex)
            {
                Check(false, ex.ToString());
            }
            steps = null;
            Log.Info("[EnemyAIQA] " + pass + " passed; END failures=" + fail);
            System.IO.File.WriteAllText(Environment.GetEnvironmentVariable("CRUCIBLE_QA_RESULT"), fail.ToString());
        }
        void Check(bool ok, string msg)
        {
            if (ok)
                pass++;
            else
                fail++;
            Log.Info("[EnemyAIQA] " + (ok ? "PASS " : "FAIL ") + msg);
        }
        Entity Spawn(int i, float x = 0f, float z = 0f)
        {
            return Entity.Find("Combat Test - " + labels[i])
                .GetComponent<ZooCombatSpawnPad>()
                .enemyPrefab.Instantiate(site + new Vector3(x, .4f, z), Quaternion.Identity);
        }
        void Put(float x, float z)
        {
            player.Transform.WorldPosition = site + new Vector3(x, 1f, z);
        }
        Enemy Track(Entity entity)
        {
            Enemy e = entity.GetComponent<Enemy>();
            e.dropChancePercent = 0;
            e.Wake();
            actors.Add(e);
            return e;
        }
        void Clear()
        {
            foreach (var e in actors)
                if (e.Entity.IsAlive)
                    Destroy(e.Entity);
            actors.Clear();
        }
        IEnumerable<float> Until(Func<bool> predicate, float seconds)
        {
            float end = Time.ElapsedTime + seconds;
            while (!predicate() && Time.ElapsedTime < end)
                yield return 0;
        }
        IEnumerator<float> Run()
        {
            yield return 1.2f;
            player = Entity.Find("Player").GetComponent<MechPlayer>();
            Entity.Find("QA second player").SetEnabled<PlayerController>(false);
            player.Health.SetMaximum(5000, true);
            for (int trial = 0; trial < 2; trial++)
            {
                Put(0, -11);
                Entity actor = Spawn(6);
                yield return .12f;
                var e = Track(actor);
                e.useSpecialCoordinator = false;
                foreach (var t in Until(() => e.SpinnerStage == Enemy.SpinnerPhase.Windup, 12))
                    yield return t;
                Check(e.TargetDetected && e.SpinnerStage == Enemy.SpinnerPhase.Windup,
                      "independent graph detects, approaches and winds up; pos=" + e.Position +
                          " d=" + e.DistanceToTarget);
                var warning = actor.FindChild("Telegraph");
                Vector3 start = e.Position, before = player.Transform.WorldPosition;
                Vector3 heading = MechMath.Flat(before - start).Normalized;
                yield return .08f;
                Check(warning.MeshRenderer.Visible &&
                          Vector3.Dot(MechMath.Flat(warning.Transform.WorldPosition - start).Normalized, heading) >
                              .96f,
                      "telegraph points down committed lane");
                if (trial == 1)
                {
                    actor.GetComponent<FlowFieldAgent>().Speed = 7.25f;
                    player.Transform.WorldPosition = before + Vector3.Cross(Vector3.Up, heading) * 6f;
                }
                float hp = player.Health.Current;
                bool charged = false, recovered = false;
                float peak = 0, side = 0, travel = 0;
                float end = Time.ElapsedTime + 3f;
                while (Time.ElapsedTime < end)
                {
                    if (e.SpinnerStage == Enemy.SpinnerPhase.Charge)
                    {
                        charged = true;
                        peak = Math.Max(peak, MechMath.Flat(actor.RigidBody.Velocity).Length);
                        Vector3 delta = MechMath.Flat(e.Position - start);
                        travel = Math.Max(travel, Vector3.Dot(delta, heading));
                        side = Math.Max(side, Math.Abs(Vector3.Dot(delta, Vector3.Cross(Vector3.Up, heading))));
                    }
                    if (e.SpinnerStage == Enemy.SpinnerPhase.Recovery)
                        recovered = true;
                    if (recovered && !e.AttackInProgress)
                        break;
                    yield return 0;
                }
                Check(charged && peak > e.chargeSpeed * .8f && side < .55f,
                      "physical committed charge speed=" + peak + " travel=" + travel + " lateral=" + side);
                Check(recovered && !e.AttackInProgress && e.Entity.IsEnabled<FlowFieldAgent>() &&
                          !warning.MeshRenderer.Visible,
                      "recovery restores navigation and warning");
                if (trial == 0)
                {
                    Check(Math.Abs(hp - player.Health.Current - e.chargeDamage) < .01f,
                          "one accepted hit, damage=" + (hp - player.Health.Current));
                    Check(Vector3.Dot(player.Transform.WorldPosition - before, heading) > 2f,
                          "player motor knockback after hit stop");
                }
                else
                {
                    Check(hp == player.Health.Current && travel > 6f, "sidestep avoids damage and charge times out");
                    var agent = actor.GetComponent<FlowFieldAgent>();
                    Check(Math.Abs(agent.Speed - 7.25f) < .001f, "special recovery preserves a live FlowField speed edit");
                    e.Wander();
                    bool slowed = Math.Abs(agent.Speed - 7.25f * e.wanderSpeedFraction) < .001f;
                    agent.Speed = 5.25f;
                    e.Hold("QA hold after external speed change");
                    bool preserved = Math.Abs(agent.Speed - 5.25f) < .001f;
                    e.Wander();
                    e.Hold("QA resume normal speed");
                    Check(slowed && preserved && Math.Abs(agent.Speed - 5.25f) < .001f,
                          "wander restores its own speed change and honors an external override");
                }
                Clear();
                yield return .2f;
            }
            foreach (var t in Interruptions())
                yield return t;
            foreach (var t in Specials())
                yield return t;
            foreach (var t in SeparateTargetsAndEdge())
                yield return t;
            foreach (var t in Melee())
                yield return t;
            foreach (var t in Ranged())
                yield return t;
            foreach (var t in Cover())
                yield return t;
            foreach (var t in Mixed())
                yield return t;
            foreach (var t in Waves())
                yield return t;
        }

        IEnumerable<float> Interruptions()
        {
            var wall = Entity.Find("QA obstruction");
            for (int i = 0; i < 10; i++)
            {
                Put(0, -7);
                Entity actor = Spawn(6);
                yield return .12f;
                var e = Track(actor);
                foreach (var t in Until(() => e.SpinnerStage == Enemy.SpinnerPhase.Windup, 4))
                    yield return t;
                bool windup = e.SpinnerStage == Enemy.SpinnerPhase.Windup;
                Vector3 start = e.Position, heading = MechMath.Flat(player.Transform.WorldPosition - start).Normalized;
                float hp = player.Health.Current;
                if (i == 0 || i == 1)
                {
                    if (i == 1)
                        foreach (var t in Until(() => e.SpinnerStage == Enemy.SpinnerPhase.Charge, 2))
                            yield return t;
                    e.ApplyDamage(10000);
                    Check(windup && e.IsDead && !e.AttackInProgress && !actor.IsEnabled<FlowFieldAgent>() &&
                              !actor.FindChild("Telegraph").MeshRenderer.Visible,
                          "death cancels " + (i == 0 ? "wind-up" : "charge") + " before corpse physics");
                }
                else if (i == 2 || i == 3 || i == 4 || i == 5)
                {
                    if (i == 2)
                        actor.SetEnabled<StateGraphAgent>(false);
                    if (i == 3)
                        player.Entity.SetActive(false);
                    if (i == 4)
                    {
                        e.Enabled = false;
                    }
                    if (i == 5)
                        actor.SetActive(false);
                    yield return .18f;
                    Check(windup && !e.AttackInProgress, "attack and reservation cancelled on " + new[] {
                        "graph abort", "target loss", "script disable", "entity disable"
                    }[i - 2]);
                    player.Entity.SetActive(true);
                    actor.SetActive(true);
                    e.Enabled = true;
                    actor.SetEnabled<StateGraphAgent>(true);
                    yield return .12f;
                    Check(actor.IsEnabled<FlowFieldAgent>() && !actor.FindChild("Telegraph").MeshRenderer.Visible,
                          "ordinary legs restored after interruption " + i);
                }
                else if (i == 6)
                {
                    Destroy(actor);
                    yield return .12f;
                    Check(windup && !actor.IsAlive && !Enemy.All.Contains(e),
                          "destroyed spinner removed from registry during special");
                }
                else if (i == 7)
                {
                    wall.Transform.WorldPosition = start + heading * 3f + Vector3.Up;
                    yield return 1.12f;
                    Check(windup && e.SpinnerStage == Enemy.SpinnerPhase.Recovery && hp == player.Health.Current &&
                              Vector3.Dot(e.Position - start, heading) < 3f,
                          "wall inserted after commitment stops charge before player");
                    wall.Transform.WorldPosition = new Vector3(-85, 2, -85);
                }
                else if (i == 8)
                {
                    var shield = Entity.Find("Zoo Pickup - Shield").GetComponent<MechPart>();
                    player.Equip(shield, MechSocket.ShoulderR);
                    player.Health.SetMaximum(5000, true);
                    hp = player.Health.Current;
                    foreach (var t in Until(() => e.SpinnerStage == Enemy.SpinnerPhase.Charge, 2))
                        yield return t;
                    foreach (var t in Until(() => MechMath.Flat(e.Position - player.Transform.WorldPosition).Length <
                                                      3f ||
                                                  e.SpinnerStage != Enemy.SpinnerPhase.Charge,
                                      1f))
                        yield return t;
                    float distance = MechMath.Flat(e.Position - player.Transform.WorldPosition).Length;
                    bool raised = player.TryUseWeapon(shield.Weapon, Time.ElapsedTime);
                    bool blocking = raised && player.Health.BlocksDamage();
                    yield return 1f;
                    Check(windup && blocking && player.Health.Current == hp,
                          "equipped Shield rejects Spinner contact, guarded=" + blocking + " distance=" + distance +
                              " loss=" + (hp - player.Health.Current));
                }
                else
                {
                    foreach (var t in Until(() => e.SpinnerStage == Enemy.SpinnerPhase.Charge, 2))
                        yield return t;
                    MechHitStop.Request(actor, .1f);
                    yield return .03f;
                    bool paused = e.IsFrozen && MechMath.Flat(actor.RigidBody.Velocity).Length < .2f;
                    e.ApplyMeleeHit(1, heading * -6f, Vector3.Zero, .25f);
                    yield return .45f;
                    Check(windup && paused && !e.AttackInProgress && actor.IsEnabled<FlowFieldAgent>(),
                          "hit stop pauses motion and a melee shove cancels the charge");
                }
                Clear();
                yield return .18f;
            }
        }

        IEnumerable<float> Specials()
        {
            var coord = EnemySpecialCoordinator.Active;
            Check(coord != null, "Zoo special coordinator available");
            Put(0, -5);
            var a = Spawn(6, -3);
            var b = Spawn(6, 3);
            yield return .12f;
            var one = Track(a);
            var two = Track(b);
            bool prevA = false, prevB = false;
            int startsA = 0, startsB = 0, peak = 0;
            float last = -100, gap = 100;
            float end = Time.ElapsedTime + 9;
            while (Time.ElapsedTime < end)
            {
                bool wa = one.SpinnerStage == Enemy.SpinnerPhase.Windup,
                     wb = two.SpinnerStage == Enemy.SpinnerPhase.Windup;
                int active = (wa || one.SpinnerStage == Enemy.SpinnerPhase.Charge ? 1 : 0) +
                             (wb || two.SpinnerStage == Enemy.SpinnerPhase.Charge ? 1 : 0);
                peak = Math.Max(peak, active);
                if ((wa && !prevA) || (wb && !prevB))
                {
                    gap = Math.Min(gap, Time.ElapsedTime - last);
                    last = Time.ElapsedTime;
                    if (wa && !prevA)
                        startsA++;
                    if (wb && !prevB)
                        startsB++;
                }
                prevA = wa;
                prevB = wb;
                if (one.SpinnerStage == Enemy.SpinnerPhase.Recovery || two.SpinnerStage == Enemy.SpinnerPhase.Recovery)
                    Put(0, -5);
                yield return 0;
            }
            Check(peak == 1 && startsA > 0 && startsB > 0 && startsA + startsB >= 3,
                  "local cap and both Spinners get repeated opportunities (" + startsA + "/" + startsB + ")");
            Check(gap >= coord.minimumStartSpacing - .04f, "observed start spacing " + gap + "s");
            Clear();
            yield return .2f;
            for (int i = 0; i < 2; i++)
            {
                Put(0, -5);
                if (i == 1)
                    coord.Enabled = false;
                a = Spawn(6, -3);
                b = Spawn(6, 3);
                yield return .07f;
                one = Track(a);
                two = Track(b);
                if (i == 0)
                {
                    one.useSpecialCoordinator = false;
                    two.useSpecialCoordinator = false;
                }
                bool together = false;
                foreach (var t in Until(() => together, 4))
                {
                    together =
                        one.SpinnerStage == Enemy.SpinnerPhase.Windup && two.SpinnerStage == Enemy.SpinnerPhase.Windup;
                    yield return t;
                }
                Check(together, i == 0 ? "per-enemy bypass admits both Spinners"
                                       : "disabled scene host permits independent specials");
                Clear();
                coord.Enabled = true;
                yield return .2f;
            }
        }

        IEnumerable<float> Melee()
        {
            var horde = EnemyHordeCoordinator.Active;
            Check(horde != null, "Zoo horde coordinator available");
            for (int mode = 0; mode < 3; mode++)
            {
                Put(0, -8);
                horde.reservedPositions = mode == 1;
                horde.maximumMeleeAttacks = 1;
                if (mode == 2)
                    horde.Enabled = false;
                Entity[] roots = new Entity[4];
                for (int i = 0; i < 4; i++)
                    roots[i] = Spawn(i % 2, (i - 1.5f) * 1.7f, i % 2);
                yield return .12f;
                foreach (Entity root in roots)
                    Track(root);
                var seen = new List<Enemy>();
                bool distinct = false, killedHolder = false;
                int peak = 0;
                float end = Time.ElapsedTime + 7;
                while (Time.ElapsedTime < end)
                {
                    int concurrent = 0;
                    Vector3? previous = null;
                    foreach (Enemy e in actors)
                    {
                        if (e.IsDead)
                            continue;
                        if (e.HasPressureGoal)
                        {
                            if (previous.HasValue && (previous.Value - e.PressureGoal).Length > 1.2f)
                                distinct = true;
                            previous = e.PressureGoal;
                        }
                        if (e.AttackInProgress)
                        {
                            if (!seen.Contains(e))
                                seen.Add(e);
                            concurrent++;
                        }
                    }
                    peak = Math.Max(peak, concurrent);
                    if (mode == 1 && !killedHolder && seen.Count == 1)
                        foreach (Enemy e in actors)
                            if (e.AttackInProgress)
                            {
                                e.ApplyDamage(10000);
                                killedHolder = true;
                                break;
                            }
                    yield return 0;
                }
                Check(seen.Count >= 2, "Sword/Mace pressure and attack in mode " + mode + " attackers=" + seen.Count);
                if (mode == 0)
                    Check(distinct, "same-side melee choose separate pressure goals");
                if (mode == 1)
                    Check(killedHolder && peak == 1 && seen.Count > 1, "melee cap and new grant after holder death");
                if (mode == 2)
                    Check(!distinct, "missing host falls back to ordinary navigation");
                Clear();
                horde.Enabled = true;
                horde.reservedPositions = false;
                yield return .2f;
            }

            // Occupy every pressure point with the other actor's spacing exclusion.
            float originalSpacing = horde.pressureSpacing;
            horde.pressureSpacing = 100f;
            Put(0, -7);
            Entity sword = Spawn(0, -1.5f), mace = Spawn(1, 1.5f);
            yield return .12f;
            Enemy first = Track(sword), second = Track(mace);
            bool rejected = false;
            float deadline = Time.ElapsedTime + 8f;
            while (Time.ElapsedTime < deadline && !(first.AttackInProgress || second.AttackInProgress))
            {
                rejected |= first.PressureTarget == player.Entity && second.PressureTarget == player.Entity &&
                            !first.HasPressureGoal && !second.HasPressureGoal;
                yield return 0;
            }
            Check(rejected && (first.AttackInProgress || second.AttackInProgress),
                  "unavailable pressure points fall back to ordinary pursuit and melee");
            horde.pressureSpacing = originalSpacing;
            Clear();
            yield return .2f;
        }

        IEnumerable<float> SeparateTargetsAndEdge()
        {
            var coord = EnemySpecialCoordinator.Active;
            var second = Entity.Find("QA second player");
            Put(-6, -5);
            second.Transform.WorldPosition = site + new Vector3(6, 1, -5);
            Entity a = Spawn(6, -6), b = Spawn(6, 6);
            yield return .12f;
            Enemy one = Track(a), two = Track(b);
            foreach (var t in Until(() => one.SpinnerStage == Enemy.SpinnerPhase.Windup &&
                                          two.SpinnerStage == Enemy.SpinnerPhase.Windup,
                              4))
                yield return t;
            Check(one.Target == player.Entity && two.Target == second && one.AttackInProgress && two.AttackInProgress,
                  "nearby Spinners can attack separate player targets concurrently");
            Clear();
            second.Transform.WorldPosition = new Vector3(85, 2, -80);
            yield return .2f;

            Put(0, -5);
            a = Spawn(6, -2);
            b = Spawn(6, 2);
            yield return .08f;
            one = Track(a);
            two = Track(b);
            coord.minimumStartSpacing = 1.1f;
            foreach (var t in Until(() => one.AttackInProgress || two.AttackInProgress, 4))
                yield return t;
            Enemy first = one.AttackInProgress ? one : two, next = first == one ? two : one;
            float cancelled = Time.ElapsedTime;
            first.Entity.SetEnabled<StateGraphAgent>(false);
            foreach (var t in Until(() => next.AttackInProgress, 4))
                yield return t;
            float gap = Time.ElapsedTime - cancelled;
            Check(next.AttackInProgress && !first.AttackInProgress && gap >= 1.0f && gap < 3f,
                  "pending special observes start spacing after graph cancellation (" + gap.ToString("F2") + "s)");
            coord.minimumStartSpacing = .55f;
            Clear();
            yield return .2f;

            var pad = Entity.Find("Combat Test - Spinner Wind-up Test").GetComponent<ZooCombatSpawnPad>();
            player.Transform.WorldPosition = new Vector3(97, 1.5f, -40);
            var edge = pad.enemyPrefab.Instantiate(new Vector3(92, 1.4f, -40), Quaternion.Euler(0, 90, 0));
            yield return .12f;
            var e = Track(edge);
            foreach (var t in Until(() => e.SpinnerStage == Enemy.SpinnerPhase.Windup, 4))
                yield return t;
            bool ready = e.AttackInProgress;
            player.Transform.WorldPosition = new Vector3(94, 1.5f, -32);
            foreach (var t in Until(() => e.SpinnerStage == Enemy.SpinnerPhase.Recovery, 3))
                yield return t;
            Check(ready && e.SpinnerStage == Enemy.SpinnerPhase.Recovery && e.Position.X > 94f && e.Position.X < 100f &&
                      e.Position.Y > 0f,
                  "unsupported arena edge stops a committed miss on solid ground (" + e.Position + ")");
            Clear();
            yield return .2f;
        }

        IEnumerable<float> Ranged()
        {
            for (int i = 2; i <= 5; i++)
            {
                Put(0, -2);
                var root = Spawn(i);
                yield return .12f;
                Enemy e = Track(root);
                float start = e.DistanceToTarget;
                bool replanned = false, attacked = false;
                float end = Time.ElapsedTime + 7;
                while (Time.ElapsedTime < end && !attacked)
                {
                    replanned |= e.HasRangedPosition;
                    attacked |= e.AttackInProgress;
                    yield return 0;
                }
                Check(e.IsRanged && replanned && e.DistanceToTarget > start + .3f && attacked,
                      labels[i] + " retreats, selects a firing position and attacks; d=" + e.DistanceToTarget);
                e.useRangedPositioning = false;
                Put(0, -(e.attackRange + 12));
                yield return .3f;
                foreach (var t in Until(() => !e.AttackInProgress, 2))
                    yield return t;
                float far = e.DistanceToTarget;
                foreach (var t in Until(() => e.DistanceToTarget < far - 1f, 3))
                    yield return t;
                Check(!e.UsesRangedPositioning && !e.HasRangedPosition && e.IsRanged && e.DistanceToTarget < far - .5f,
                      labels[i] + " ordinary ranged approach with planner off");
                player.Transform.WorldPosition = e.Position + new Vector3(0, .8f, -2f);
                bool retreated = false;
                end = Time.ElapsedTime + 8f;
                while (Time.ElapsedTime < end && !e.AttackInProgress)
                {
                    retreated |= e.DistanceToTarget < e.minimumRange && e.CurrentAction == "Reposition";
                    yield return 0;
                }
                Check(retreated && e.AttackInProgress && !e.HasRangedPosition && e.DistanceToTarget >= e.minimumRange,
                      labels[i] + " retreats and fires again with the planner disabled");
                Clear();
                yield return .18f;
            }
        }

        IEnumerable<float> Mixed()
        {
            Put(0, -6);
            var roots = new List<Entity>();
            for (int i = 0; i < 21; i++)
                roots.Add(Spawn(i % 7, (i % 7 - 3) * 3.1f, (i / 7) * 4f + 2));
            yield return .18f;
            foreach (Entity root in roots)
                Track(root);
            int frames = 0;
            double sum = 0, max = 0;
            DateTime last = DateTime.UtcNow;
            var attacking = new List<Enemy>();
            float end = Time.ElapsedTime + 6;
            while (Time.ElapsedTime < end)
            {
                foreach (Enemy e in actors)
                    if (e.AttackInProgress && !attacking.Contains(e))
                        attacking.Add(e);
                DateTime now = DateTime.UtcNow;
                double ms = (now - last).TotalMilliseconds;
                last = now;
                if (frames > 0)
                {
                    sum += ms;
                    max = Math.Max(max, ms);
                }
                frames++;
                yield return 0;
            }
            bool grounded = true;
            foreach (Enemy e in actors)
                grounded &= e.Position.Y > -.5f && e.Position.Y < 6f;
            Check(grounded && attacking.Count >= 7,
                  "21-enemy mixed horde stays grounded, " + attacking.Count + " attackers");
            Log.Info("[EnemyAIQA] mixed 21 agents, " + frames + " frames, mean " +
                     (sum / Math.Max(1, frames - 1)).ToString("F2") + " ms, max " + max.ToString("F2") +
                     " ms (whole Debug game)");
            Clear();
            yield return .25f;
            Check(Enemy.All.Count == 0, "mixed horde registry clean after destruction");
        }

        IEnumerable<float> Cover()
        {
            var wall = Entity.Find("QA obstruction");
            wall.Transform.WorldPosition = site + new Vector3(0, 1, -4);
            Put(0, -9);
            var sword = Spawn(0, -.5f);
            var mace = Spawn(1, .5f, 1);
            var bow = Spawn(3, -.3f, 2);
            var cannon = Spawn(4, .3f, 3);
            var axe = Spawn(2, -.8f, 3.7f);
            var heavyBow = Spawn(5, .8f, 4.7f);
            yield return .16f;
            Enemy a = Track(sword), b = Track(mace), c = Track(bow), d = Track(cannon);
            Enemy[] shooters = { Track(axe), c, d, Track(heavyBow) };
            bool[] soughtPosition = new bool[shooters.Length], fired = new bool[shooters.Length];
            bool validGoal = false, firing = false, different = false, meleeAttack = false;
            float end = Time.ElapsedTime + 13;
            while (Time.ElapsedTime < end)
            {
                for (int i = 0; i < shooters.Length; i++)
                {
                    soughtPosition[i] |= shooters[i].HasRangedPosition;
                    fired[i] |= shooters[i].AttackInProgress;
                }
                validGoal |= c.HasRangedPosition || d.HasRangedPosition;
                if (c.HasRangedPosition && d.HasRangedPosition)
                    different |= (c.RangedPosition - d.RangedPosition).Length > 1f;
                firing |= c.AttackInProgress || d.AttackInProgress;
                meleeAttack |= a.AttackInProgress || b.AttackInProgress;
                yield return 0;
            }
            Check(validGoal && firing, "ranged group finds a firing angle around solid cover");
            Check(different || MechMath.Flat(c.Position - d.Position).Length > 3f,
                  "ranged peers maintain separate firing positions");
            Check(meleeAttack, "melee group navigates around cover and resumes pressure");
            for (int i = 0; i < shooters.Length; i++)
                Check(soughtPosition[i] && fired[i], labels[i + 2] + " finds a firing position and attacks around cover");
            a.useHordeCoordination = false;
            c.useRangedPositioning = false;
            yield return .15f;
            Check(!a.HasPressureGoal && !c.HasRangedPosition && c.IsRanged,
                  "participation switches clear stale goals independently");
            wall.Transform.WorldPosition = new Vector3(-85, 2, -85);
            Clear();
            yield return .2f;
        }

        IEnumerable<float> Waves()
        {
            var waves = Entity.Find("Enemy").GetComponent<ZooEnemyWaves>();
            waves.spawnInterval = .15f;
            waves.resourcePerCycle = 25;
            waves.maximumAlive = 8;
            waves.resetDelay = 10;
            waves.smallEnemies.SetActive(true);
            waves.eliteEnemies.SetActive(true);
            yield return 2f;
            int spent = 0;
            bool clean = true, grounded = true;
            var ids = new List<string>();
            foreach (Enemy e in Enemy.All)
            {
                if (e.IsDead)
                    continue;
                clean &= !e.previewSpinnerWindup && !string.IsNullOrEmpty(e.equippedWeaponId);
                grounded &= e.Position.Y > -.8f;
                spent += e.spawnCost;
                if (!ids.Contains(e.equippedWeaponId))
                    ids.Add(e.equippedWeaponId);
            }
            Check(clean && grounded && ids.Count == 6,
                  "Zoo budget selects all four fodder and two elites on supported ground");
            Check(spent == 25 && waves.ResourceRemaining == 0 && waves.ActiveCount == 6,
                  "Zoo charges prefab costs exactly once (" + spent + "/25, alive " + waves.ActiveCount + ")");
            waves.smallEnemies.SetActive(false);
            waves.eliteEnemies.SetActive(false);
            foreach (Enemy e in Enemy.All)
                if (!e.IsDead)
                    e.ApplyDamage(10000);
            yield return .35f;
            Check(waves.ActiveCount == 0, "disabled Zoo groups remove dead enemies from wave tracking");
        }
    }
}
