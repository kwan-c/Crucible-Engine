/*************************************************************************
* \file:         ZooPlayerRespawn.cs
* \brief         Zoo-only test respawn: when the player mech is destroyed, it
*                comes back after respawnDelay seconds at a safe spawn spot,
*                away from the living enemies. Production missions have no
*                such entity; their death and failure flow is separate.
*
* \details
*    Put it on one entity in a test scene ("Zoo Player Respawn" in the Zoo).
*    While the player is dead its own update already refuses movement,
*    attacks and abilities (MechPlayer), and the enemies stop: a dead target
*    is not sensed, and pending hits on it are cancelled (Enemy). The player
*    entity itself is never destroyed, so no enemy keeps a dangling reference.
*
*    Spawn candidates, in order: the player's position when the scene started
*    (useStartPosition), then spawnPointA and spawnPointB (any entities; their
*    position and facing). The first candidate with no living enemy within
*    safeEnemyDistance wins; if none is that far, the one whose nearest enemy
*    is farthest away. Each candidate is dropped onto the ground below it
*    (the mech keeps the height above the ground it started with), and a
*    candidate with no ground below is skipped.
*
*    The respawn itself is MechPlayer.Respawn (MechRespawn.cs): it keeps the
*    current loadout, rearms it, restores health through the start-of-play
*    path and clears every temporary combat state.
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    public sealed class ZooPlayerRespawn : EntityScript
    {
        public string playerName = "Player";
        public float respawnDelay = 3f;            // seconds from death to respawn
        public float safeEnemyDistance = 18f;      // metres; a spot with no living enemy this close is safe
        public bool useStartPosition = true;
        public Entity spawnPointA;
        public Entity spawnPointB;

        MechPlayer player;
        float nextSearch;
        bool haveStart;
        Vector3 startPosition;
        float startYaw, rootAboveGround = 1f;
        float deadSince = -1f;

        /// <summary>Seconds until the pending respawn, or -1 when the player is alive.</summary>
        public float SecondsUntilRespawn { get { return deadSince < 0f ? -1f : Math.Max(0f, respawnDelay - (Time.ElapsedTime - deadSince)); } }
        public Vector3 LastSpawnPosition { get; private set; }

        public override void OnUpdate(float deltaTime)
        {
            if (player == null || !player.Entity.IsAlive)
            {
                player = null;
                if (Time.ElapsedTime < nextSearch) return;
                nextSearch = Time.ElapsedTime + .5f;
                Entity found = Entity.Find(playerName);
                player = found != null ? found.GetComponent<MechPlayer>() : null;
                if (player == null || player.Health == null) { player = null; return; }
            }
            if (!haveStart && !player.IsDead) RecordStart();

            if (!player.IsDead) { deadSince = -1f; return; }
            if (deadSince < 0f)
            {
                deadSince = Time.ElapsedTime;
                Log.Info("ZooPlayerRespawn: player destroyed, respawning in " + respawnDelay.ToString("0.0") + " s.");
            }
            if (Time.ElapsedTime - deadSince < Math.Max(0f, respawnDelay)) return;

            Vector3 position; float yaw;
            if (!ChooseSpawn(out position, out yaw)) return;   // nothing usable yet: try again next frame
            if (player.Respawn(position, yaw)) { LastSpawnPosition = position; deadSince = -1f; }
        }

        void RecordStart()
        {
            haveStart = true;
            startPosition = player.Transform.WorldPosition;
            Vector3 f = player.Transform.Forward;
            startYaw = (float)(Math.Atan2(-f.X, -f.Z) * 180.0 / Math.PI);
            Vector3 ground;
            if (Ground(startPosition, out ground)) rootAboveGround = Math.Max(0f, startPosition.Y - ground.Y);
        }

        bool ChooseSpawn(out Vector3 position, out float yaw)
        {
            position = Vector3.Zero; yaw = 0f;
            float bestDistance = -1f;
            bool found = false;
            for (int i = 0; i < 3; i++)
            {
                Vector3 point; float facing;
                if (!Candidate(i, out point, out facing)) continue;
                float nearest = NearestEnemy(point);
                if (nearest >= safeEnemyDistance) { position = point; yaw = facing; return true; }   // authored order wins when safe
                if (nearest > bestDistance) { bestDistance = nearest; position = point; yaw = facing; found = true; }
            }
            return found;
        }

        bool Candidate(int index, out Vector3 point, out float yaw)
        {
            point = Vector3.Zero; yaw = startYaw;
            Vector3 raw;
            if (index == 0)
            {
                if (!useStartPosition || !haveStart) return false;
                raw = startPosition;
            }
            else
            {
                Entity spot = index == 1 ? spawnPointA : spawnPointB;
                if (spot == null || !spot.IsAlive) return false;
                raw = spot.Transform.WorldPosition;
                Vector3 f = spot.Transform.Forward;
                yaw = (float)(Math.Atan2(-f.X, -f.Z) * 180.0 / Math.PI);
            }
            Vector3 ground;
            if (!Ground(raw, out ground)) return false;
            point = new Vector3(raw.X, ground.Y + rootAboveGround, raw.Z);
            return true;
        }

        // The level under a point (walls and props, never bodies), from a little above it.
        bool Ground(Vector3 from, out Vector3 ground)
        {
            RaycastHit hit;
            bool ok = Physics.Raycast(from + Vector3.Up * 2f, new Vector3(0f, -1f, 0f), out hit, 40f,
                                      Enemy.MovementQueryMask, player != null ? player.Entity : null);
            ground = ok ? hit.Point : from;
            return ok;
        }

        static float NearestEnemy(Vector3 point)
        {
            float nearest = float.PositiveInfinity;
            foreach (Enemy enemy in Enemy.All)
            {
                if (enemy == null || enemy.IsDead || !enemy.Entity.IsAlive || !enemy.Entity.IsActive) continue;
                Vector3 d = enemy.Position - point;
                nearest = Math.Min(nearest, (float)Math.Sqrt(d.X * d.X + d.Z * d.Z));
            }
            return nearest;
        }
    }
}
