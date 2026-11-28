/*************************************************************************
* \file:         Wave.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         One wave's settings in the Inspector (Unity's WaveData
*                asset), on a child of the Spawn Director prefab.
*
* \details
*    The Spawn Director prefab holds one child per wave (Wave Roaming,
*    Wave Capture, Wave Escort, Wave Liberation), so every mission scene
*    shares the same waves, as Unity's WaveData assets were shared; a
*    scene can still override a value on its prefab instance.
*    TEMPORARY LAYOUT: the Inspector has no list fields yet, so the enemy
*    list is 8 fixed slots (enemyN + weightN; an empty slot is unused).
*    When lists arrive, only these fields and Settings() change: the
*    director and the rules read the WaveSettings it returns.
*      resourceTotal       resource at full; each enemy spends its cost
*                          (EnemyTable, by prefab name)
*      spawnInterval       seconds between one spawn and the next
*      timerReset          seconds to wait once the resource runs out
*      clearedTimerReset   all of the wave's enemies gone during the wait:
*                          the next comes this many seconds later (0 = off)
*      maxConcurrent       most of this wave's enemies alive at once (0 = no limit)
*      leashDistance       members further than this from the owner's
*                          anchor are removed and refunded (0 = off)
*      pausesRoaming       objective waves: roaming pauses while it runs
*      enemyN / weightN    an enemy prefab and its chance relative to the
*                          others (like the part drop list)
*
* \history
*    -v1.0       Unity's WaveData fields; 8 enemy slots (no Inspector lists yet)
*
**************************************************************************/
using System;

namespace Crucible.@Scripts
{
    public sealed class Wave : EntityScript
    {
        public int resourceTotal = 50;
        public float spawnInterval = 0.5f;
        public float timerReset = 120f;
        public float clearedTimerReset = 0f;
        public int maxConcurrent = 0;
        public float leashDistance = 0f;
        public bool pausesRoaming = true;

        public Prefab enemy1;
        public float weight1 = 1f;
        public Prefab enemy2;
        public float weight2 = 1f;
        public Prefab enemy3;
        public float weight3 = 1f;
        public Prefab enemy4;
        public float weight4 = 1f;
        public Prefab enemy5;
        public float weight5 = 1f;
        public Prefab enemy6;
        public float weight6 = 1f;
        public Prefab enemy7;
        public float weight7 = 1f;
        public Prefab enemy8;
        public float weight8 = 1f;

        private bool warned;

        // This wave's settings as the spawn rules read them, built fresh each call (so an
        // Inspector change applies the next time the wave starts).
        public WaveSettings Settings()
        {
            var s = new WaveSettings
            {
                Name = Entity.Name,
                ResourceTotal = resourceTotal,
                SpawnInterval = spawnInterval,
                TimerReset = timerReset,
                ClearedTimerReset = clearedTimerReset,
                MaxConcurrent = maxConcurrent,
                LeashDistance = leashDistance,
                PausesRoaming = pausesRoaming,
            };
            Prefab[] prefabs = { enemy1, enemy2, enemy3, enemy4, enemy5, enemy6, enemy7, enemy8 };
            float[] weights = { weight1, weight2, weight3, weight4, weight5, weight6, weight7, weight8 };
            for (int i = 0; i < prefabs.Length; i++)
            {
                if (prefabs[i] == null) continue;
                WaveEnemy e = s.Add(prefabs[i], prefabs[i].Name, weights[i]);
                if (e.Cost <= 0 && !warned)
                    Log.Warning("Wave " + Entity.Name + ": " + e.Name + " is not in EnemyTable, so it never spawns (add its cost there)");
            }
            if (s.Enemies.Count == 0 && !warned) Log.Warning("Wave " + Entity.Name + ": no enemies set, so nothing spawns");
            warned = true;
            return s;
        }
    }
}
