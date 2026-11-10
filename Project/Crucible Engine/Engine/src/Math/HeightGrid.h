/*************************************************************************
* \file:         HeightGrid.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
* \brief
*                HeightGrid: a square-celled grid of corner heights, the one
*                description of uneven ground that rendering, physics and
*                navigation all read.
*
* \details
*                Plain data, no engine dependencies, so each system that
*                needs ground heights can use it without depending on the
*                others: the Terrain component builds its mesh from it,
*                physics turns it into a heightfield collider, and
*                navigation can sample it directly instead of raycasting.
*
*                Layout, in the grid's own space (the entity it sits on):
*                  - cols x rows CELLS, each cellSize metres square;
*                  - (cols + 1) x (rows + 1) CORNER heights, row-major,
*                    index = z * (cols + 1) + x;
*                  - centred on the origin: corner (0, 0) is at
*                    (-cols * cellSize / 2, h, -rows * cellSize / 2).
*
*                Every cell is two triangles split along the diagonal from
*                corner (x, z) to (x + 1, z + 1). Jolt's HeightFieldShape
*                splits its cells the same way, so the collider and the
*                rendered mesh are the same surface.
*
* \history
*    -v1.0       first version: corners, centring, triangle-exact height
*
**************************************************************************/
#pragma once

// STL
#include <algorithm>   // std::min
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Crucible engine
#include "Math/Math.h"

namespace Crucible {

    struct HeightGrid {

        std::uint32_t      cols     = 0;      // cells along X
        std::uint32_t      rows     = 0;      // cells along Z
        float              cellSize = 1.0f;   // metres per cell, both ways
        std::vector<float> heights;           // (cols + 1) * (rows + 1) corner heights, row-major

        std::uint32_t CornerCols() const { return cols + 1; }
        std::uint32_t CornerRows() const { return rows + 1; }

        // True when the sizes agree and every height is a real number.
        bool IsValid() const {
            if (cols == 0 || rows == 0 || !(cellSize > 0.0f) || !std::isfinite(cellSize)) return false;
            if (heights.size() != static_cast<std::size_t>(CornerCols()) * CornerRows()) return false;
            for (float h : heights) if (!std::isfinite(h)) return false;
            return true;
        }

        float Width() const { return static_cast<float>(cols) * cellSize; }   // metres along X
        float Depth() const { return static_cast<float>(rows) * cellSize; }   // metres along Z
        float MinX()  const { return -0.5f * Width(); }                       // the grid's left edge, local space
        float MinZ()  const { return -0.5f * Depth(); }                       // the grid's back edge, local space

        float Height(std::uint32_t x, std::uint32_t z) const { return heights[static_cast<std::size_t>(z) * CornerCols() + x]; }

        // Where corner (x, z) is, in the grid's own space.
        Math::vec3 Corner(std::uint32_t x, std::uint32_t z) const {
            return Math::vec3(MinX() + static_cast<float>(x) * cellSize, Height(x, z), MinZ() + static_cast<float>(z) * cellSize);
        }

        /*!
            \brief
            The ground's height under a point, on the exact triangle that
            covers it, so it matches the collider and the mesh.

            \param localX, localZ
            The point, in the grid's own space.

            \param outHeight
            Set when the point is over the grid.

            \return
            False if the point is off the grid (outHeight left alone).
        */
        bool SampleHeight(float localX, float localZ, float& outHeight) const {

            const float gx = (localX - MinX()) / cellSize;
            const float gz = (localZ - MinZ()) / cellSize;
            if (!(gx >= 0.0f && gz >= 0.0f && gx <= static_cast<float>(cols) && gz <= static_cast<float>(rows))) return false;

            // The far edge belongs to the last cell.
            const std::uint32_t x = std::min(static_cast<std::uint32_t>(gx), cols - 1);
            const std::uint32_t z = std::min(static_cast<std::uint32_t>(gz), rows - 1);
            const float fx = gx - static_cast<float>(x);
            const float fz = gz - static_cast<float>(z);

            const float h00 = Height(x, z),     h10 = Height(x + 1, z);
            const float h01 = Height(x, z + 1), h11 = Height(x + 1, z + 1);

            // Two triangles either side of the (x, z) -> (x + 1, z + 1) diagonal.
            outHeight = fz >= fx ? h00 + (h11 - h01) * fx + (h01 - h00) * fz    // triangle 00, 01, 11
                                 : h00 + (h10 - h00) * fx + (h11 - h10) * fz;   // triangle 00, 11, 10
            return true;

        }   // SampleHeight()

    };  // HEIGHTGRID STRUCT

}   // NAMESPACE CRUCIBLE
