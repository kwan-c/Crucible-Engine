/*************************************************************************
* \file:         FlowField.cpp
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          1 November 2026
* \version       1.2
* \brief
*                FlowField's cost field, Dijkstra flood and direction pass.
*
* \details
*                Follows Jean David's porting reference, plus the three
*                changes agreed for the port: the corner check (no diagonal
*                step between two blocked cells), a goal inside a blocked
*                cell moves to the nearest open one, and several goals can
*                share one flood. Since 6d-1 it runs on the grid's nodes and
*                links (layers), so bridges, ramps and drops route.
*
* \history
*    -v1.0       first version
*    -v1.1       CostAt (the cost rule, shared); UpdateCosts and TouchesFlow
*    -v1.2       per node, along the grid's links (build step 6d-1); with
*                only the ground, the same costs and arrows as before
*
**************************************************************************/
#include "FlowField.h"

// STL
#include <algorithm>    // std::max, std::min, std::pop_heap, std::push_heap
#include <cmath>        // std::ceil
#include <limits>       // std::numeric_limits

namespace Crucible {

    namespace {

        constexpr float kInfinity = std::numeric_limits<float>::infinity();
        constexpr float kDiagonal = 1.41421356f;    // a diagonal step is sqrt(2) times as long as a straight one
        constexpr float kInvSqrt2 = 0.70710678f;    // makes a diagonal (1, 0, 1) unit length

        // How far (in cells) to look for an open node when a goal stands in a
        // blocked one. A player brushing a wall is inside its padding by a cell
        // or two; 8 is plenty, and stops a goal deep in a sealed area from
        // searching the whole map.
        constexpr int kGoalSearchCells = 8;

        // The straight direction (index into kNavDirCol/Row) along one axis:
        // +col 0, -col 1, +row 2, -row 3.
        int StraightAlongCol(int step) { return step > 0 ? 0 : 1; }
        int StraightAlongRow(int step) { return step > 0 ? 2 : 3; }

    }   // ANONYMOUS NAMESPACE

    bool FlowField::IsOpen(int node) const {

        return node >= 0 && m_cost[static_cast<std::size_t>(node)] != kInfinity;

    }   // FlowField::IsOpen()

    // A diagonal step from `node` in direction `dir` squeezes past two cells
    // touching at a corner: both straight steps beside it must be open, or no
    // body would fit through. Straight steps always pass.
    bool FlowField::CornerClear(int node, int dir) const {

        if (dir < 4) return true;
        return IsOpen(m_grid->LinkedNode(node, StraightAlongCol(kNavDirCol[dir])))
            && IsOpen(m_grid->LinkedNode(node, StraightAlongRow(kNavDirRow[dir])));

    }   // FlowField::CornerClear()

    float FlowField::CostAt(int node) const {

        const NavGrid& grid = *m_grid;
        if (grid.IsNodeBlocked(node)) return kInfinity;    // never entered

        // Next to a wall straight on (not diagonally) costs extra: no link that
        // way (the map's edge, a ledge, a cliff face) or a link into a blocked
        // node. On a grid with only the ground that is exactly "a straight
        // neighbour cell is blocked or off the grid", as before layers.
        for (int dir = 0; dir < 4; ++dir) {
            const int next = grid.LinkedNode(node, dir);
            if (next < 0 || grid.IsNodeBlocked(next)) return 1.0f + m_wallAdjacencyCost;
        }
        return 1.0f;

    }   // FlowField::CostAt()

    void FlowField::BuildCostField(const NavGrid& grid, float wallAdjacencyCost) {

        m_grid              = &grid;
        m_wallAdjacencyCost = wallAdjacencyCost;

        const std::size_t count = static_cast<std::size_t>(grid.NodeCount());
        m_cost.assign(count, 1.0f);
        m_integration.assign(count, kInfinity);    // nothing reached yet
        m_direction.assign(count, -1);
        m_areaMin = { 0, 0 };
        m_areaMax = { -1, -1 };
        m_hasFlow = false;

        for (int node = 0; node < grid.NodeCount(); ++node) m_cost[static_cast<std::size_t>(node)] = CostAt(node);

    }   // FlowField::BuildCostField()

