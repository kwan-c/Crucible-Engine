/*************************************************************************
* \file:         WaveCycle.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         One wave's spend-and-refill clock, as plain data (no engine
*                calls), so it can be tested alone.
*
* \details
*    Ported from the Unity prototype (AI/Spawning/WaveCycle.cs), unchanged.
*    A wave has a resource (ResourceTotal). Every SpawnInterval seconds
*    Tick says "try a spawn"; the caller reports back:
*      Spent(cost)     an enemy appeared and cost that much
*      Skipped()       nothing this time (no free spot, at the cap, ...)
*      OutOfResource() nothing affordable: TimerReset counts down, then
*                      the resource refills and spawning carries on
*    ClearedRefill shortens the wait once per refill when the wave's
*    enemies are all gone (WaveSettings.ClearedTimerReset).
*    Pick is the weighted choice among the enemies still affordable.
*
* \history
*    -v1.0       ported from Unity
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class WaveCycle
    {
        public float SpawnInterval { get; private set; }
        public float TimerReset { get; private set; }
        public int ResourceTotal { get; private set; }
        public int Resource { get; private set; }
        public bool Refilling { get; private set; }
        public float ResetRemaining { get; private set; }
        // True once the current refill has been shortened by ClearedRefill.
        public bool ClearedThisRefill { get; private set; }

        readonly int baseResourceTotal;
        float untilNextSpawn;

        public WaveCycle(float spawnInterval, float timerReset, int resourceTotal)
        {
            SpawnInterval = Math.Max(0.01f, spawnInterval);
            TimerReset = Math.Max(0f, timerReset);
            ResourceTotal = baseResourceTotal = Math.Max(1, resourceTotal);
            Restart();
        }

        // Scales the wave's resource (e.g. 1.25 = 25% more enemies) from its next refill on.
        public void ScaleTotal(float scale)
        {
            ResourceTotal = Math.Max(1, (int)Math.Round(baseResourceTotal * Math.Max(0f, scale)));
        }

        // Full resource, first spawn straight away.
        public void Restart()
        {
            Resource = ResourceTotal;
            Refilling = false;
            ResetRemaining = 0f;
            untilNextSpawn = 0f;
        }

        // Advances time. True when it's time to try spawning one enemy.
        public bool Tick(float deltaTime)
        {
            deltaTime = Math.Max(0f, deltaTime);
            if (Refilling)
            {
                ResetRemaining -= deltaTime;
                if (ResetRemaining > 0f) return false;
                Resource = ResourceTotal;
                Refilling = false;
                untilNextSpawn = 0f;
            }
            untilNextSpawn -= deltaTime;
            return untilNextSpawn <= 0f;
        }

        public void Spent(int cost)
        {
            Resource = Math.Max(0, Resource - Math.Max(0, cost));
            untilNextSpawn = SpawnInterval;
        }

        public void Skipped() { untilNextSpawn = SpawnInterval; }

        // Gives back a removed member's cost (never above the total), so it can be spent again.
        public void Refund(int cost) { Resource = Math.Min(ResourceTotal, Resource + Math.Max(0, cost)); }

        public void OutOfResource()
        {
            Refilling = true;
            ResetRemaining = TimerReset;
            ClearedThisRefill = false;
        }

        // The wave's enemies were all cleared during its refill: the wait restarts at this many
        // seconds from now. Once per refill (so it doesn't keep resetting while you wait);
        // does nothing outside a refill.
        public void ClearedRefill(float seconds)
        {
            if (!Refilling || ClearedThisRefill) return;
            ResetRemaining = Math.Max(0f, seconds);
            ClearedThisRefill = true;
        }

        // Weighted pick among the entries the resource can still pay for; -1 when none can be
        // afforded. random01 is a number from 0 up to (not including) 1.
        public static int Pick(IList<int> costs, IList<float> weights, int resource, float random01)
        {
            float total = 0f;
            for (int i = 0; i < costs.Count; i++)
                if (Affordable(costs[i], weights[i], resource)) total += weights[i];
            if (total <= 0f) return -1;
            float roll = Math.Max(0f, Math.Min(random01, 0.999999f)) * total;
            for (int i = 0; i < costs.Count; i++)
            {
                if (!Affordable(costs[i], weights[i], resource)) continue;
                if (roll < weights[i]) return i;
                roll -= weights[i];
            }
            return -1;
        }

        static bool Affordable(int cost, float weight, int resource) { return cost > 0 && cost <= resource && weight > 0f; }
    }
}
