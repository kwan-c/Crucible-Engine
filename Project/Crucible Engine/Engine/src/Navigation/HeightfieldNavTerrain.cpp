/*************************************************************************
* \file:         HeightfieldNavTerrain.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          7 November 2026
* \version       1.0
* \brief         See HeightfieldNavTerrain.h.
*
**************************************************************************/
#include "HeightfieldNavTerrain.h"

// STL
#include <algorithm>    // std::clamp, std::max
#include <cmath>        // std::floor

namespace Crucible {

    void HeightfieldNavTerrain::Reset(float minX, float minZ, int cols, int rows, float cellSize, float startHeight) {

        m_minX     = minX;
        m_minZ     = minZ;
        m_cellSize = std::max(cellSize, 0.01f);   // never divide by zero in the lookups
        m_cols     = std::max(cols, 0);
        m_rows     = std::max(rows, 0);

        const std::size_t count = static_cast<std::size_t>(m_cols) * static_cast<std::size_t>(m_rows);
        m_heights.assign(count, startHeight);
        m_walls.assign(count, 0);

    }   // HeightfieldNavTerrain::Reset()

    void HeightfieldNavTerrain::SetCell(int col, int row, float height, bool wall) {

        if (col < 0 || row < 0 || col >= m_cols || row >= m_rows) return;
        const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(m_cols) + static_cast<std::size_t>(col);
        m_heights[index] = height;
        m_walls[index]   = wall ? 1 : 0;

    }   // HeightfieldNavTerrain::SetCell()

    float HeightfieldNavTerrain::CellHeight(int col, int row) const {

        if (m_heights.empty()) return 0.0f;
        col = std::clamp(col, 0, m_cols - 1);
        row = std::clamp(row, 0, m_rows - 1);
        return m_heights[static_cast<std::size_t>(row) * static_cast<std::size_t>(m_cols) + static_cast<std::size_t>(col)];

    }   // HeightfieldNavTerrain::CellHeight()

    bool HeightfieldNavTerrain::CellWall(int col, int row) const {

        if (col < 0 || row < 0 || col >= m_cols || row >= m_rows) return true;
        return m_walls[static_cast<std::size_t>(row) * static_cast<std::size_t>(m_cols) + static_cast<std::size_t>(col)] != 0;

    }   // HeightfieldNavTerrain::CellWall()

    bool HeightfieldNavTerrain::IsWall(const Math::vec3& position) const {

        // floor, not a cast: a cast rounds -0.5 up to 0, which would put a point
        // just off the left edge into the first column instead of outside.
        const int col = static_cast<int>(std::floor((position.x - m_minX) / m_cellSize));
        const int row = static_cast<int>(std::floor((position.z - m_minZ) / m_cellSize));
        return CellWall(col, row);

    }   // HeightfieldNavTerrain::IsWall()

    float HeightfieldNavTerrain::GroundHeight(const Math::vec3& position) const {

        if (m_heights.empty()) return 0.0f;

        // In "cell-centre units": 0 at the first centre, 1 at the next. The
        // 0.5 shifts from cell edges to centres, since heights are sampled there.
        const float fx = (position.x - m_minX) / m_cellSize - 0.5f;
        const float fz = (position.z - m_minZ) / m_cellSize - 0.5f;
        const int   c0 = static_cast<int>(std::floor(fx));
        const int   r0 = static_cast<int>(std::floor(fz));

        // Clamped blend weights: past the last centre it holds the edge height
        // rather than extrapolating a slope off into the air.
        const float tx = std::clamp(fx - static_cast<float>(c0), 0.0f, 1.0f);
        const float tz = std::clamp(fz - static_cast<float>(r0), 0.0f, 1.0f);

        // Not called near / far: Windows headers define those as empty macros.
        const float front = CellHeight(c0, r0)     + (CellHeight(c0 + 1, r0)     - CellHeight(c0, r0))     * tx;
        const float back  = CellHeight(c0, r0 + 1) + (CellHeight(c0 + 1, r0 + 1) - CellHeight(c0, r0 + 1)) * tx;
        return front + (back - front) * tz;

    }   // HeightfieldNavTerrain::GroundHeight()

}   // NAMESPACE CRUCIBLE
