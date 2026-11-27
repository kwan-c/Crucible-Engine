/*************************************************************************
* \file:         WaveSettings.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         One wave's settings as plain data (Unity's WaveData asset).
*
* \details
*    Who fills this in is kept separate from who uses it: today the Wave
*    script (fixed enemy slots in the Inspector) builds one; when the
*    Inspector gets list fields only that script changes. The spawn rules
*    (WaveRun, WaveCycle) read nothing but this.
*      SpawnInterval       seconds between one spawn and the next
*      TimerReset          seconds to wait after the resource runs out
*      ClearedTimerReset   if every enemy of the wave is gone during the
*                          wait, the next wave comes this many seconds
*                          later instead (0 = off)
*      ResourceTotal       resource at full; each enemy spends its cost
*      PausesRoaming       objective waves: roaming pauses while it runs
*      MaxConcurrent       most of this wave's own enemies alive at once
*                          (0 = no limit)
*      LeashDistance       members further than this from the owner's
*                          anchor are removed and refunded (0 = off)
*      Enemies             what it can spawn, each with a relative weight
*                          and its cost (EnemyTable)
*
* \history
*    -v1.0       Unity's WaveData fields
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class WaveEnemy
    {
        public Prefab Prefab;      // what to spawn (null in offline tests)
        public string Name;        // the prefab's name, for logs and the cost lookup
        public float Weight;       // chance relative to the wave's other enemies
        public int Cost;           // from EnemyTable; 0 = never spawned
    }

    public sealed class WaveSettings
    {
        public string Name = "Wave";
        public float SpawnInterval = 0.5f;
        public float TimerReset = 120f;
        public float ClearedTimerReset = 0f;
        public int ResourceTotal = 50;
        public bool PausesRoaming = true;
        public int MaxConcurrent = 0;
        public float LeashDistance = 0f;
        public readonly List<WaveEnemy> Enemies = new List<WaveEnemy>();

        // Adds an enemy entry, its cost looked up in EnemyTable by name.
        public WaveEnemy Add(Prefab prefab, string name, float weight)
        {
            var e = new WaveEnemy { Prefab = prefab, Name = name ?? string.Empty, Weight = Math.Max(0f, weight), Cost = EnemyTable.CostOf(name) };
            Enemies.Add(e);
            return e;
        }
    }
}