    void FlowField::UpdateCosts(const std::vector<int>& changed) {

        if (!m_grid || m_cost.empty()) return;
        const NavGrid& grid = *m_grid;

        for (const int node : changed) {

            if (node < 0 || static_cast<std::size_t>(node) >= m_cost.size()) continue;   // from an older bake
            m_cost[static_cast<std::size_t>(node)] = CostAt(node);

            // The nodes of its straight neighbour cells: their "next to a
            // wall" extra may depend on it (any of them may link to it).
            const NavCell cell = grid.NodeCell(node);
            for (int dir = 0; dir < 4; ++dir) {
                const NavCell next{ cell.col + kNavDirCol[dir], cell.row + kNavDirRow[dir] };
                for (int layer = 0; layer < grid.LayerCount(next); ++layer) {
                    const int other = grid.NodeIndex(next, layer);
                    m_cost[static_cast<std::size_t>(other)] = CostAt(other);
                }
            }

        }

    }   // FlowField::UpdateCosts()

    bool FlowField::TouchesFlow(const std::vector<int>& changed) const {

        if (!m_hasFlow || !m_grid) return false;

        // One cell wider than the flooded square: a change just outside it
        // still alters the cost of the node next to it, inside.
        for (const int node : changed) {
            if (node < 0 || node >= m_grid->NodeCount()) continue;
            const NavCell cell = m_grid->NodeCell(node);
            if (cell.col >= m_areaMin.col - 1 && cell.col <= m_areaMax.col + 1 &&
                cell.row >= m_areaMin.row - 1 && cell.row <= m_areaMax.row + 1) return true;
        }
        return false;

    }   // FlowField::TouchesFlow()

    void FlowField::ResetArea() {

        // Only the last flood's square was ever written, so only its nodes
        // need wiping. Everywhere else is still "not reached, no direction".
        for (int row = m_areaMin.row; row <= m_areaMax.row; ++row) {
            for (int col = m_areaMin.col; col <= m_areaMax.col; ++col) {
                for (int layer = 0; layer < m_grid->LayerCount({ col, row }); ++layer) {
                    const std::size_t node = static_cast<std::size_t>(m_grid->NodeIndex({ col, row }, layer));
                    m_integration[node] = kInfinity;
                    m_direction[node]   = -1;
                }
            }
        }
        m_areaMax = { -1, -1 };    // max < min: no area, so a second reset does nothing

    }   // FlowField::ResetArea()

    void FlowField::ClearFlow() {

        if (m_grid) ResetArea();
        m_hasFlow = false;

    }   // FlowField::ClearFlow()

    void FlowField::Clear() {

        m_grid = nullptr;
        m_cost.clear();
        m_integration.clear();
        m_direction.clear();
        m_areaMin = { 0, 0 };
        m_areaMax = { -1, -1 };
        m_hasFlow = false;

    }   // FlowField::Clear()

