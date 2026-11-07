/*************************************************************************
* \file:         ZooEnemyWaves.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.0
* \brief         Zoo enemy showcase: while the Small / Elite Enemies group
*                entities are switched on (by the ZooToggle floor switches),
*                spawns that kind of enemy at its spawner. Port of Unity's
*                ZooEnemyWaves + the SpawnDirector pacing it configures.
*
* \details
*    Unity's pacing: one spawn every spawnInterval seconds while fewer than
*    maximumAlive are alive and the cycle's resource budget can pay the
*    enemy's cost (small 2, elite 10 - EnemySpawnCost). An exhausted budget
*    refills resetDelay seconds later.
*
*    Inspector fields: the two group entities, the two spawn points (any
*    entities; enemies appear at their position) and the two enemy prefabs
*    (Assets/Prefabs/Zoo). The enemies themselves are ZooEnemy placeholders.
*
**************************************************************************/
namespace Crucible.Scripts
{
    public sealed class ZooEnemyWaves : EntityScript
    {
        public Entity smallEnemies;
        public Entity eliteEnemies;
        public Entity smallSpawnPoint;
        public Entity eliteSpawnPoint;
        public Prefab smallEnemyPrefab;
        public Prefab eliteEnemyPrefab;

        // Zoo showcase pacing (Unity's ZooEnemyWaves defaults)
        public float spawnInterval = 2f;
        public int maximumAlive = 8;
        public int resourcePerCycle = 50;
        public float resetDelay = 15f;
        public int smallCost = 2;
        public int eliteCost = 10;
        public float spawnScatter = 2.5f;   // metres around the spawn point

        int activeGroups = -1;
        int resource;
        float nextSpawn, refillAt = -1f;
        bool eliteNext;
        int spawned;

        public override void OnUpdate(float deltaTime)
        {
            int groups = (IsOn(smallEnemies) ? 1 : 0) | (IsOn(eliteEnemies) ? 2 : 0);
            float now = Time.ElapsedTime;
            if (groups != activeGroups)
            {
                // A new wave: fresh budget, first spawn right away. Living enemies stay (Unity's StopWave
                // leaves them too).
                activeGroups = groups;
                resource = resourcePerCycle;
                refillAt = -1f;
                nextSpawn = now;
                if (groups != 0) Log.Info("ZooEnemyWaves: wave running for " + Describe(groups) + ".");
            }
            if (groups == 0) return;

            if (refillAt >= 0f && now >= refillAt) { resource = resourcePerCycle; refillAt = -1f; }
            if (now < nextSpawn || ZooEnemy.AliveCount >= maximumAlive) return;

            // Alternate between the two kinds when both are on (Unity picks by equal weights).
            bool elite = groups == 2 || (groups == 3 && eliteNext);
            eliteNext = !eliteNext;
            int cost = elite ? eliteCost : smallCost;
            if (resource < cost)
            {
                if (refillAt < 0f) refillAt = now + resetDelay;
                return;
            }
            if (Spawn(elite)) { resource -= cost; nextSpawn = now + spawnInterval; }
            else nextSpawn = now + spawnInterval;   // misconfigured: retry later, not every frame
        }

        bool Spawn(bool elite)
        {
            Prefab prefab = elite ? eliteEnemyPrefab : smallEnemyPrefab;
            Entity point = elite ? eliteSpawnPoint : smallSpawnPoint;
            if (prefab == null || point == null) { Log.Warning("ZooEnemyWaves: missing " + (elite ? "elite" : "small") + " prefab or spawn point."); return false; }
            // A small ring around the spawner so a crowd does not spawn inside itself.
            spawned++;
            float angle = spawned * 2.39996f;   // golden angle
            Vector3 offset = new Vector3((float)System.Math.Cos(angle), 0f, (float)System.Math.Sin(angle)) * spawnScatter;
            Vector3 position = point.Transform.WorldPosition + offset;
            return prefab.Instantiate(position, Quaternion.Identity) != null;
        }

        static bool IsOn(Entity group) { return group != null && group.IsAlive && group.IsActiveSelf; }
        static string Describe(int groups) { return groups == 3 ? "small and elite enemies" : groups == 1 ? "small enemies" : "elite enemies"; }
    }
}
