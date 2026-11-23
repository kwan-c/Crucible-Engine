/*************************************************************************
* \file:         TerrainComponents.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.1
* \brief
*                Terrain: uneven ground built at runtime from a height grid,
*                Unity's Terrain (or a generated mesh plus MeshCollider).
*
* \details
*                A script (the level generator) builds the grid and hands it
*                over with Terrain::Build; the engine does the rest:
*                  - ECSTerrainSystem draws it, through the entity's
*                    MeshRenderer (one flat-shaded mesh, a colour per region);
*                  - ECSPhysicsSystem collides with it, through the entity's
*                    Collider set to HeightField;
*                  - ECSNavigationSystem sees it on its next bake, which a
*                    script asks for with Navigation.Rebake().
*
*                So a terrain entity is: Transform + Terrain + MeshRenderer +
*                Collider (HeightField, NonMoving, no RigidBody).
*
*                The grid is generated, never saved: only the colours are
*                scene data. Each Build makes a new, immutable TerrainGrid;
*                the systems spot a change by its pointer, so a rebuild
*                costs nothing until the ground really changes.
*
* \history
*    -v1.0       first version
*    -v1.1       TerrainRegion::Tunnel and m_tunnelColour (cave floors)
*
**************************************************************************/
#pragma once

// STL
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>   // std::move
#include <vector>

// Crucible engine
#include "ECS/Components/ComponentKind.h"
#include "Math/HeightGrid.h"
#include "Math/Math.h"

namespace Crucible {

    // What a cell of ground is, for how it is drawn (and later, footsteps or
    // friction). Unity's generator split its mesh into the same three.
    enum class TerrainRegion : std::uint8_t {

        Ground = 0,   // walkable ground
        Cliff  = 1,   // too steep to walk: a wall for players and enemies alike
        Road   = 2,   // a road the generator laid between chunks
        Tunnel = 3,   // a cave tunnel's floor (walkable; only Cliff is a wall)

        Count         // keep last

    };  // TERRAINREGION ENUM

    // One built terrain: its heights and what each cell is. Immutable once
    // built, and shared (by the component, physics and the renderer's record).
    struct TerrainGrid {

        HeightGrid                heights;
        std::vector<std::uint8_t> regions;   // heights.cols * heights.rows cells, row-major, one TerrainRegion each

        bool IsValid() const {
            if (!heights.IsValid()) return false;
            if (regions.size() != static_cast<std::size_t>(heights.cols) * heights.rows) return false;
            for (std::uint8_t region : regions)
                if (region >= static_cast<std::uint8_t>(TerrainRegion::Count)) return false;
            return true;
        }

    };  // TERRAINGRID STRUCT

    struct Terrain {

        // --- saved, set in the Inspector ------------------------------------ //
        // Multiplied with the MeshRenderer's material, so a plain (white) material
        // shows these exactly; a textured one is tinted by them.
        Math::vec3 m_groundColour = Math::vec3(0.42f, 0.55f, 0.30f);
        Math::vec3 m_cliffColour  = Math::vec3(0.46f, 0.43f, 0.40f);
        Math::vec3 m_roadColour   = Math::vec3(0.63f, 0.53f, 0.39f);
        Math::vec3 m_tunnelColour = Math::vec3(0.36f, 0.27f, 0.18f);

        // --- runtime only, never saved ---------------------------------------- //
        // The ground as last built. Null until a script builds it.
        std::shared_ptr<const TerrainGrid> m_grid;

        /*!
            \brief
            Replaces the ground. Drawn, collided with and (after a re-bake)
            navigated from the next frame.

            \return
            False if the grid is inconsistent (sizes that disagree, NaN
            heights, an unknown region); the old ground is kept.
        */
        bool Build(TerrainGrid grid) {
            if (!grid.IsValid()) return false;
            m_grid = std::make_shared<const TerrainGrid>(std::move(grid));
            return true;
        }

        // The heights alone, sharing ownership with the grid: what physics needs.
        std::shared_ptr<const HeightGrid> Heights() const {
            return m_grid ? std::shared_ptr<const HeightGrid>(m_grid, &m_grid->heights) : nullptr;
        }

    };  // TERRAIN STRUCT

    CRUCIBLE_COMPONENT(Terrain, Terrain)

}   // NAMESPACE CRUCIBLE
