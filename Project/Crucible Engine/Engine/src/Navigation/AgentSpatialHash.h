/*************************************************************************
* \file:         AgentSpatialHash.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief
*                AgentSpatialHash: answers "which agents are near this
*                point?" without checking every agent against every other.
*
* \details
*                Ported from Jean David's Unity prototype
*                (EnemySpatialHash.cs). Agents are dropped into buckets by
*                flat (XZ) square cells; a query only looks in the buckets
*                round the point. With 100 agents that is a few dozen
*                distance checks per agent instead of 100.
*
*                Refilled once per frame (Clear, then Insert every agent).
*                Clear empties the buckets but keeps their memory, so after
*                the first few frames a refill allocates nothing.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <unordered_map>
#include <vector>

// Crucible engine
#include "Math/Math.h"

namespace Crucible {

    class AgentSpatialHash {

    public:

        // cellSize: metres per bucket. Jean David's 8 m: with his 10 m
        // neighbour radius a query visits a 5 x 5 block of buckets.
        explicit AgentSpatialHash(float cellSize = 8.0f) : m_cellSize(cellSize) {}

        // Empties every bucket, keeping its memory for the next refill.
        void Clear();

        // Files agent number `index` (the caller's own numbering) at `position`.
        void Insert(int index, const Math::vec3& position);

        /*!
            \brief
            Every agent within `radius` of `position`, measured flat (Y is
            ignored), except agent `self`.

            \param out
            Cleared first, then filled with the agents' indices. Pass the same
            vector every time so it stops allocating.
        */
        void Query(const Math::vec3& position, float radius, int self, std::vector<int>& out) const;

    private:

        struct Entry {

            int        index = 0;
            Math::vec3 position{ 0.0f };   // kept here so a query needs nothing from the caller

        };  // ENTRY STRUCT

        // One key per cell: X in the top 32 bits, Z in the bottom 32 (as in his
        // prototype). The cast to uint32 stops a negative Z smearing its sign
        // bits over X's half.
        static std::int64_t Key(int cellX, int cellZ) {
            return (static_cast<std::int64_t>(cellX) << 32) ^ static_cast<std::int64_t>(static_cast<std::uint32_t>(cellZ));
        }

        int CellOf(float coordinate) const;

        float                                            m_cellSize;
        std::unordered_map<std::int64_t, std::vector<Entry>> m_buckets;   // empty buckets are kept: they are reused next frame

    };  // AGENTSPATIALHASH CLASS

}   // NAMESPACE CRUCIBLE