    bool FlowField::Rebuild(const std::vector<Math::vec3>& goals, float floodRadius) {

        ClearFlow();
        if (!m_grid || m_cost.empty() || goals.empty()) return false;
        const NavGrid& grid = *m_grid;

        // 1. The goal nodes: the layer under each goal (on the bridge, the
        // deck). Off the grid: pulled onto its edge, so enemies crowd the
        // nearest edge instead of giving up. On a blocked node (a player
        // brushing a wall's padding): moved to the nearest open node - without
        // this the whole flood would stop and every arrow vanish.
        std::vector<int> goalNodes;
        goalNodes.reserve(goals.size());
        for (const Math::vec3& goal : goals) {

            const NavCell cell = grid.ClampToGrid(grid.WorldToCell(goal));
            const int     here = grid.NodeAt(cell, goal.y);
            if (IsOpen(here)) { goalNodes.push_back(here); continue; }

            // Search squares of growing size around it; within one square take
            // the open node closest to the goal (in cells, then in height), so
            // the pick doesn't depend on which corner the loop starts from.
            for (int ring = 1; ring <= kGoalSearchCells; ++ring) {

                int   best = -1;
                int   bestDistance = -1;
                float bestRise     = 0.0f;
                for (int row = cell.row - ring; row <= cell.row + ring; ++row) {
                    for (int col = cell.col - ring; col <= cell.col + ring; ++col) {
                        for (int layer = 0; layer < grid.LayerCount({ col, row }); ++layer) {
                            const int node = grid.NodeIndex({ col, row }, layer);
                            if (!IsOpen(node)) continue;
                            const int   distance = (col - cell.col) * (col - cell.col) + (row - cell.row) * (row - cell.row);
                            const float rise     = std::abs(grid.NodeHeight(node) - goal.y);
                            if (best < 0 || distance < bestDistance || (distance == bestDistance && rise < bestRise)) {
                                best = node; bestDistance = distance; bestRise = rise;
                            }
                        }
                    }
                }
                if (best >= 0) { goalNodes.push_back(best); break; }

            }

        }
        if (goalNodes.empty()) return false;

        // 2. The area to flood: a square of +-radius cells round the goals,
        // clamped to the grid (several goals: the box round all their squares).
        // Inside it, only cells within the radius of some goal - a circle - are
        // flooded. 0 means the whole grid.
        const bool wholeGrid   = !(floodRadius > 0.0f);
        const int  radiusCells = wholeGrid ? 0 : static_cast<int>(std::ceil(floodRadius / grid.CellSize()));
        const int  radiusSq    = radiusCells * radiusCells;

        std::vector<NavCell> goalCells;
        goalCells.reserve(goalNodes.size());
        for (const int node : goalNodes) goalCells.push_back(grid.NodeCell(node));

        NavCell areaMin{ grid.Cols() - 1, grid.Rows() - 1 };
        NavCell areaMax{ 0, 0 };
        if (wholeGrid) {
            areaMin = { 0, 0 };
            areaMax = { grid.Cols() - 1, grid.Rows() - 1 };
        } else {
            for (const NavCell& goal : goalCells) {
                areaMin.col = std::min(areaMin.col, std::max(0, goal.col - radiusCells));
                areaMin.row = std::min(areaMin.row, std::max(0, goal.row - radiusCells));
                areaMax.col = std::max(areaMax.col, std::min(grid.Cols() - 1, goal.col + radiusCells));
                areaMax.row = std::max(areaMax.row, std::min(grid.Rows() - 1, goal.row + radiusCells));
            }
        }

        const auto insideRadius = [&](int col, int row) {
            if (wholeGrid) return true;
            for (const NavCell& goal : goalCells) {
                const int dc = col - goal.col;
                const int dr = row - goal.row;
                if (dc * dc + dr * dr <= radiusSq) return true;
            }
            return false;
        };

        // 3. The integration field: Dijkstra outward from every goal at once,
        // backwards along the links - from a node, every neighbour node that
        // links TO it (an enemy there can step, walk or drop here).
        //
        // std::push_heap / pop_heap keep m_heap as a binary heap. They build a
        // MAX-heap by default; comparing with > turns it into the MIN-heap
        // Dijkstra needs (the nearest node on top). A node can be pushed more
        // than once as cheaper routes to it turn up; the older, dearer copies
        // are skipped when popped ("lazy deletion") - cheaper than finding and
        // fixing an entry inside the heap.
        const auto further = [](const HeapEntry& a, const HeapEntry& b) { return a.distance > b.distance; };

        m_heap.clear();
        for (const int goal : goalNodes) {
            m_integration[static_cast<std::size_t>(goal)] = 0.0f;
            m_heap.push_back({ 0.0f, goal });
            std::push_heap(m_heap.begin(), m_heap.end(), further);
        }

        while (!m_heap.empty()) {

            std::pop_heap(m_heap.begin(), m_heap.end(), further);
            const HeapEntry top = m_heap.back();
            m_heap.pop_back();
            if (top.distance > m_integration[static_cast<std::size_t>(top.index)]) continue;    // a stale copy

            const NavCell cell = grid.NodeCell(top.index);
            for (int k = 0; k < kNavDirections; ++k) {

                const int nc = cell.col + kNavDirCol[k];
                const int nr = cell.row + kNavDirRow[k];
                if (nc < areaMin.col || nc > areaMax.col || nr < areaMin.row || nr > areaMax.row) continue;
                if (!insideRadius(nc, nr)) continue;

                // Every layer of that cell whose link back this way lands here.
                const int back = kNavOpposite[k];
                for (int layer = 0; layer < grid.LayerCount({ nc, nr }); ++layer) {

                    const int other = grid.NodeIndex({ nc, nr }, layer);
                    if (!IsOpen(other) || grid.LinkedNode(other, back) != top.index) continue;

                    // The corner check, for the step `other` would take: the
                    // flood never squeezes between two blocks touching at a
                    // corner, where no body would fit.
                    if (!CornerClear(other, back)) continue;

                    // The node's own cost multiplies the step, so entering one
                    // next to a wall costs 5 (or 5 * sqrt 2 diagonally).
                    const std::size_t next     = static_cast<std::size_t>(other);
                    const float       distance = top.distance + (k >= 4 ? kDiagonal : 1.0f) * m_cost[next];
                    if (distance < m_integration[next]) {
                        m_integration[next] = distance;
                        m_heap.push_back({ distance, other });
                        std::push_heap(m_heap.begin(), m_heap.end(), further);
                    }

                }

            }

        }

        // 4. The direction field: every reached node points along its link
        // to the neighbour with the lowest integration value, if that is
        // strictly lower than its own. Unreached ones are infinity, so they
        // never win, and a goal has nothing lower, so it keeps no direction.
        // Ties keep the first in Jean David's direction order.
        for (int row = areaMin.row; row <= areaMax.row; ++row) {
            for (int col = areaMin.col; col <= areaMax.col; ++col) {
                for (int layer = 0; layer < grid.LayerCount({ col, row }); ++layer) {

                    const int         node  = grid.NodeIndex({ col, row }, layer);
                    const std::size_t index = static_cast<std::size_t>(node);
                    float best  = m_integration[index];
                    int   bestK = -1;
                    if (best == kInfinity) continue;

                    for (int k = 0; k < kNavDirections; ++k) {
                        const int next = grid.LinkedNode(node, k);
                        if (next < 0 || !CornerClear(node, k)) continue;
                        const float value = m_integration[static_cast<std::size_t>(next)];
                        if (value < best) { best = value; bestK = k; }
                    }
                    if (bestK >= 0) m_direction[index] = static_cast<std::int8_t>(bestK);

                }
            }
        }

        m_areaMin = areaMin;
        m_areaMax = areaMax;
        m_hasFlow = true;
        return true;

    }   // FlowField::Rebuild()

    Math::vec3 FlowField::SampleDirection(const Math::vec3& position) const {

        if (!m_hasFlow) return Math::vec3(0.0f);
        return DirectionAtNode(m_grid->NodeAt(position));

    }   // FlowField::SampleDirection()

    Math::vec3 FlowField::DirectionAtNode(int node) const {

        if (!m_hasFlow || node < 0 || static_cast<std::size_t>(node) >= m_direction.size()) return Math::vec3(0.0f);

        const int k = m_direction[static_cast<std::size_t>(node)];
        if (k < 0) return Math::vec3(0.0f);

        // Straight steps are already unit length; diagonals need scaling down.
        const Math::vec3 direction(static_cast<float>(kNavDirCol[k]), 0.0f, static_cast<float>(kNavDirRow[k]));
        return k >= 4 ? direction * kInvSqrt2 : direction;

    }   // FlowField::DirectionAtNode()

}   // NAMESPACE CRUCIBLE
