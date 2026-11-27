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
*    enemy prefab's spawnCost. An exhausted budget
*    refills resetDelay seconds later.
*
*    Inspector fields: the two group entities, the two spawn points (any
*    entities; enemies appear at their position) and each group's eligible prefabs.
*
**************************************************************************/
using System;
using System.Collections.Generic;

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
        public Prefab secondSmallPrefab;
        public Prefab thirdSmallPrefab;
        public Prefab fourthSmallPrefab;
        public Prefab secondElitePrefab;

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
        sealed class Choice { public Prefab Prefab; public int Cost; }
        readonly List<Choice> small = new List<Choice>(), elites = new List<Choice>();
        readonly List<Entity> living = new List<Entity>();
        int smallIndex, eliteIndex;
        Entity pending;
        Choice pendingChoice;
        int pendingGroup;
        float pendingAt;
        public int ResourceRemaining { get { return resource; } }
        public int ActiveCount { get { return living.Count; } }
        public override void OnCreate()
        {
            foreach (var prefab in new[] { smallEnemyPrefab, secondSmallPrefab, thirdSmallPrefab, fourthSmallPrefab })
                if (prefab != null) small.Add(new Choice { Prefab = prefab });
            foreach (var prefab in new[] { eliteEnemyPrefab, secondElitePrefab })
                if (prefab != null) elites.Add(new Choice { Prefab = prefab });
        }

        public override void OnUpdate(float deltaTime)
        {
            int groups = (IsOn(smallEnemies) ? 1 : 0) | (IsOn(eliteEnemies) ? 2 : 0);
            float now = Time.ElapsedTime;
            if (pending != null) ResolveSpawn(groups, now);
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
            if (pending != null) return;

            if (refillAt >= 0f && now >= refillAt) { resource = resourcePerCycle; refillAt = -1f; }
            if (now < nextSpawn) return;
            nextSpawn = now + Math.Max(.05f, spawnInterval);
            for (int i = living.Count - 1; i >= 0; i--)
                if (!living[i].IsActive || living[i].GetComponent<Enemy>()?.IsDead == true) living.RemoveAt(i);
            if (groups == 0 || living.Count >= maximumAlive) return;

            // Alternate between the two kinds when both are on (Unity picks by equal weights).
            bool elite = groups == 2 || (groups == 3 && eliteNext);
            eliteNext = !eliteNext;
            Choice choice = Pick(elite);
            if (choice == null && groups == 3) { elite = !elite; choice = Pick(elite); }
            if (choice == null)
            {
                if (refillAt < 0f) refillAt = now + resetDelay;
                return;
            }
            Spawn(choice, elite, groups, now);
        }

        Choice Pick(bool elite)
        {
            var choices = elite ? elites : small;
            int index = elite ? eliteIndex : smallIndex;
            for (int i = 0; resource > 0 && i < choices.Count; i++)
            {
                Choice choice = choices[index++ % choices.Count];
                if (choice.Cost > resource) continue;
                if (elite) eliteIndex = index % choices.Count; else smallIndex = index % choices.Count;
                return choice;
            }
            return null;
        }

        void Spawn(Choice choice, bool elite, int groups, float now)
        {
            Entity point = elite ? eliteSpawnPoint : smallSpawnPoint;
            if (point == null) return;
            // A small ring around the spawner so a crowd does not spawn inside itself.
            spawned++;
            float angle = spawned * 2.39996f;   // golden angle
            Vector3 offset = new Vector3((float)System.Math.Cos(angle), 0f, (float)System.Math.Sin(angle)) * spawnScatter;
            Vector3 position = point.Transform.WorldPosition + offset;
            RaycastHit floor;
            if (!Physics.Raycast(position + Vector3.Up * 3f, Vector3.Up * -1f, out floor, 8f,
                                 Enemy.MovementQueryMask) || floor.Normal.Y < .7f) return;
            position.Y = floor.Point.Y + 1.2f;
            pending = choice.Prefab.Instantiate(position, Quaternion.Identity);
            if (pending == null) return;
            pendingChoice = choice;
            pendingGroup = groups;
            pendingAt = now;
            if (pending.HasComponent<StateGraphAgent>()) pending.SetEnabled<StateGraphAgent>(false);
        }

        // Prefab scripts start on the following update. A provisional spawn cannot run
        // its graph until the actual authored cost is available and paid.
        void ResolveSpawn(int groups, float now)
        {
            var enemy = pending.GetComponent<Enemy>();
            bool legacy = enemy == null && pending.GetComponent<ZooEnemy>() != null;
            if (pending.IsAlive && enemy == null && !legacy && now - pendingAt < .5f) return;
            if (enemy != null) pendingChoice.Cost = Math.Max(1, enemy.spawnCost);
            if (legacy) pendingChoice.Cost = Math.Max(1, elites.Contains(pendingChoice) ? eliteCost : smallCost);
            if (pending.IsAlive && (enemy != null || legacy) && groups == pendingGroup && resource >= pendingChoice.Cost)
            {
                resource -= pendingChoice.Cost;
                living.Add(pending);
                if (enemy != null) { enemy.Wake(); pending.SetEnabled<StateGraphAgent>(true); }
            }
            else if (pending.IsAlive) Destroy(pending);
            pending = null;
        }

        static bool IsOn(Entity group) { return group != null && group.IsAlive && group.IsActiveSelf; }
        static string Describe(int groups) { return groups == 3 ? "small and elite enemies" : groups == 1 ? "small enemies" : "elite enemies"; }
    }
}
