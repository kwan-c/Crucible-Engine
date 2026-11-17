/*************************************************************************
* \file:         TerrainGridNavTerrain.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          17 November 2026
* \version       1.0
* \brief         See TerrainGridNavTerrain.h.
*
**************************************************************************/
#include "TerrainGridNavTerrain.h"

// STL
#include <algorithm>    // std::min
#include <cmath>        // std::floor
#include <utility>      // std::move

namespace Crucible {

    bool TerrainGridNavTerrain::Reset(std::shared_ptr<const TerrainGrid> grid, const Math::mat4& toWorld, float offTerrainHeight) {

        m_offTerrainHeight = offTerrainHeight;
        if (!grid || !grid->IsValid()) { m_grid.reset(); return false; }

        m_grid    = std::move(grid);
        m_toWorld = toWorld;
        m_toLocal = glm::inverse(toWorld);
        return true;

    }   // TerrainGridNavTerrain::Reset()

    void TerrainGridNavTerrain::Clear() {

        m_grid.reset();

    }   // TerrainGridNavTerrain::Clear()

    Math::vec2 TerrainGridNavTerrain::ToLocal(const Math::vec3& position) const {

        // y doesn't matter: the terrain is only turned about Y, so the point
        // lands on the same (x, z) whatever its height.
        const Math::vec4 local = m_toLocal * Math::vec4(position.x, 0.0f, position.z, 1.0f);
        return Math::vec2(local.x, local.z);

    }   // TerrainGridNavTerrain::ToLocal()

    bool TerrainGridNavTerrain::IsWall(const Math::vec3& position) const {

        if (!m_grid) return true;
        const HeightGrid& heights = m_grid->heights;
        const Math::vec2  local   = ToLocal(position);

        // Which generator cell the point is in; off the terrain is a wall.
        const float gx = (local.x - heights.MinX()) / heights.cellSize;
        const float gz = (local.y - heights.MinZ()) / heights.cellSize;
        if (!(gx >= 0.0f && gz >= 0.0f && gx <= static_cast<float>(heights.cols) && gz <= static_cast<float>(heights.rows))) return true;

        // The far edge belongs to the last cell, as in HeightGrid::SampleHeight.
        const std::uint32_t x = std::min(static_cast<std::uint32_t>(std::floor(gx)), heights.cols - 1);
        const std::uint32_t z = std::min(static_cast<std::uint32_t>(std::floor(gz)), heights.rows - 1);
        const std::uint8_t  region = m_grid->regions[static_cast<std::size_t>(z) * heights.cols + x];
        return region == static_cast<std::uint8_t>(TerrainRegion::Cliff);

    }   // TerrainGridNavTerrain::IsWall()

    float TerrainGridNavTerrain::GroundHeight(const Math::vec3& position) const {

        if (!m_grid) return m_offTerrainHeight;
        const Math::vec2 local = ToLocal(position);

        float height = 0.0f;
        if (!m_grid->heights.SampleHeight(local.x, local.y, height)) return m_offTerrainHeight;

        // Back to world space: the terrain entity's position and scale.
        return (m_toWorld * Math::vec4(local.x, height, local.y, 1.0f)).y;

    }   // TerrainGridNavTerrain::GroundHeight()

}   // NAMESPACE CRUCIBLE
