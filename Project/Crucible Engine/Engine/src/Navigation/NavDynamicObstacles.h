/*************************************************************************
* \file:         NavDynamicObstacles.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          19 November 2026
* \version       1.0
* \brief
*                NavDynamicObstacles: keeps the grid's dynamic layer in step
*                with where the DynamicObstacles really are.
*
* \details
*                Build step 4c, Unity's NavMeshObstacle with Carve on. For
*                each DynamicObstacle entity it remembers the cells it last
*                stamped (its footprint). Update() is handed every
*                DynamicObstacle's box as it is now, and for each one:
*                  - same cells as last time: nothing (the move threshold:
*                    an obstacle must cover different cells to count)
*                  - different cells (moved, or rose above / sank below the
*                    height band): unstamp the old, stamp the new
*                  - not seen before (spawned or tagged mid-Play): stamp it
*                  - no longer handed over (destroyed, untagged, no
*                    collider): unstamp it and forget it
*                Every cell that went open <-> blocked is reported, for the
*                cost field (FlowField::UpdateCosts) and the flow.
*
*                Never sees the World: ECSNavigationSystem reads the scene
*                and hands over plain boxes. Entity is only kept as a key.
*
*                Not exported (no ENGINE_API), like NavGrid.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <unordered_map>
#include <vector>

// Crucible engine
#include "ECS/Core/ECSCore.h"   // Entity
#include "Navigation/NavGrid.h"

namespace Crucible {

    class NavTerrain;

    // One DynamicObstacle as it is now: which entity, and its world box.
    struct NavDynamicObstacle {

        Entity      entity;
        NavObstacle box;

    };  // NAVDYNAMICOBSTACLE STRUCT

    class NavDynamicObstacles {

    public:

        /*!
            \brief
            Right after a bake, which has already stamped these same boxes
            (NavGrid::Bake stamps dynamic obstacles into the count): only
            remembers each one's footprint, so the next Update compares
            against it. Stamps nothing, so nothing is counted twice.
        */
        void Adopt(const std::vector<NavDynamicObstacle>& current, const NavGrid& grid, const NavTerrain& ground);

        /*!
            \brief
            Brings the grid's dynamic layer up to date with `current` (every
            DynamicObstacle now; see the file's details).

            \param changed
            Cleared, then filled with the cells that went open <-> blocked.
            May hold a cell twice; FlowField::UpdateCosts doesn't mind.
        */
        void Update(const std::vector<NavDynamicObstacle>& current, NavGrid& grid, const NavTerrain& ground,
                    std::vector<int>& changed);

        // Forgets every obstacle without touching the grid (the grid was cleared or rebaked).
        void Clear() { m_entries.clear(); }

        std::size_t Count() const { return m_entries.size(); }

    private:

        struct Entry {

            std::vector<int> cells;           // what it stamped last
            bool             seen = false;    // handed over in this Update

        };  // ENTRY STRUCT

        static std::uint64_t Key(Entity entity) {
            return (static_cast<std::uint64_t>(entity.generation) << 32) | entity.index;
        }

        std::unordered_map<std::uint64_t, Entry> m_entries;
        std::vector<int>                         m_cells;   // one obstacle's new footprint (memory reused)

    };  // NAVDYNAMICOBSTACLES CLASS

}   // NAMESPACE CRUCIBLE
