/*************************************************************************
* \file:         NetEnemySpawner.cs
* \author:       EnemySpawner (the original): the enemy team's port of
*                Gam3333's EnemySpawnerManager;
*                Abhishek Akilandeswari Patchamuthu (this networked copy)
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: a copy of EnemySpawner for the
*                combat region. The host spawns network golems (every
*                machine sees them) that go for whichever player is
*                nearest, once that player is within detectRadius.
*
* \details
*    On "Combat Spawner" in net_gameplay_test.scene (over the +X pad); its
*    children are the spawn points. Changes from EnemySpawner (which is
*    unchanged):
*      - host only (Network.IsAuthority), with Network.Spawn: a golem is a
*        network object (Net Golem prefabs: Enemy + NetEnemyView)
*      - waits for the match to start (NetMatchState says Playing)
*      - at most maxAlive golems at once; a wave tops up to that
*      - rangedPrefab: a ranged golem instead, rangedPercent of the time
*      - every golem spawned here is woken (Enemy.Wake), in case its
*        prefab starts asleep (the tutorial's golems do)
*      - each golem's awareness is set here (Enemy's own fields):
*            detectRadius  it notices the nearest player this close
*            loseRadius    ...and gives up on it beyond this
*        The nearest player is the engine's choice (every golem's "Target"
*        is the nearest PlayerController, and every mech has one).
*
* \history
*    -v1.0       copy of EnemySpawner: network spawns, match start, cap,
*                awareness radii
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetEnemySpawner : EntityScript
    {
        public Prefab enemyPrefab;
        public Prefab rangedPrefab;              // optional: a ranged golem instead...
        public float rangedPercent = 30f;        // ...this often
        public float waveInterval = 30f;
        public int enemiesPerWave = 4;
        public float spawnStagger = 0.5f;
        public bool spawnFirstWaveImmediately = true;
        public int maxAlive = 6;

        // Awareness of the golems spawned here (Enemy.detectRadius / loseRadius).
        public float detectRadius = 25f;         // goes for the nearest player once it is this close
        public float loseRadius = 40f;           // gives up beyond this (keep it above detectRadius)

        static readonly Random random = new Random();

        int currentWave, spawnedThisWave, totalSpawned;
        float waveStartTime, nextWaveTime, nextSpawnTime;
        bool waveInProgress, started;
        readonly List<Entity> alive = new List<Entity>();
        readonly List<Entity> unconfigured = new List<Entity>();   // spawned; their Enemy is set up next frame

        public override void OnCreate()
        {
            if (enemyPrefab == null)
                Log.Warning("[NetEnemySpawner] " + Entity.Name + ": no Enemy Prefab assigned - nothing will spawn.");
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!Network.IsAuthority) return;   // the host spawns; everyone sees the golems
            Configure();
            if (!NetMatchState.MovementAllowed) return;

            float now = Time.ElapsedTime;
            if (!started)
            {
                started = true;
                nextWaveTime = spawnFirstWaveImmediately ? now : now + waveInterval;
            }
            for (int i = alive.Count - 1; i >= 0; i--)
            {
                CombatHealth health = alive[i].IsAlive ? CombatHealth.Find(alive[i]) : null;
                if (health == null || health.IsDead) alive.RemoveAt(i);
            }

            if (!waveInProgress && now >= nextWaveTime)
                BeginWave(now);

            if (waveInProgress && now >= nextSpawnTime && spawnedThisWave < enemiesPerWave)
            {
                if (alive.Count < maxAlive) SpawnOne();
                spawnedThisWave++;
                nextSpawnTime = now + spawnStagger;

                if (spawnedThisWave >= enemiesPerWave)
                {
                    waveInProgress = false;
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
            nextSpawnTime = now;
            Log.Info("[NetEnemySpawner] " + Entity.Name + ": wave " + currentWave + " (" + totalSpawned + " spawned so far, " + alive.Count + " alive)");
        }

        void SpawnOne()
        {
            Prefab prefab = rangedPrefab != null && random.NextDouble() * 100.0 < rangedPercent ? rangedPrefab : enemyPrefab;
            if (prefab == null) return;
            Entity point = PickRandomChild();
            Transform where = point != null ? point.Transform : Transform;
            Entity golem = Network.Spawn(prefab, where.WorldPosition, where.WorldRotation);
            if (golem == null) return;
            alive.Add(golem);
            unconfigured.Add(golem);
            totalSpawned++;
        }

        // A spawned golem's scripts exist from the next frame: then it gets this spawner's radii.
        void Configure()
        {
            for (int i = unconfigured.Count - 1; i >= 0; i--)
            {
                Entity golem = unconfigured[i];
                if (!golem.IsAlive) { unconfigured.RemoveAt(i); continue; }
                Enemy enemy = golem.GetComponent<Enemy>();
                if (enemy == null) continue;
                enemy.detectRadius = detectRadius;
                enemy.loseRadius = Math.Max(loseRadius, detectRadius);
                enemy.Wake();   // a golem prefab made for the tutorial starts asleep (brain off)
                unconfigured.RemoveAt(i);
            }
        }

        Entity PickRandomChild()
        {
            int count = Transform.ChildCount;
            return count > 0 ? Transform.GetChild(random.Next(count)) : null;
        }
    }
}
