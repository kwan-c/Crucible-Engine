using System;
using Crucible;

namespace Crucible.Scripts
{
    // Port of Gam3333's EnemySpawnerManager. Fixed cadence: a new wave starts every
    // waveInterval seconds, timed from the START of the previous wave, not "when it is
    // cleared". Within a wave, enemies trickle in spawnStagger seconds apart.
    //
    // Spawn points: this entity's CHILDREN (empty entities placed round the level); one is
    // picked at random per enemy. Unity used a Transform[] field, but Crucible script fields
    // can't be lists yet. No children = spawn on this entity itself.
    public sealed class EnemySpawner : EntityScript
    {
        public Prefab enemyPrefab;
        public float waveInterval = 30f;
        public int enemiesPerWave = 10;
        public float spawnStagger = 0.5f;
        public bool spawnFirstWaveImmediately = true;

        static readonly Random random = new Random();

        int currentWave, spawnedThisWave, totalSpawned;
        float waveStartTime, nextWaveTime, nextSpawnTime;
        bool waveInProgress;

        public override void OnCreate()
        {
            if (enemyPrefab == null)
                Log.Warning("[EnemySpawner] " + Entity.Name + ": no Enemy Prefab assigned - nothing will spawn.");

            float now = Time.ElapsedTime;
            nextWaveTime = spawnFirstWaveImmediately ? now : now + waveInterval;
        }

        public override void OnUpdate(float deltaTime)
        {
            float now = Time.ElapsedTime;

            if (!waveInProgress && now >= nextWaveTime)
                BeginWave(now);

            if (waveInProgress && now >= nextSpawnTime && spawnedThisWave < enemiesPerWave)
            {
                SpawnOne();
                spawnedThisWave++;
                nextSpawnTime = now + spawnStagger;

                if (spawnedThisWave >= enemiesPerWave)
                {
                    waveInProgress = false;
                    // From when THIS wave started, so a slow stagger never pushes every
                    // later wave further back.
                    nextWaveTime = waveStartTime + waveInterval;
                }
            }
        }

        void BeginWave(float now)
        {
            currentWave++;
            waveStartTime = now;
            waveInProgress = true;
            spawnedThisWave = 0;
            nextSpawnTime = now;              // the wave's first enemy comes at once
            Log.Info("[EnemySpawner] " + Entity.Name + ": wave " + currentWave + " (" + totalSpawned + " spawned so far)");
        }

        void SpawnOne()
        {
            if (enemyPrefab == null) return;

            Entity point = PickRandomChild();
            Transform where = point != null ? point.Transform : Transform;
            enemyPrefab.Instantiate(where.WorldPosition, where.WorldRotation);
            totalSpawned++;
        }

        Entity PickRandomChild()
        {
            int count = Transform.ChildCount;
            return count > 0 ? Transform.GetChild(random.Next(count)) : null;
        }
    }
}
