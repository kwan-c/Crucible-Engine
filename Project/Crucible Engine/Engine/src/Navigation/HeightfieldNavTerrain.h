/*************************************************************************
* \file:         HeightfieldNavTerrain.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          7 November 2026
* \version       1.0
* \brief
*                HeightfieldNavTerrain: ground that isn't flat - one height
*                and one wall flag per nav cell, for hills, ramps and
*                plateaus.
*
* \details
*                Knows nothing about physics or the ECS, like NavGrid: whoever
*                has the heights fills it in. Today that is ECSNavigationSystem,
*                sampling the level's colliders with rays when Play starts;
*                later the sub-level generator can fill one straight from its
*                own height map (Jean David's porting reference hands off
*                exactly these two answers, IsWall and GroundHeight).
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <vector>

// Crucible engine
#include "Math/Math.h"
#include "Navigation/NavTerrain.h"

// ---------------------------------------------------------------------------
// Layout: the same cells as the NavGrid it goes with - a cols x rows rectangle
// whose smallest corner is (minX, minZ), row by row, index = row * cols + col.
// Matching cells means the grid bake's "is this cell's centre a wall?" reads
// exactly the cell that was sampled, with no rounding between two grids.
//
// Heights are cell-centre heights. GroundHeight blends the four nearest
// centres (bilinear, like Jean David's), so an agent walking up a ramp rises
// smoothly instead of in 2 m stairs.
//
// Not exported (no ENGINE_API), like NavGrid: only the engine uses it, so it
// can hold std::vectors without the C4251 warning.
// ---------------------------------------------------------------------------

namespace Crucible {

    class HeightfieldNavTerrain final : public NavTerrain {

    public:

        /*!
            \brief
            Empties it and sizes it to a grid. Every cell starts open at
            `startHeight`, until SetCell says otherwise.
        */
        void Reset(float minX, float minZ, int cols, int rows, float cellSize, float startHeight);

        // Out-of-range cells are ignored, so a caller can't write past the end.
        void SetCell(int col, int row, float height, bool wall);

        bool  IsEmpty() const                 { return m_heights.empty(); }
        float CellHeight(int col, int row) const;   // clamped to the grid
        bool  CellWall(int col, int row) const;     // outside the grid is a wall

        // --- NavTerrain ------------------------------------------------------ //

        // The cell under the point is a wall (steep, a ledge, or nothing to
        // stand on) - or the point is off the grid, which is off the map.
        bool  IsWall(const Math::vec3& position) const override;

        // The four nearest cell centres blended. Off the grid, the nearest
        // edge cells (so an agent knocked off the edge still has a floor).
        float GroundHeight(const Math::vec3& position) const override;

    private:

        float                     m_minX     = 0.0f;
        float                     m_minZ     = 0.0f;
        float                     m_cellSize = 1.0f;
        int                       m_cols     = 0;
        int                       m_rows     = 0;
        std::vector<float>        m_heights;           // one per cell, cell-centre height
        std::vector<std::uint8_t> m_walls;             // 1 = wall; uint8_t, not vector<bool>, as in NavGrid

    };  // HEIGHTFIELDNAVTERRAIN CLASS

}   // NAMESPACE CRUCIBLE
