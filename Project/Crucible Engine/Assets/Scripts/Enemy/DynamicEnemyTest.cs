using System;
using Crucible;

namespace Crucible.Scripts
{
    // A test, not gameplay: checks that dynamic enemies react to physics. Put it on any
    // entity in a scene with golems and a player, press Play, and read the "[DynamicTest]"
    // lines in the log.
    //   explodeAt   - Physics.Explode next to the nearest golem: golems in range should be
    //                 thrown (speed and height jump), then land and walk back.
    //   killAt      - the nearest golem takes a killing hit with a big knockback: it should
    //                 fly back, topple over (rotation leaves upright) and vanish toppleSeconds later.
    // Expected results are written into the log before each step, so a run reads as a check.
    public sealed class DynamicEnemyTest : EntityScript
    {
        public string playerName = "Player Golem";
        public float explodeAt = 10f;
        public float explosionForce = 1500f;     // impulse at the centre; a 200 kg golem gets 7.5 m/s
        public float explosionRadius = 8f;
        public float killAt = 13f;
        public float killKnockback = 800f;       // impulse: 4 m/s on a 200 kg golem

        bool exploded, killed;
        float watchUntil, nextWatch;
        Enemy victim;

        public override void OnUpdate(float deltaTime)
        {
            float now = Time.ElapsedTime;
            Entity player = Entity.Find(playerName);
            if (ReferenceEquals(player, null)) return;
            Vector3 p = player.Transform.WorldPosition;

            if (!exploded && now >= explodeAt)
            {
                exploded = true;
                Enemy near = Nearest(p);
                if (near == null) { Log.Warning("[DynamicTest] no golem to explode next to"); return; }

                // The blast goes off between the player and the nearest golem, a little low.
                Vector3 centre = near.Position + Flat(p - near.Position).Normalized * 1.5f + Vector3.Up * -0.5f;
                Log.Info("[DynamicTest] EXPLODE at " + Fmt(centre) + " r=" + explosionRadius + " force=" + explosionForce
                         + ". Expect: golems within " + explosionRadius + " m get speed > 2 m/s away from it, some leave the ground, then land and walk back.");
                foreach (Enemy e in Enemy.All) if (e.Entity.IsActive) Log.Info("[DynamicTest]   before " + Describe(e));
                int reached = Physics.Explode(centre, explosionRadius, explosionForce, 1f);
                Log.Info("[DynamicTest]   Explode reached " + reached + " entities");
                watchUntil = now + 2.5f;
            }

            if (!killed && now >= killAt)
            {
                killed = true;
                victim = Nearest(p);
                if (victim == null) { Log.Warning("[DynamicTest] no golem to kill"); return; }
                Vector3 away = Flat(victim.Position - p).Normalized;
                Log.Info("[DynamicTest] KILL " + victim.Entity.Name + "#" + victim.Entity.Index + " with knockback " + killKnockback
                         + " away from the player. Expect: it flies back several metres, its tilt goes well past 30 degrees (toppled), and it is gone "
                         + victim.toppleSeconds + " s later.");
                victim.ApplyDamage(10000f, away * killKnockback);
                watchUntil = now + victim.toppleSeconds + 1f;
            }

            // Watch for a while after each event, every 0.25 s.
            if (now < watchUntil && now >= nextWatch)
            {
                nextWatch = now + 0.25f;
                foreach (Enemy e in Enemy.All)
                    if (e.Entity.IsActive && (e == victim || !killed)) Log.Info("[DynamicTest]   t=" + now.ToString("F2") + " " + Describe(e));
                if (killed && victim != null && !victim.Entity.IsAlive) Log.Info("[DynamicTest]   t=" + now.ToString("F2") + " victim is gone");
            }
        }

        Enemy Nearest(Vector3 from)
        {
            Enemy best = null;
            float bestDistance = float.PositiveInfinity;
            foreach (Enemy e in Enemy.All)
            {
                if (!e.Entity.IsActive || e.IsDead) continue;
                float d = Flat(e.Position - from).Length;
                if (d < bestDistance) { bestDistance = d; best = e; }
            }
            return best;
        }

        static string Describe(Enemy e)
        {
            Entity entity = e.Entity;
            if (!entity.IsAlive) return "(gone)";
            Vector3 v = entity.HasComponent<RigidBody>() ? entity.RigidBody.Velocity : Vector3.Zero;
            // Tilt: how far its up axis leans from world up, in degrees (0 = upright).
            Vector3 up = entity.Transform.WorldRotation.Up;
            float tilt = (float)(Math.Acos(Math.Max(-1.0, Math.Min(1.0, up.Y))) * 180.0 / Math.PI);
            return entity.Name + "#" + entity.Index + " pos=" + Fmt(e.Position) + " vel=" + Fmt(v)
                 + " speed=" + Flat(v).Length.ToString("F2") + " tilt=" + tilt.ToString("F0") + " action=\"" + e.CurrentAction + "\""
                 + (e.IsDead ? " DEAD" : "");
        }

        static Vector3 Flat(Vector3 v) { return new Vector3(v.X, 0f, v.Z); }
        static string Fmt(Vector3 v) { return "(" + v.X.ToString("F2") + "," + v.Y.ToString("F2") + "," + v.Z.ToString("F2") + ")"; }
    }
}
