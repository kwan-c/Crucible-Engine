/*************************************************************************
* \file:         TerrainVolume.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                TerrainVolume: rock and air in a box of samples, for ground
*                a heightfield can't hold (caves, overhangs).
*
* \details
*                Plain data next to HeightGrid, for the same reason: the
*                Terrain component draws it and physics collides with it,
*                and neither should need the other (or the ECS) to do so.
*                Its surface, as triangles, is Terrain/VolumeSurface.h.
*
* \history
*    -v1.0       moved here from Terrain/TerrainComponents.h (v1.2)
*
**************************************************************************/
#pragma once

// STL
#include <cmath>     // std::isfinite, std::round, std::abs
#include <cstddef>
#include <cstdint>
#include <vector>

// Crucible engine
#include "Math/HeightGrid.h"
#include "Math/Math.h"

namespace Crucible {

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

}   // NAMESPACE CRUCIBLE
