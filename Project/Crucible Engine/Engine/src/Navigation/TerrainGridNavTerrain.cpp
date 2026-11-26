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
*    -v1.1       Floors (V4a; reads the 4 sample columns directly since
*                V4b, for the links' many calls)
*
**************************************************************************/
#include "TerrainGridNavTerrain.h"

// STL
#include <algorithm>    // std::clamp, std::min
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

    // Down the column through the point, the density at each sample height,
    // blended between the 4 sample columns around it, and how it changes
    // along x and z there. A floor is where it goes from rock (above 0) to
    // air going up; its headroom reaches the next change back to rock. The
    // slope is the surface's own: the density falls towards the air, so the
    // surface faces against its gradient. Reads the 4 columns directly: the
    // links ask this at hundreds of thousands of points per bake.
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

        // The 4 sample columns around the point, and where it sits between them.
        const float         size = volume.voxelSize;
        const float         fx   = std::clamp((local.x - volume.origin.x) / size, 0.0f, static_cast<float>(volume.countX - 1));
        const float         fz   = std::clamp((local.y - volume.origin.z) / size, 0.0f, static_cast<float>(volume.countZ - 1));
        const std::uint32_t x    = std::min(static_cast<std::uint32_t>(fx), volume.countX - 2);
        const std::uint32_t z    = std::min(static_cast<std::uint32_t>(fz), volume.countZ - 2);
        const float         tx   = fx - static_cast<float>(x);
        const float         tz   = fz - static_cast<float>(z);

        m_column.resize(volume.countY);
        m_alongX.resize(volume.countY);
        m_alongZ.resize(volume.countY);
        for (std::uint32_t y = 0; y < volume.countY; ++y) {
            const float d00 = volume.At(x, y, z),     d10 = volume.At(x + 1, y, z);
            const float d01 = volume.At(x, y, z + 1), d11 = volume.At(x + 1, y, z + 1);
            const float back = d00 + (d10 - d00) * tx, front = d01 + (d11 - d01) * tx;
            m_column[y] = back + (front - back) * tz;
            m_alongX[y] = ((d10 - d00) * (1.0f - tz) + (d11 - d01) * tz) / size;
            m_alongZ[y] = (front - back) / size;
        }

        // Where the density crosses 0 between sample y and y + 1, as a fraction of the way up.
        const auto crossing = [&](std::uint32_t y) { return m_column[y] / (m_column[y] - m_column[y + 1]); };
        const auto localY   = [&](std::uint32_t y, float t) { return volume.origin.y + (static_cast<float>(y) + t) * size; };
        const auto worldY   = [&](float y) { return (m_toWorld * Math::vec4(local.x, y, local.y, 1.0f)).y; };
        const float infinity = std::numeric_limits<float>::infinity();

        for (std::uint32_t y = 0; y + 1 < volume.countY; ++y) {

            if (!(m_column[y] > 0.0f && m_column[y + 1] <= 0.0f)) continue;   // rock below, air above: a floor
            const float t = crossing(y);

            float ceiling = infinity;
            for (std::uint32_t up = y + 1; up + 1 < volume.countY; ++up) {
                if (m_column[up] <= 0.0f && m_column[up + 1] > 0.0f) { ceiling = localY(up, crossing(up)); break; }
            }

            const Math::vec3 gradient(m_alongX[y] + (m_alongX[y + 1] - m_alongX[y]) * t,
                                      (m_column[y + 1] - m_column[y]) / size,
                                      m_alongZ[y] + (m_alongZ[y + 1] - m_alongZ[y]) * t);
            const Math::vec3 normal = m_normalToWorld * -gradient;
            const float      length = glm::length(normal);

            NavFloor result;
            result.height   = worldY(localY(y, t));
            result.headroom = ceiling == infinity ? infinity : worldY(ceiling) - result.height;
            result.steep    = length > 1e-6f && normal.y / length < TerrainVolume::kFloorNormalY;
            floors.push_back(result);

        }
        return true;

    }   // TerrainGridNavTerrain::Floors()

}   // NAMESPACE CRUCIBLE
