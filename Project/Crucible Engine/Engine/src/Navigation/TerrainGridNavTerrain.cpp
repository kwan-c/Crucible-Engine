/*************************************************************************
* \file:         TerrainGridNavTerrain.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          17 November 2026
* \version       1.1
* \brief         See TerrainGridNavTerrain.h.
*
* \history
*    -v1.0       first version
*    -v1.1       Floors
*
**************************************************************************/
#include "TerrainGridNavTerrain.h"

// STL
#include <algorithm>    // std::min
#include <cmath>        // std::floor
#include <limits>       // infinity: no rock above a floor
#include <utility>      // std::move

namespace Crucible {

    bool TerrainGridNavTerrain::Reset(std::shared_ptr<const TerrainGrid> grid, const Math::mat4& toWorld, float offTerrainHeight) {

        m_offTerrainHeight = offTerrainHeight;
        if (!grid || !grid->IsValid()) { m_grid.reset(); return false; }

        m_grid    = std::move(grid);
        m_toWorld = toWorld;
        m_toLocal = glm::inverse(toWorld);
        m_normalToWorld = glm::transpose(glm::inverse(Math::mat3(toWorld)));
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

    // Down the column through the point, the density at each sample height
    // (blended between the 4 sample columns around it). A floor is where it
    // goes from rock (above 0) to air going up; its headroom reaches the next
    // change back to rock. The slope is the surface's own, from the density's
    // gradient there.
    bool TerrainGridNavTerrain::Floors(const Math::vec3& position, std::vector<NavFloor>& floors) const {

        floors.clear();
        if (!m_grid || m_grid->volume.IsEmpty()) return false;

        const TerrainVolume& volume  = m_grid->volume;
        const HeightGrid&    heights = m_grid->heights;
        const Math::vec2     local   = ToLocal(position);

        // Only where the volume replaces the heightfield: the same cells the
        // mesh and the collider give to it.
        const float cellX = std::floor((local.x - heights.MinX()) / heights.cellSize);
        const float cellZ = std::floor((local.y - heights.MinZ()) / heights.cellSize);
        if (cellX < 0.0f || cellZ < 0.0f || cellX >= static_cast<float>(heights.cols) || cellZ >= static_cast<float>(heights.rows)) return false;
        if (!volume.Covers(heights, static_cast<std::uint32_t>(cellX), static_cast<std::uint32_t>(cellZ))) return false;

        const float size = volume.voxelSize;
        m_column.resize(volume.countY);
        for (std::uint32_t y = 0; y < volume.countY; ++y)
            m_column[y] = volume.Sample(Math::vec3(local.x, volume.origin.y + static_cast<float>(y) * size, local.y));

        // Where the density crosses 0 between sample y and y + 1.
        const auto crossing = [&](std::uint32_t y) {
            return volume.origin.y + (static_cast<float>(y) + m_column[y] / (m_column[y] - m_column[y + 1])) * size;
        };
        const auto worldY = [&](float y) { return (m_toWorld * Math::vec4(local.x, y, local.y, 1.0f)).y; };
        const float infinity = std::numeric_limits<float>::infinity();

        for (std::uint32_t y = 0; y + 1 < volume.countY; ++y) {

            if (!(m_column[y] > 0.0f && m_column[y + 1] <= 0.0f)) continue;   // rock below, air above: a floor
            const float floor = crossing(y);

            float ceiling = infinity;
            for (std::uint32_t up = y + 1; up + 1 < volume.countY; ++up) {
                if (m_column[up] <= 0.0f && m_column[up + 1] > 0.0f) { ceiling = crossing(up); break; }
            }

            // The density falls towards the air, so the surface faces against its gradient.
            const Math::vec3 point(local.x, floor, local.y);
            const float      h = size * 0.5f;
            const Math::vec3 gradient(volume.Sample(point + Math::vec3(h, 0.0f, 0.0f)) - volume.Sample(point - Math::vec3(h, 0.0f, 0.0f)),
                                      volume.Sample(point + Math::vec3(0.0f, h, 0.0f)) - volume.Sample(point - Math::vec3(0.0f, h, 0.0f)),
                                      volume.Sample(point + Math::vec3(0.0f, 0.0f, h)) - volume.Sample(point - Math::vec3(0.0f, 0.0f, h)));
            const Math::vec3 normal = m_normalToWorld * -gradient;
            const float      length = glm::length(normal);

            NavFloor result;
            result.height   = worldY(floor);
            result.headroom = ceiling == infinity ? infinity : worldY(ceiling) - result.height;
            result.steep    = length > 1e-6f && normal.y / length < TerrainVolume::kFloorNormalY;
            floors.push_back(result);

        }
        return true;

    }   // TerrainGridNavTerrain::Floors()

}   // NAMESPACE CRUCIBLE
