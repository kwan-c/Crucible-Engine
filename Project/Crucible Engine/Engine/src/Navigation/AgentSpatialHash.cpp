/*************************************************************************
* \file:         AgentSpatialHash.cpp
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief
*                AgentSpatialHash's buckets and neighbour query.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "AgentSpatialHash.h"

// STL
#include <algorithm>    // std::clamp
#include <cmath>        // std::ceil, std::floor

namespace Crucible {

    int AgentSpatialHash::CellOf(float coordinate) const {

        // floor, not a cast: a cast rounds towards zero, so -1 m and +1 m
        // would share cell 0. Clamped before the cast because a float that
        // doesn't fit in an int is undefined behaviour - a runaway agent
        // must not crash the hash.
        return static_cast<int>(std::clamp(std::floor(coordinate / m_cellSize), -1.0e9f, 1.0e9f));

    }   // AgentSpatialHash::CellOf()

    void AgentSpatialHash::Clear() {

        // clear() on each vector, not on the map: the vectors keep their
        // capacity, so next frame's Inserts reuse it instead of allocating.
        for (auto& [key, bucket] : m_buckets) bucket.clear();

    }   // AgentSpatialHash::Clear()

    void AgentSpatialHash::Insert(int index, const Math::vec3& position) {

        m_buckets[Key(CellOf(position.x), CellOf(position.z))].push_back({ index, position });

    }   // AgentSpatialHash::Insert()

    void AgentSpatialHash::Query(const Math::vec3& position, float radius, int self, std::vector<int>& out) const {

        out.clear();
        if (!(radius > 0.0f)) return;

        // Enough buckets each way to cover the radius from anywhere inside
        // this agent's own bucket.
        const int reach   = static_cast<int>(std::ceil(radius / m_cellSize));
        const int centreX = CellOf(position.x);
        const int centreZ = CellOf(position.z);
        const float radiusSq = radius * radius;

        for (int z = centreZ - reach; z <= centreZ + reach; ++z) {
            for (int x = centreX - reach; x <= centreX + reach; ++x) {

                const auto found = m_buckets.find(Key(x, z));
                if (found == m_buckets.end()) continue;

                for (const Entry& entry : found->second) {
                    if (entry.index == self) continue;
                    const float dx = entry.position.x - position.x;
                    const float dz = entry.position.z - position.z;
                    if (dx * dx + dz * dz < radiusSq) out.push_back(entry.index);
                }

            }
        }

    }   // AgentSpatialHash::Query()

}   // NAMESPACE CRUCIBLE
