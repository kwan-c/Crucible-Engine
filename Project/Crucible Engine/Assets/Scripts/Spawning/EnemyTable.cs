/*************************************************************************
* \file:         EnemyTable.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         What each enemy costs from a wave's resource: the one place
*                spawn costs are set.
*
* \details
*    Unity put an EnemySpawnCost component on each enemy prefab. Here the
*    costs live in one table (decided 10 Nov), keyed by the enemy prefab's
*    file name, so a wave slot holding a prefab finds its cost without the
*    enemy prefabs (teammates' assets) being changed.
*    Values are Unity's for now (small 1-2, elite 10, boss 20); to be tuned
*    to the GDD. An enemy not in the table costs 0, which waves treat as
*    unspawnable (with a warning), as Unity did for a missing cost.
*    Adding an enemy = one line in Entries. Renaming a prefab = rename it
*    here too.
*
* \history
*    -v1.0       Unity's costs for the six engine golems
*
**************************************************************************/
using System;

namespace Crucible.@Scripts
{
    public static class EnemyTable
    {
        public struct Entry
        {
            public string PrefabName;
            public int Cost;
            public Entry(string prefabName, int cost) { PrefabName = prefabName; Cost = cost; }
        }

        // Small 1-2, elite 10 (Unity: sword/mace/throwing axe 1, crossbow 2, elite cannon/crossbow 10).
        public static readonly Entry[] Entries =
        {
            new Entry("Golem Sword", 1),
            new Entry("Golem Mace", 1),
            new Entry("Golem Throwing Axe", 1),
            new Entry("Golem Crossbow", 2),
            new Entry("Golem Cannon", 10),
            new Entry("Golem Heavy Crossbow", 10),
        };

        // The cost of the prefab with this file name; 0 when it isn't in the table.
        public static int CostOf(string prefabName)
        {
            if (string.IsNullOrEmpty(prefabName)) return 0;
            foreach (Entry e in Entries)
                if (string.Equals(e.PrefabName, prefabName, StringComparison.Ordinal)) return e.Cost;
            return 0;
        }
    }
}
