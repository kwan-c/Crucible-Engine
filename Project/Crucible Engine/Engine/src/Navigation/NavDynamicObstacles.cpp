/*************************************************************************
* \file:         NavDynamicObstacles.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          19 November 2026
* \version       1.0
* \brief
*                Re-stamps the DynamicObstacles whose cells changed.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "NavDynamicObstacles.h"

namespace Crucible {

    void NavDynamicObstacles::Adopt(const std::vector<NavDynamicObstacle>& current, const NavGrid& grid, const NavTerrain& ground) {

        m_entries.clear();
        for (const NavDynamicObstacle& obstacle : current) {
            Entry& entry = m_entries[Key(obstacle.entity)];
            grid.Footprint(obstacle.box, ground, entry.cells);   // the same cells the bake stamped
        }

    }   // NavDynamicObstacles::Adopt()

    void NavDynamicObstacles::Update(const std::vector<NavDynamicObstacle>& current, NavGrid& grid, const NavTerrain& ground,
                                     std::vector<int>& changed) {

        changed.clear();
        for (auto& [key, entry] : m_entries) entry.seen = false;

        for (const NavDynamicObstacle& obstacle : current) {

            grid.Footprint(obstacle.box, ground, m_cells);

            const auto found = m_entries.find(Key(obstacle.entity));
            if (found == m_entries.end()) {
                // New: spawned, or tagged DynamicObstacle, since the last look.
                grid.AddDynamic(m_cells, &changed);
                Entry& entry = m_entries[Key(obstacle.entity)];
                entry.cells  = m_cells;
                entry.seen   = true;
                continue;
            }

            Entry& entry = found->second;
            entry.seen   = true;
            if (entry.cells == m_cells) continue;    // still covers the same cells: nothing to do

            grid.RemoveDynamic(entry.cells, &changed);
            grid.AddDynamic(m_cells, &changed);
            entry.cells.swap(m_cells);

        }

        // Gone: destroyed, untagged, or its collider removed or made a trigger.
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (it->second.seen) { ++it; continue; }
            grid.RemoveDynamic(it->second.cells, &changed);
            it = m_entries.erase(it);
        }

    }   // NavDynamicObstacles::Update()

}   // NAMESPACE CRUCIBLE
