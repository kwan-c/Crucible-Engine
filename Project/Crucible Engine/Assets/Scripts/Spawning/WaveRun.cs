/*************************************************************************
* \file:         WaveRun.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         One running wave: its settings, clock, roster and sources,
*                and the spawn rules, as plain data (no engine calls).
*
* \details
*    The per-wave half of Unity's SpawnDirector (ActiveWave, TrySpawn,
*    ShortenIfCleared, ApplyLeash, TryPickPose), pulled out so the rules
*    can be tested alone; the director (SpawnDirector.cs) only decides
*    which waves run and does the engine work through the delegates.
*    TrySpawn, in Unity's order:
*      1. drop members that died
*      2. at MaxConcurrent: skip this turn
*      3. weighted pick among affordable enemies; none: OutOfResource
*      4. a spot from a random usable source; none: skip
*      5. over the owner's budget (Liberation): skip
*      6. spawn, spend its cost, add it to the roster
*    T is the spawned thing (Entity in the game, a stand-in in tests).
*
* \history
*    -v1.0       ported from Unity's SpawnDirector
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class WaveRun<T> where T : class
    {
        public WaveSettings Settings { get; private set; }
        public WaveCycle Cycle { get; private set; }
        public WaveRoster<T> Roster { get; private set; }
        public object Owner { get; private set; }
        public int Budget = -1;     // most spawn cost alive at once (-1 = no limit), set by the owner
        public int TotalSpawned { get; private set; }

        readonly ISpawnSource[] sources;
        readonly int[] costs;
        readonly float[] weights;

        public WaveRun(WaveSettings settings, IList<ISpawnSource> spawnSources, object owner)
        {
            Settings = settings;
            Owner = owner;
            Roster = new WaveRoster<T>();
            Cycle = new WaveCycle(settings.SpawnInterval, settings.TimerReset, settings.ResourceTotal);
            sources = new ISpawnSource[spawnSources != null ? spawnSources.Count : 0];
            for (int i = 0; i < sources.Length; i++) sources[i] = spawnSources[i];
            costs = new int[settings.Enemies.Count];
            weights = new float[settings.Enemies.Count];
            for (int i = 0; i < costs.Length; i++)
            {
                costs[i] = settings.Enemies[i].Cost;
                weights[i] = settings.Enemies[i].Weight;
            }
        }

        // True when it's time to try a spawn (see TrySpawn).
        public bool Tick(float deltaTime) { return Cycle.Tick(deltaTime); }

        // One spawn attempt. spawn(entry, position, rotation) makes the enemy (null = it failed,
        // counted as a skip). Returns what was spawned, or null.
        public T TrySpawn(Random random, Func<T, bool> alive, Func<WaveEnemy, Vector3, Quaternion, T> spawn)
        {
            Roster.Prune(alive);
            if (Settings.MaxConcurrent > 0 && Roster.Count >= Settings.MaxConcurrent) { Cycle.Skipped(); return null; }
            int pick = WaveCycle.Pick(costs, weights, Cycle.Resource, (float)random.NextDouble());
            if (pick < 0) { Cycle.OutOfResource(); return null; }
            Vector3 position; Quaternion rotation;
            if (!TryPickPose(random, out position, out rotation)) { Cycle.Skipped(); return null; }
            if (Budget >= 0 && Roster.TotalCost + costs[pick] > Budget) { Cycle.Skipped(); return null; }
            T member = spawn(Settings.Enemies[pick], position, rotation);
            if (member == null) { Cycle.Skipped(); return null; }
            Cycle.Spent(costs[pick]);
            Roster.Add(member, costs[pick]);
            TotalSpawned++;
            return member;
        }

        // Every member gone while the wave refills: the next one comes ClearedTimerReset from now.
        public void ShortenIfCleared(Func<T, bool> alive)
        {
            if (Settings.ClearedTimerReset <= 0f || !Cycle.Refilling) return;
            Roster.Prune(alive);
            if (Roster.Count == 0) Cycle.ClearedRefill(Settings.ClearedTimerReset);
        }

        // Removes members further than the leash from anchor, refunding their cost. remove does
        // the removal (it isn't a death: no drops, no kill). Returns how many were removed.
        public int ApplyLeash(Vector3 anchor, Func<T, Vector3> positionOf, Func<T, bool> alive, Action<T> remove)
        {
            float leash = Settings.LeashDistance;
            if (leash <= 0f) return 0;
            Roster.Prune(alive);
            var culled = Roster.Cull(m => { Vector3 p = positionOf(m); return WaveLeash.Beyond(p.X - anchor.X, p.Z - anchor.Z, leash); });
            foreach (WaveMember<T> c in culled)
            {
                if (remove != null) remove(c.Member);
                Cycle.Refund(c.Cost);
            }
            return culled.Count;
        }

        // A spot from a random source, trying the others in turn if it has none.
        bool TryPickPose(Random random, out Vector3 position, out Quaternion rotation)
        {
            if (sources.Length > 0)
            {
                int start = random.Next(sources.Length);
                for (int step = 0; step < sources.Length; step++)
                {
                    ISpawnSource s = sources[(start + step) % sources.Length];
                    if (s != null && s.TryGetPose(random, out position, out rotation)) return true;
                }
            }
            position = Vector3.Zero;
            rotation = Quaternion.Identity;
            return false;
        }
    }
}
