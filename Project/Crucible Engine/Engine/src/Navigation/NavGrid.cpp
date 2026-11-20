/*************************************************************************
* \file:         NavGrid.cpp
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.4
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
*    -v1.1       static flag + dynamic count per cell; the obstacle maths
*                moved into Footprint(), shared by the bake and (later)
*                re-stamping
*    -v1.2       shoulderHeight renamed agentHeight
*    -v1.3       layer storage (nodes): one ground node per cell for now,
*                with its height; the flags and counts live on nodes
*    -v1.4       layers from Walkable boxes (BuildLayers): top faces with
*                agentHeight of headroom, up to maxLayersPerCell; obstacles,
*                terrain walls and IsBlocked use each cell's ground node
*
**************************************************************************/
#include "NavGrid.h"

// STL
#include <algorithm>    // std::clamp, std::max, std::min, std::sort
#include <cmath>        // std::ceil, std::floor
#include <limits>       // infinity: nothing above a layer

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

        // Heights closer than this are the same surface (two deck boxes
        // overlapping, a box resting on the ground).
        constexpr float kSameHeight = 0.01f;

        // Where a vertical line through (x, z) passes through a Walkable box:
        // from `bottom` to `top`, in world height. False if it misses. The
        // line is taken into the box's own space, where the box is axis-
        // aligned, and clipped against its three pairs of faces (the slab
        // test) - so turned, tilted and scaled boxes all work, a tilted one
        // giving a ramp's height at that point.
        bool ThroughBox(const NavWalkable& box, const Math::mat4& toLocal, float x, float z, float& bottom, float& top) {

            // The line is (x, t, z): in local space, origin + t * along.
            const Math::vec3 origin(toLocal * Math::vec4(x, 0.0f, z, 1.0f));
            const Math::vec3 along (toLocal * Math::vec4(0.0f, 1.0f, 0.0f, 0.0f));

            float low = -1e30f, high = 1e30f;
            for (int axis = 0; axis < 3; ++axis) {
                const float lo = box.centre[axis] - box.half[axis];
                const float hi = box.centre[axis] + box.half[axis];
                if (std::abs(along[axis]) < 1e-8f) {
                    if (origin[axis] < lo || origin[axis] > hi) return false;   // parallel to these faces and outside them
                    continue;
                }
                float t0 = (lo - origin[axis]) / along[axis];
                float t1 = (hi - origin[axis]) / along[axis];
                if (t0 > t1) std::swap(t0, t1);
                low  = std::max(low, t0);
                high = std::min(high, t1);
                if (low > high) return false;
            }
            bottom = low;    // t is the world height, since the line is (x, t, z)
            top    = high;
            return true;

        }   // ThroughBox()

    }   // ANONYMOUS NAMESPACE

    bool NavGrid::Bake(const NavGridSettings& settings, const std::vector<NavObstacle>& obstacles, const NavTerrain& terrain,
                       const std::vector<NavWalkable>& walkables) {

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
        // 0. The layers: the ground and the Walkable boxes' tops, per cell.
        BuildLayers(terrain, walkables);
        m_static.assign(m_nodeCell.size(), 0);    // all open; obstacles and walls close nodes below
        m_dynamic.assign(m_nodeCell.size(), 0);

        const float cellSize = settings.cellSize;

        // 1. Obstacles: static ones set the static flag; dynamic ones add to
        // the count, so they can be taken off again when they move.
        int counted = 0;
        for (const NavObstacle& obstacle : obstacles) {

            if (!Footprint(obstacle, terrain, m_footprint)) continue;
            ++counted;

            if (obstacle.dynamic) AddDynamic(m_footprint);
            else for (const int index : m_footprint) m_static[static_cast<std::size_t>(index)] = 1;

        }

        // 2. Terrain walls: cliffs and off-map, tested at each cell's centre
        // only (so a cell that is part cliff can still read as open - Jean
        // David's prototype does the same). They close the cell's ground
        // node (a Walkable deck over a cliff stays open). Nodes an obstacle
        // already closed are skipped, so the wall count below is only the
        // walls' own.
        int wallCells = 0;
        for (int row = 0; row < m_rows; ++row) {
            for (int col = 0; col < m_cols; ++col) {

                const NavCell cell{ col, row };
                const int     ground = GroundNode(cell);
                if (ground < 0) continue;
                const std::size_t index = static_cast<std::size_t>(ground);
                if (!IsBlockedIndex(index) && terrain.IsWall(CellToWorldCenter(cell))) {
                    m_static[index] = 1;
                    ++wallCells;
                }

            }
        }

        m_baked = true;

        std::size_t blocked = 0;
        for (std::size_t node = 0; node < m_static.size(); ++node) {
            if (IsBlockedIndex(node)) ++blocked;
        }
        CRU_INFO("NavGrid baked: {} x {} cells of {} m, {} blocked ({} of {} obstacles inside the height band, {} terrain wall cells)",
                 m_cols, m_rows, cellSize, blocked, counted, obstacles.size(), wallCells);

        // Only with Walkable boxes, so a scene without them logs as before.
        if (!walkables.empty()) {
            int upper = 0, layered = 0, noGround = 0;
            for (std::size_t cell = 0; cell < m_layerCount.size(); ++cell) {
                const int extra = static_cast<int>(m_layerCount[cell]) - (m_groundNode[cell] >= 0 ? 1 : 0);
                upper += extra;
                if (extra > 0) ++layered;
                if (m_groundNode[cell] < 0) ++noGround;
            }
            CRU_INFO("NavGrid layers: {} Walkable box(es) give {} raised layer(s) over {} cells; {} cells have no ground layer (a box sits on it)",
                     walkables.size(), upper, layered, noGround);
        }
        return true;

    }   // NavGrid::Bake()

    bool NavGrid::IsBlocked(const NavCell& cell) const {

        // Off the map is a wall: the flood never spreads outside the grid, and
        // an unbaked grid has no open cells to spread through.
        // Until the layered flow field (6d) a cell is asked about as one
        // thing: its ground layer. A cell whose ground is not a layer (a
        // Walkable box sits on it) is a wall to something walking the ground.
        if (!m_baked || !Contains(cell)) return true;
        const int node = GroundNode(cell);
        return node < 0 || IsBlockedIndex(static_cast<std::size_t>(node));

    }   // NavGrid::IsBlocked()

    int NavGrid::GroundNode(const NavCell& cell) const {

        // Not gated on m_baked: the bake uses it (see NodeIndex).
        if (!Contains(cell)) return -1;
        const std::size_t index = static_cast<std::size_t>(Index(cell));
        return index < m_groundNode.size() ? m_groundNode[index] : -1;

    }   // NavGrid::GroundNode()

    /*!
        \brief
        Fills the per-cell layer lists and the per-node height and headroom.

        \details
        For each cell, the vertical line through its centre:
          1. Every Walkable box it passes through is a solid stretch, from
             where it enters the box to where it leaves.
          2. The candidate surfaces are the ground and each box's top, if
             above the ground.
          3. A candidate's headroom is the gap up to the lowest solid stretch
             above it; inside a solid (the ground under a box resting on it)
             it has none.
          4. Kept if the headroom is at least agentHeight, lowest first,
             at most maxLayersPerCell (the extra ones, highest, are dropped
             and reported). Two candidates at the same height are one.
        Boxes are visited only over their own cells, so this costs nothing
        with no Walkable boxes: one ground layer per cell, node i = cell i.
    */
    void NavGrid::BuildLayers(const NavTerrain& terrain, const std::vector<NavWalkable>& walkables) {

        const std::size_t cellCount = static_cast<std::size_t>(m_cols) * static_cast<std::size_t>(m_rows);
        const float       infinity  = std::numeric_limits<float>::infinity();
        const int         maxLayers = std::clamp(m_settings.maxLayersPerCell, 1, 255);

        // 1. Every (cell, solid stretch) pair, from each box's own cells only.
        struct Stretch { int cell; float bottom; float top; };
        std::vector<Stretch> stretches;
        const float cellSize = m_settings.cellSize;
        for (const NavWalkable& box : walkables) {

            // The box's world AABB, to know which cell centres can be inside it.
            Math::vec3 low(1e30f), high(-1e30f);
            for (int corner = 0; corner < 8; ++corner) {
                const Math::vec3 sign((corner & 1) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f, (corner & 4) ? 1.0f : -1.0f);
                const Math::vec3 point(box.world * Math::vec4(box.centre + sign * box.half, 1.0f));
                low  = glm::min(low, point);
                high = glm::max(high, point);
            }

            // Cells whose centre lies inside [low, high]: centre = min + (c + 0.5) * size.
            const int colFirst = std::max(0,          ToCellNumber(std::ceil((low.x - m_minX) / cellSize - 0.5f), m_cols));
            const int colLast  = std::min(m_cols - 1, ToCellNumber(std::floor((high.x - m_minX) / cellSize - 0.5f), m_cols));
            const int rowFirst = std::max(0,          ToCellNumber(std::ceil((low.z - m_minZ) / cellSize - 0.5f), m_rows));
            const int rowLast  = std::min(m_rows - 1, ToCellNumber(std::floor((high.z - m_minZ) / cellSize - 0.5f), m_rows));

            const Math::mat4 toLocal = glm::inverse(box.world);
            for (int row = rowFirst; row <= rowLast; ++row) {
                for (int col = colFirst; col <= colLast; ++col) {
                    const Math::vec3 centre = CellToWorldCenter({ col, row });
                    float bottom = 0.0f, top = 0.0f;
                    if (ThroughBox(box, toLocal, centre.x, centre.z, bottom, top)) stretches.push_back({ Index({ col, row }), bottom, top });
                }
            }

        }
        std::sort(stretches.begin(), stretches.end(), [](const Stretch& a, const Stretch& b) { return a.cell < b.cell; });

        // 2-4. Each cell's layers, cell by cell, into the flat node arrays.
        m_layerStart.resize(cellCount);
        m_layerCount.assign(cellCount, 0);
        m_groundNode.assign(cellCount, -1);
        m_nodeCell.clear();
        m_nodeHeight.clear();
        m_nodeHeadroom.clear();
        m_nodeCell.reserve(cellCount);
        m_nodeHeight.reserve(cellCount);
        m_nodeHeadroom.reserve(cellCount);

        struct Surface { float height; float headroom; bool ground; };
        std::vector<Surface> surfaces;
        int         overCap      = 0;          // cells that had more surfaces than maxLayersPerCell
        Math::vec3  firstOverCap(0.0f);
        std::size_t next = 0;                  // the next stretch in `stretches`

        for (std::size_t cell = 0; cell < cellCount; ++cell) {

            const NavCell    at{ static_cast<int>(cell) % m_cols, static_cast<int>(cell) / m_cols };
            const Math::vec3 centre = CellToWorldCenter(at);
            const float      ground = terrain.GroundHeight(centre);

            // This cell's solid stretches.
            const std::size_t first = next;
            while (next < stretches.size() && stretches[next].cell == static_cast<int>(cell)) ++next;

            // Headroom above a surface: up to the lowest stretch above it;
            // none if the surface is inside a stretch.
            const auto headroomAt = [&](float height) {
                float room = infinity;
                for (std::size_t i = first; i < next; ++i) {
                    if (stretches[i].top <= height + kSameHeight) continue;          // below it (or its own top)
                    if (stretches[i].bottom <= height + kSameHeight) return 0.0f;    // the surface is inside this box
                    room = std::min(room, stretches[i].bottom - height);
                }
                return room;
            };

            surfaces.clear();
            surfaces.push_back({ ground, headroomAt(ground), true });
            for (std::size_t i = first; i < next; ++i) {
                if (stretches[i].top > ground + kSameHeight) surfaces.push_back({ stretches[i].top, headroomAt(stretches[i].top), false });
            }
            std::sort(surfaces.begin(), surfaces.end(), [](const Surface& a, const Surface& b) { return a.height < b.height; });

            m_layerStart[cell] = static_cast<int>(m_nodeCell.size());
            int   kept       = 0;
            float lastHeight = -infinity;
            for (const Surface& surface : surfaces) {
                if (surface.headroom < m_settings.agentHeight) continue;             // no room to stand
                if (surface.height - lastHeight < kSameHeight) continue;             // the same surface twice
                if (kept == maxLayers) {
                    if (overCap++ == 0) firstOverCap = centre;
                    break;
                }
                if (surface.ground) m_groundNode[cell] = static_cast<int>(m_nodeCell.size());
                m_nodeCell.push_back(static_cast<int>(cell));
                m_nodeHeight.push_back(surface.height);
                m_nodeHeadroom.push_back(surface.headroom);
                lastHeight = surface.height;
                ++kept;
            }
            m_layerCount[cell] = static_cast<std::uint8_t>(kept);

        }

        if (overCap > 0) {
            CRU_WARN("NavGrid: {} cell(s) have more than maxLayersPerCell ({}) walkable surfaces - the highest were dropped. "
                     "First at ({:.1f}, {:.1f}); raise maxLayersPerCell or untag a surface there",
                     overCap, maxLayers, firstOverCap.x, firstOverCap.z);
        }

    }   // NavGrid::BuildLayers()

    int NavGrid::LayerCount(const NavCell& cell) const {

        if (!m_baked || !Contains(cell)) return 0;
        return m_layerCount[static_cast<std::size_t>(Index(cell))];

    }   // NavGrid::LayerCount()

    int NavGrid::NodeIndex(const NavCell& cell, int layer) const {

        // Not gated on m_baked like LayerCount: the bake itself looks nodes
        // up before it is finished. Callers outside the bake check IsBaked.
        if (!Contains(cell) || layer < 0) return -1;
        const std::size_t index = static_cast<std::size_t>(Index(cell));
        if (index >= m_layerCount.size() || layer >= m_layerCount[index]) return -1;
        return m_layerStart[index] + layer;

    }   // NavGrid::NodeIndex()

    bool NavGrid::Footprint(const NavObstacle& obstacle, const NavTerrain& terrain, std::vector<int>& cells) const {

        cells.clear();

        // The height band is measured from the ground under the box's
        // centre, so a rock on a hillside is judged against the hill.
        const Math::vec3 centre = (obstacle.min + obstacle.max) * 0.5f;
        const float      ground = terrain.GroundHeight(centre);

        // Entirely below the step (a kerb, the floor itself): walk over it.
        // Entirely above the enemies' heads (a branch, a bridge): walk under it.
        if (obstacle.max.y < ground + m_settings.stepHeight || obstacle.min.y > ground + m_settings.agentHeight) {
            return false;
        }

        // Grow the footprint by the body radius, so the cells left open are
        // ones an enemy's whole body fits in, not just its centre point.
        const float cellSize = m_settings.cellSize;
        const float padding  = m_settings.bodyRadiusPadding;
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
        // simply don't run. Each covered cell's ground layer for now; the
        // layered grid (6d) picks the layers the box actually reaches.
        for (int row = rowFirst; row <= rowLast; ++row) {
            for (int col = colFirst; col <= colLast; ++col) {
                const int node = GroundNode({ col, row });
                if (node >= 0) cells.push_back(node);
            }
        }
        return true;

    }   // NavGrid::Footprint()

    void NavGrid::AddDynamic(const std::vector<int>& cells, std::vector<int>* changed) {

        for (const int cell : cells) {
            const std::size_t index = static_cast<std::size_t>(cell);
            if (index >= m_dynamic.size()) continue;        // a footprint from an older bake: ignore
            if (m_dynamic[index] == UINT16_MAX) continue;   // never wraps round to 0 (and open)
            if (changed && !IsBlockedIndex(index)) changed->push_back(cell);
            ++m_dynamic[index];
        }

    }   // NavGrid::AddDynamic()

    void NavGrid::RemoveDynamic(const std::vector<int>& cells, std::vector<int>* changed) {

        for (const int cell : cells) {
            const std::size_t index = static_cast<std::size_t>(cell);
            if (index >= m_dynamic.size() || m_dynamic[index] == 0) continue;   // never below 0
            --m_dynamic[index];
            if (changed && !IsBlockedIndex(index)) changed->push_back(cell);
        }

    }   // NavGrid::RemoveDynamic()

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
