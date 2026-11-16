/*************************************************************************
* \file:         FlowField.cpp
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief
*                FlowField's cost field, Dijkstra flood and direction pass.
*
* \details
*                Follows Jean David's porting reference, plus the three
*                changes agreed for the port: the corner check (no diagonal
*                step between two blocked cells), a goal inside a blocked
*                cell moves to the nearest open one, and several goals can
*                share one flood.
*
* \history
*    -v1.0       first version
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

        // How far (in cells) to look for an open cell when a goal stands in a
        // blocked one. A player brushing a wall is inside its padding by a cell
        // or two; 8 is plenty, and stops a goal deep in a sealed area from
        // searching the whole map.
        constexpr int kGoalSearchCells = 8;

        // The 8 neighbours, in Jean David's order: right, left, up, down, then
        // the four diagonals. On a tie the direction pass keeps the first in
        // this order, so keeping his order keeps his arrows. Indices 4-7 are
        // the diagonals.
        constexpr int kStepCol[8] = { 1, -1, 0,  0, 1, -1,  1, -1 };
        constexpr int kStepRow[8] = { 0,  0, 1, -1, 1,  1, -1, -1 };

    }   // ANONYMOUS NAMESPACE

    bool FlowField::IsOpen(int col, int row) const {

        // Off the grid counts as blocked, the same as in NavGrid.
        const NavCell cell{ col, row };
        return m_grid->Contains(cell) && m_cost[static_cast<std::size_t>(m_grid->Index(cell))] != kInfinity;

    }   // FlowField::IsOpen()

    void FlowField::BuildCostField(const NavGrid& grid, float wallAdjacencyCost) {

        m_grid = &grid;

        const std::size_t count = static_cast<std::size_t>(grid.Cols()) * static_cast<std::size_t>(grid.Rows());
        m_cost.assign(count, 1.0f);
        m_integration.assign(count, kInfinity);    // nothing reached yet
        m_dirX.assign(count, 0);
        m_dirZ.assign(count, 0);
        m_areaMin = { 0, 0 };
        m_areaMax = { -1, -1 };
        m_hasFlow = false;

        for (int row = 0; row < grid.Rows(); ++row) {
            for (int col = 0; col < grid.Cols(); ++col) {

                const std::size_t index = static_cast<std::size_t>(grid.Index({ col, row }));
                if (grid.IsBlocked({ col, row })) {
                    m_cost[index] = kInfinity;    // never entered
                    continue;
                }

                // Touching a blocked cell straight on (not diagonally) costs
                // extra. IsBlocked counts off-grid as blocked, so the map's
                // edge pushes routes inwards too.
                const bool nearWall = grid.IsBlocked({ col + 1, row }) || grid.IsBlocked({ col - 1, row })
                                   || grid.IsBlocked({ col, row + 1 }) || grid.IsBlocked({ col, row - 1 });
                if (nearWall) m_cost[index] = 1.0f + wallAdjacencyCost;

            }
        }
        
    }   // FlowField::BuildCostField()

    void FlowField::ResetArea() {

        // Only the last flood's square was ever written, so only it needs
        // wiping. Everywhere else is still "not reached, no direction".
        for (int row = m_areaMin.row; row <= m_areaMax.row; ++row) {
            for (int col = m_areaMin.col; col <= m_areaMax.col; ++col) {
                const std::size_t index = static_cast<std::size_t>(m_grid->Index({ col, row }));
                m_integration[index] = kInfinity;
                m_dirX[index]        = 0;
                m_dirZ[index]        = 0;
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
        m_dirX.clear();
        m_dirZ.clear();
        m_areaMin = { 0, 0 };
        m_areaMax = { -1, -1 };
        m_hasFlow = false;

    }   // FlowField::Clear()

    bool FlowField::Rebuild(const std::vector<Math::vec3>& goals, float floodRadius) {

        ClearFlow();
        if (!m_grid || m_cost.empty() || goals.empty()) return false;
        const NavGrid& grid = *m_grid;

        // 1. The goal cells. Off the grid: pulled onto its edge, so enemies
        // crowd the nearest edge instead of giving up. Inside a blocked cell
        // (a player brushing a wall's padding): moved to the nearest open cell
        // - without this the whole flood would stop and every arrow vanish.
        std::vector<NavCell> goalCells;
        goalCells.reserve(goals.size());
        for (const Math::vec3& goal : goals) {

            const NavCell cell = grid.ClampToGrid(grid.WorldToCell(goal));
            if (IsOpen(cell.col, cell.row)) { goalCells.push_back(cell); continue; }

            // Search squares of growing size around it; within one square take
            // the open cell closest to the goal, so the pick doesn't depend on
            // which corner the loop happens to start from.
            for (int ring = 1; ring <= kGoalSearchCells; ++ring) {

                NavCell best{};
                int     bestDistance = -1;
                for (int row = cell.row - ring; row <= cell.row + ring; ++row) {
                    for (int col = cell.col - ring; col <= cell.col + ring; ++col) {
                        if (!IsOpen(col, row)) continue;
                        const int distance = (col - cell.col) * (col - cell.col) + (row - cell.row) * (row - cell.row);
                        if (bestDistance < 0 || distance < bestDistance) { best = { col, row }; bestDistance = distance; }
                    }
                }
                if (bestDistance >= 0) { goalCells.push_back(best); break; }

            }

        }
        if (goalCells.empty()) return false;

        // 2. The area to flood: a square of +-radius cells round the goals,
        // clamped to the grid (several goals: the box round all their squares).
        // Inside it, only cells within the radius of some goal - a circle - are
        // flooded. 0 means the whole grid.
        const bool wholeGrid   = !(floodRadius > 0.0f);
        const int  radiusCells = wholeGrid ? 0 : static_cast<int>(std::ceil(floodRadius / grid.CellSize()));
        const int  radiusSq    = radiusCells * radiusCells;

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

        // 3. The integration field: Dijkstra outward from every goal at once.
        //
        // std::push_heap / pop_heap keep m_heap as a binary heap. They build a
        // MAX-heap by default; comparing with > turns it into the MIN-heap
        // Dijkstra needs (the nearest cell on top). A cell can be pushed more
        // than once as cheaper routes to it turn up; the older, dearer copies
        // are skipped when popped ("lazy deletion") - cheaper than finding and
        // fixing an entry inside the heap.
        const auto further = [](const HeapEntry& a, const HeapEntry& b) { return a.distance > b.distance; };

        m_heap.clear();
        for (const NavCell& goal : goalCells) {
            const int index = grid.Index(goal);
            m_integration[static_cast<std::size_t>(index)] = 0.0f;
            m_heap.push_back({ 0.0f, index });
            std::push_heap(m_heap.begin(), m_heap.end(), further);
        }

        while (!m_heap.empty()) {

            std::pop_heap(m_heap.begin(), m_heap.end(), further);
            const HeapEntry top = m_heap.back();
            m_heap.pop_back();
            if (top.distance > m_integration[static_cast<std::size_t>(top.index)]) continue;    // a stale copy

            const int col = top.index % grid.Cols();
            const int row = top.index / grid.Cols();

            for (int k = 0; k < 8; ++k) {

                const int nc = col + kStepCol[k];
                const int nr = row + kStepRow[k];

                // Inside the area (which is inside the grid), open, and for a
                // diagonal both cells beside it open too: the corner check, so
                // the flood never squeezes between two blocks touching at a
                // corner, where no body would fit.
                if (nc < areaMin.col || nc > areaMax.col || nr < areaMin.row || nr > areaMax.row) continue;
                if (!IsOpen(nc, nr)) continue;
                const bool diagonal = k >= 4;
                if (diagonal && (!IsOpen(nc, row) || !IsOpen(col, nr))) continue;
                if (!insideRadius(nc, nr)) continue;

                // The neighbour's own cost multiplies the step, so entering a
                // cell next to a wall costs 5 (or 5 * sqrt 2 diagonally).
                const std::size_t next     = static_cast<std::size_t>(grid.Index({ nc, nr }));
                const float       distance = top.distance + (diagonal ? kDiagonal : 1.0f) * m_cost[next];
                if (distance < m_integration[next]) {
                    m_integration[next] = distance;
                    m_heap.push_back({ distance, static_cast<int>(next) });
                    std::push_heap(m_heap.begin(), m_heap.end(), further);
                }

            }

        }

        // 4. The direction field: every reached cell points at its neighbour
        // with the lowest integration value, if that is strictly lower than
        // its own. Blocked and unreached neighbours are infinity, so they never
        // win, and a goal's own cell has nothing lower, so it stays (0, 0).
        for (int row = areaMin.row; row <= areaMax.row; ++row) {
            for (int col = areaMin.col; col <= areaMax.col; ++col) {

                const std::size_t index = static_cast<std::size_t>(grid.Index({ col, row }));
                float best  = m_integration[index];
                int   bestK = -1;
                if (best == kInfinity) continue;

                for (int k = 0; k < 8; ++k) {

                    const int nc = col + kStepCol[k];
                    const int nr = row + kStepRow[k];
                    if (!grid.Contains({ nc, nr })) continue;
                    if (k >= 4 && (!IsOpen(nc, row) || !IsOpen(col, nr))) continue;   // same corner check

                    const float value = m_integration[static_cast<std::size_t>(grid.Index({ nc, nr }))];
                    if (value < best) { best = value; bestK = k; }

                }

                if (bestK >= 0) {
                    m_dirX[index] = static_cast<std::int8_t>(kStepCol[bestK]);
                    m_dirZ[index] = static_cast<std::int8_t>(kStepRow[bestK]);
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
        return DirectionAt(m_grid->WorldToCell(position));

    }   // FlowField::SampleDirection()

    Math::vec3 FlowField::DirectionAt(const NavCell& cell) const {

        if (!m_hasFlow || !m_grid->Contains(cell)) return Math::vec3(0.0f);

        const std::size_t index = static_cast<std::size_t>(m_grid->Index(cell));
        const int         x     = m_dirX[index];
        const int         z     = m_dirZ[index];
        if (x == 0 && z == 0) return Math::vec3(0.0f);

        // Straight steps are already unit length; diagonals need scaling down.
        const Math::vec3 direction(static_cast<float>(x), 0.0f, static_cast<float>(z));
        return (x != 0 && z != 0) ? direction * kInvSqrt2 : direction;

    }   // FlowField::DirectionAt()

}   // NAMESPACE CRUCIBLE
