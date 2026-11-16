/*************************************************************************
* \file:         NavGrid.cpp
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.0
* \brief
*                NavGrid's bake and its cell maths.
*
* \details
*                The bake follows Jean David's porting reference step by
*                step: height band, padding, mark the cells under each
*                padded footprint, then terrain walls.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "NavGrid.h"

// STL
#include <algorithm>    // std::clamp, std::count, std::max, std::min
#include <cmath>        // std::ceil, std::floor

// Crucible engine
#include "Diagnostics/Logger.h"
#include "Navigation/NavTerrain.h"

namespace Crucible {

    namespace {

        // A typo like 0.001 m cells on a 500 m map would ask for 250 billion
        // cells. This cap turns that into a clear error instead of a crash. 4
        // million is a 4 km map at 2 m cells - far beyond anything we plan.
        constexpr long long kMaxCells = 4'000'000;

        // A float cell number to an int, clamped to [-1, limit] first. Casting a
        // float that doesn't fit in an int is undefined behaviour, and a far-off
        // box or a runaway enemy can produce one. -1 and limit both still read
        // as "outside the grid", so clamping changes no answer.
        int ToCellNumber(float value, int limit) {

            return static_cast<int>(std::clamp(value, -1.0f, static_cast<float>(limit)));

        }   // ToCellNumber()

    }   // ANONYMOUS NAMESPACE

    bool NavGrid::Bake(const NavGridSettings& settings, const std::vector<NavObstacle>& obstacles, const NavTerrain& terrain) {

        // Every early return below leaves the grid unbaked, and an unbaked grid
        // reads as blocked everywhere - so a failed bake can't send enemies
        // through walls, it just stops the flow field from building.
        m_baked = false;

        // Written as !(x > 0) rather than x <= 0 so a NaN fails too.
        if (!(settings.cellSize > 0.0f) || !(settings.width > 0.0f) || !(settings.depth > 0.0f)) {
            CRU_ERROR("NavGrid: can't bake - width ({}), depth ({}) and cell size ({}) must all be above 0",
                      settings.width, settings.depth, settings.cellSize);
            return false;
        }

        // ceil, so a 501 m map at 2 m cells gets 251 columns and its last
        // metre is still covered. long long so the size check can't overflow.
        const long long cols = static_cast<long long>(std::ceil(settings.width / settings.cellSize));
        const long long rows = static_cast<long long>(std::ceil(settings.depth / settings.cellSize));
        if (cols * rows > kMaxCells) {
            CRU_ERROR("NavGrid: can't bake - {} x {} cells is over the limit of {}; use bigger cells or a smaller grid",
                      cols, rows, kMaxCells);
            return false;
        }

        m_settings = settings;
        m_cols     = static_cast<int>(cols);
        m_rows     = static_cast<int>(rows);
        m_minX     = settings.origin.x - settings.width * 0.5f;
        m_minZ     = settings.origin.z - settings.depth * 0.5f;
        m_blocked.assign(static_cast<std::size_t>(cols * rows), 0);   // all open; obstacles and walls close cells below

        const float cellSize = settings.cellSize;
        const float padding  = settings.bodyRadiusPadding;

        // 1. Obstacles.
        int counted = 0;
        for (const NavObstacle& obstacle : obstacles) {

            // The height band is measured from the ground under the box's
            // centre, so a rock on a hillside is judged against the hill.
            const Math::vec3 centre = (obstacle.min + obstacle.max) * 0.5f;
            const float      ground = terrain.GroundHeight(centre);

            // Entirely below the step (a kerb, the floor itself): walk over it.
            // Entirely above the shoulder (a branch, a bridge): walk under it.
            if (obstacle.max.y < ground + settings.stepHeight || obstacle.min.y > ground + settings.shoulderHeight) {
                continue;
            }
            ++counted;

            // Grow the footprint by the body radius, so the cells left open are
            // ones an enemy's whole body fits in, not just its centre point.
            const float x0 = obstacle.min.x - padding;
            const float x1 = obstacle.max.x + padding;
            const float z0 = obstacle.min.z - padding;
            const float z1 = obstacle.max.z + padding;

            // Only visit the cells under the rectangle, not the whole grid -
            // that's what keeps the bake fast with hundreds of trees.
            //
            // Cell c spans [minX + c * size, minX + (c + 1) * size]. It overlaps
            // (x0, x1) when its right edge is past x0 and its left edge is
            // before x1, both strictly, so a cell that only touches the
            // rectangle's edge stays open. Solving those for c gives:
            //   first = floor((x0 - minX) / size),  last = ceil((x1 - minX) / size) - 1
            const int colFirst = std::max(0,          ToCellNumber(std::floor((x0 - m_minX) / cellSize), m_cols));
            const int colLast  = std::min(m_cols - 1, ToCellNumber(std::ceil((x1 - m_minX) / cellSize), m_cols) - 1);
            const int rowFirst = std::max(0,          ToCellNumber(std::floor((z0 - m_minZ) / cellSize), m_rows));
            const int rowLast  = std::min(m_rows - 1, ToCellNumber(std::ceil((z1 - m_minZ) / cellSize), m_rows) - 1);

            // A box entirely off the grid gives first > last, so these loops
            // simply don't run.
            for (int row = rowFirst; row <= rowLast; ++row) {
                for (int col = colFirst; col <= colLast; ++col) {
                    m_blocked[static_cast<std::size_t>(Index({ col, row }))] = 1;
                }
            }

        }

        // 2. Terrain walls: cliffs and off-map, tested at each cell's centre
        // only (so a cell that is part cliff can still read as open - Jean
        // David's prototype does the same). Cells an obstacle already closed
        // are skipped, so the wall count below is only the walls' own.
        int wallCells = 0;
        for (int row = 0; row < m_rows; ++row) {
            for (int col = 0; col < m_cols; ++col) {

                const NavCell     cell{ col, row };
                const std::size_t index = static_cast<std::size_t>(Index(cell));
                if (m_blocked[index] == 0 && terrain.IsWall(CellToWorldCenter(cell))) {
                    m_blocked[index] = 1;
                    ++wallCells;
                }

            }
        }

        m_baked = true;

        const auto blocked = std::count(m_blocked.begin(), m_blocked.end(), std::uint8_t{ 1 });
        CRU_INFO("NavGrid baked: {} x {} cells of {} m, {} blocked ({} of {} obstacles inside the height band, {} terrain wall cells)",
                 m_cols, m_rows, cellSize, blocked, counted, obstacles.size(), wallCells);
        return true;

    }   // NavGrid::Bake()

    bool NavGrid::IsBlocked(const NavCell& cell) const {

        // Off the map is a wall: the flood never spreads outside the grid, and
        // an unbaked grid has no open cells to spread through.
        if (!m_baked || !Contains(cell)) return true;
        return m_blocked[static_cast<std::size_t>(Index(cell))] != 0;

    }   // NavGrid::IsBlocked()

    NavCell NavGrid::WorldToCell(const Math::vec3& position) const {

        // floor, not a plain cast: a cast rounds towards zero, which would put
        // x = -0.5 m past the left edge into column 0 instead of column -1.
        return {
            ToCellNumber(std::floor((position.x - m_minX) / m_settings.cellSize), m_cols),
            ToCellNumber(std::floor((position.z - m_minZ) / m_settings.cellSize), m_rows)
        };

    }   // NavGrid::WorldToCell()

    NavCell NavGrid::ClampToGrid(const NavCell& cell) const {

        // std::max guards the unbaked grid (0 columns), where clamp's upper
        // bound would otherwise be -1 and below its lower bound.
        return {
            std::clamp(cell.col, 0, std::max(0, m_cols - 1)),
            std::clamp(cell.row, 0, std::max(0, m_rows - 1))
        };

    }   // NavGrid::ClampToGrid()

    Math::vec3 NavGrid::CellToWorldCenter(const NavCell& cell) const {

        const float cellSize = m_settings.cellSize;
        return {
            m_minX + (static_cast<float>(cell.col) + 0.5f) * cellSize,
            m_settings.origin.y,
            m_minZ + (static_cast<float>(cell.row) + 0.5f) * cellSize
        };

    }   // NavGrid::CellToWorldCenter()

}   // NAMESPACE CRUCIBLE
