/*************************************************************************
* \file:         TerrainComponents.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.2
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
*    -v1.2       Optional cave volume (TerrainGrid::volume): rock and air
*                in a box, replacing the heightfield cells under it
*
**************************************************************************/
#pragma once

// STL
#include <cmath>     // std::isfinite, std::round, std::abs
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

    // Ground a heightfield can't hold - caves, overhangs - as a box of rock
    // and air: a density per sample, above 0 rock, below 0 air. The surface
    // is where it crosses 0 (marching cubes). In the grid's own space: sample
    // (x, y, z) sits at origin + (x, y, z) * voxelSize.
    //
    // It replaces the heightfield cells its footprint covers. To meet them
    // without a crack, the box starts and ends on heightfield corners, the
    // voxel size divides the cell size, and the samples along the box's sides
    // are the heightfield's own ground there (height - y) - the generator's
    // job. No volume: count 0.
    struct TerrainVolume {

        Math::vec3         origin    = Math::vec3(0.0f);
        float              voxelSize = 1.0f;
        std::uint32_t      countX = 0, countY = 0, countZ = 0;   // samples per axis
        std::vector<float> density;                              // countX * countY * countZ; x fastest, then z, then y

        bool IsEmpty() const { return countX == 0; }
        std::size_t Index(std::uint32_t x, std::uint32_t y, std::uint32_t z) const {
            return (static_cast<std::size_t>(y) * countZ + z) * countX + x;
        }
        float At(std::uint32_t x, std::uint32_t y, std::uint32_t z) const { return density[Index(x, y, z)]; }

        // The heightfield cells under the box, as [first, last) along x and z.
        void Footprint(const HeightGrid& grid, std::uint32_t& x0, std::uint32_t& x1, std::uint32_t& z0, std::uint32_t& z1) const {
            const float step = voxelSize / grid.cellSize;
            x0 = static_cast<std::uint32_t>(std::round((origin.x - grid.MinX()) / grid.cellSize));
            z0 = static_cast<std::uint32_t>(std::round((origin.z - grid.MinZ()) / grid.cellSize));
            x1 = x0 + static_cast<std::uint32_t>(std::round(static_cast<float>(countX - 1) * step));
            z1 = z0 + static_cast<std::uint32_t>(std::round(static_cast<float>(countZ - 1) * step));
        }

        bool Covers(const HeightGrid& grid, std::uint32_t cellX, std::uint32_t cellZ) const {
            if (IsEmpty()) return false;
            std::uint32_t x0, x1, z0, z1;
            Footprint(grid, x0, x1, z0, z1);
            return cellX >= x0 && cellX < x1 && cellZ >= z0 && cellZ < z1;
        }

        // Sizes agree, values are real, and the box lines up with the grid as above.
        bool IsValid(const HeightGrid& grid) const {
            if (IsEmpty()) return countY == 0 && countZ == 0 && density.empty();
            if (countX < 2 || countY < 2 || countZ < 2) return false;
            if (density.size() != static_cast<std::size_t>(countX) * countY * countZ) return false;
            if (!(voxelSize > 0.0f) || !std::isfinite(voxelSize)) return false;
            if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return false;
            for (float d : density) if (!std::isfinite(d)) return false;

            const auto whole = [](float v) { return std::abs(v - std::round(v)) < 1e-3f; };
            const float perCell = grid.cellSize / voxelSize;                  // voxels per cell: a whole number
            if (!whole(perCell) || std::round(perCell) < 1.0f) return false;
            const float gx = (origin.x - grid.MinX()) / grid.cellSize, gz = (origin.z - grid.MinZ()) / grid.cellSize;
            if (!whole(gx) || !whole(gz)) return false;                       // starts on a corner
            if (!whole(static_cast<float>(countX - 1) / perCell) || !whole(static_cast<float>(countZ - 1) / perCell)) return false;
            std::uint32_t x0, x1, z0, z1;                                       // ends on a corner, on the grid
            Footprint(grid, x0, x1, z0, z1);
            return gx >= -1e-3f && gz >= -1e-3f && x1 <= grid.cols && z1 <= grid.rows && x1 > x0 && z1 > z0;
        }

    };  // TERRAINVOLUME STRUCT

    // One built terrain: its heights, what each cell is, and an optional cave
    // volume. Immutable once built, and shared (by the component, physics and
    // the renderer's record).
    struct TerrainGrid {

        HeightGrid                heights;
        std::vector<std::uint8_t> regions;   // heights.cols * heights.rows cells, row-major, one TerrainRegion each
        TerrainVolume             volume;    // rock and air the heightfield can't hold (caves); often empty

        bool IsValid() const {
            if (!heights.IsValid()) return false;
            if (regions.size() != static_cast<std::size_t>(heights.cols) * heights.rows) return false;
            for (std::uint8_t region : regions)
                if (region >= static_cast<std::uint8_t>(TerrainRegion::Count)) return false;
            return volume.IsValid(heights);
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
