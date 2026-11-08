using System;
using Crucible;

namespace Crucible.Scripts
{
    // A measuring tool for the enemy AI, not gameplay: put it on any entity and every
    // "interval" seconds it logs, for each enemy, its state, its flat distance to the
    // player, its speed, whether its agent has arrived, and how close its nearest other
    // enemy is. Read the lines starting "[NavProbe]" in the log. Drift shows up as the
    // distance growing while the player stands still.
    public sealed class NavProbe : EntityScript
    {
        public string playerName = "Player Golem";
        public float interval = 0.5f;

        float nextLog;
        Entity player;
        bool queriesChecked;

        public override void OnUpdate(float deltaTime)
        {
            if (Time.ElapsedTime < nextLog) return;
            nextLog = Time.ElapsedTime + interval;

            if (ReferenceEquals(player, null) || !player.IsAlive) player = Entity.Find(playerName);
            if (ReferenceEquals(player, null)) { Log.Warning("[NavProbe] no entity called " + playerName); return; }
            Vector3 p = player.Transform.WorldPosition;

            // Once: the Navigation queries against answers known in advance.
            if (!queriesChecked && Time.ElapsedTime > 1f)
            {
                queriesChecked = true;
                Vector3 far = p + new Vector3(100000f, 0f, 0f);
                Log.Info("[NavProbe] queries (expect True False True False): walkable(player)=" + Navigation.IsWalkable(p)
                         + " walkable(far off the surface)=" + Navigation.IsWalkable(far)
                         + " clear(player -> 2 m beside)=" + Navigation.IsPathClear(p, p + new Vector3(2f, 0f, 0f))
                         + " clear(player -> far off)=" + Navigation.IsPathClear(p, far));

                RaycastHit down;
                bool floor = Physics.SphereCast(p + Vector3.Up * 5f, 0.25f, Vector3.Up * -1f, out down, 20f,
                                                LayerMask.GetMask(PhysicsLayer.NonMoving), player);
                Log.Info("[NavProbe] physics (expect True True False): sphereCast(down onto floor)=" + floor
                         + (floor ? " at y=" + down.Point.Y.ToString("F2") : "")
                         + " checkSphere(player, Player layer)=" + Physics.CheckSphere(p + Vector3.Up, 1f, LayerMask.GetMask(PhysicsLayer.Player))
                         + " checkSphere(50 m up)=" + Physics.CheckSphere(p + Vector3.Up * 50f, 1f));
            }

            CombatHealth playerHealth = CombatHealth.Find(player);
            Log.Info("[NavProbe] t=" + Time.ElapsedTime.ToString("F1") + " player=(" + p.X.ToString("F1") + "," + p.Z.ToString("F1") + ")"
                     + " hp=" + (playerHealth != null ? playerHealth.CurrentHealth.ToString("F0") : "-"));
            for (int i = 0; i < Enemy.All.Count; ++i)
            {
                Enemy enemy = Enemy.All[i];
                Entity e = enemy.Entity;
                if (!e.IsActive) continue;

                Vector3 here = e.Transform.WorldPosition;
                FlowFieldAgent agent = e.GetComponent<FlowFieldAgent>();

                // The nearest other enemy, flat.
                float nearest = float.PositiveInfinity;
                for (int j = 0; j < Enemy.All.Count; ++j)
                {
                    if (j == i || !Enemy.All[j].Entity.IsActive) continue;
                    nearest = Math.Min(nearest, Flat(Enemy.All[j].Entity.Transform.WorldPosition - here));
                }

                // How far its facing is off the player, in degrees. The agent turns
                // +Z towards where it goes; Transform.Forward is -Z, hence * -1.
                Vector3 facing = e.Transform.Forward * -1f;
                Vector3 toPlayer = p - here;
                float fl = Flat(facing), tl = Flat(toPlayer);
                float faceError = (fl > 1e-4f && tl > 1e-4f)
                    ? (float)(Math.Acos(Math.Max(-1.0, Math.Min(1.0, (facing.X * toPlayer.X + facing.Z * toPlayer.Z) / (fl * tl)))) * 180.0 / Math.PI)
                    : 0f;

                Log.Info("[NavProbe]   " + e.Name + "#" + e.Index
                         + " face=" + faceError.ToString("F0")
                         + " action=\"" + enemy.CurrentAction + "\""
                         + " cd=" + enemy.CooldownRemaining.ToString("F1")
                         + (enemy.AttackInProgress ? " ATTACKING" : "")
                         + " dist=" + Flat(p - here).ToString("F2")
                         + " speed=" + (agent != null ? Flat(agent.Velocity).ToString("F2") : "-")
                         + " arrived=" + (agent != null && agent.HasArrived)
                         + " nearestEnemy=" + (float.IsInfinity(nearest) ? "-" : nearest.ToString("F2"))
                         + " pos=(" + here.X.ToString("F1") + "," + here.Y.ToString("F2") + "," + here.Z.ToString("F1") + ")"
                         // A dynamic golem's real velocity, as physics left it (speed= above also
                         // counts steering not yet used by a physics step), and its fall speed.
                         + (e.HasComponent<RigidBody>() && !e.RigidBody.IsKinematic
                            ? " body=" + Flat(e.RigidBody.Velocity).ToString("F2") + " vy=" + e.RigidBody.Velocity.Y.ToString("F2") : "")
                         // Is its brain (StateGraphAgent) switched on? A sleeping enemy reads "off".
                         + (e.HasComponent<StateGraphAgent>() ? " brain=" + (e.IsEnabled<StateGraphAgent>() ? "on" : "off") : ""));
            }
        }

        static float Flat(Vector3 v) { return (float)Math.Sqrt(v.X * v.X + v.Z * v.Z); }
    }
}
