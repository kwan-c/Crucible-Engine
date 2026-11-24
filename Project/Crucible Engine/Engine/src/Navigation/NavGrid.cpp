/*************************************************************************
* \file:         NavGrid.cpp
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.7
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
*    -v1.5       BuildLinks: per node and direction, the neighbour layer it
*                connects to (same surface; within a step where the cells
*                meet; or a one-way drop up to maxDropHeight)
*    -v1.6       NodeAt; Footprint judges each layer's own band. On flat
*                ground with one layer the same cells as before
*    -v1.7       IsWalkable, IsPathClear (from ECSNavigationSystem), by
*                layers and links; a path must end on the layer at its end
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

        // A link entry meaning "no link in this direction".
        constexpr std::uint8_t kNoLink = 0xFF;

        // NodeAt: a layer this far above a position still counts as under it,
        // so a position at the feet, or a little below a ramp's surface, gets
        // that surface. Layers in one cell are at least agentHeight apart.
        constexpr float kStandSlack = 0.5f;

        // Points along the line between two cells' centres where a climb is
        // looked for: every quarter metre on 2 m cells.
        constexpr int kClimbSamples = 8;

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
        // 0. The layers: the ground and the Walkable boxes' tops, per cell,
        // and the links between neighbouring cells' layers.
        BuildLayers(terrain, walkables);
        BuildLinks(terrain, walkables);
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

            // Links between different surfaces: a link with one back is a
            // climb (counted once), one without is a drop.
            int climbs = 0, drops = 0;
            for (int node = 0; node < NodeCount(); ++node) {
                for (int dir = 0; dir < kNavDirections; ++dir) {
                    const int other = LinkedNode(node, dir);
                    if (other < 0 || m_nodeSource[static_cast<std::size_t>(other)] == m_nodeSource[static_cast<std::size_t>(node)]) continue;
                    if (LinkedNode(other, kNavOpposite[dir]) != node) ++drops;
                    else if (node < other)                             ++climbs;
                }
            }
            CRU_INFO("NavGrid links between surfaces: {} climb(s) (two-way, within {} m), {} drop(s) (one-way, up to {} m)",
                     climbs, m_settings.stepHeight, drops, m_settings.maxDropHeight);
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

    bool NavGrid::IsWalkable(const Math::vec3& position, const NavTerrain& terrain) const {

        const int node = NodeAt(position);
        if (node < 0 || IsNodeBlocked(node)) return false;

        // On the ground the terrain's own wall test, at this exact point (a
        // cell can be open with a cliff in one corner). A deck over a cliff
        // is walkable whatever the ground below it is.
        return !(IsGroundNode(node) && terrain.IsWall(position));

    }   // NavGrid::IsWalkable()

    bool NavGrid::IsPathClear(const Math::vec3& from, const Math::vec3& to, const NavTerrain& terrain) const {

        if (!m_baked) return false;

        // Half a cell per step (at least 20 cm), so the line can't skip over a
        // one-cell wall between two samples - and never crosses more than one
        // cell per axis between two samples. Unity's spacing exactly.
        const Math::vec3 delta  = to - from;
        const float      length = glm::length(Math::vec3(delta.x, 0.0f, delta.z));
        const int        steps  = std::max(1, static_cast<int>(std::ceil(length / std::max(0.2f, m_settings.cellSize * 0.5f))));

        // The layer it walks on, carried along the links from cell to cell.
        // -1: lost (no link where the line goes: off the map, a ledge, a wall).
        int node = NodeAt(from);

        // Still inside the blocked cells it started in: those don't count (see the header).
        bool leavingBlockedStart = node < 0 || IsNodeBlocked(node);
        for (int i = 0; i <= steps; ++i) {

            const Math::vec3 sample = from + delta * (static_cast<float>(i) / static_cast<float>(steps));
            const NavCell    want   = WorldToCell(sample);

            // Walk the node over to the sample's cell, one link at a time.
            while (node >= 0) {
                const NavCell at = NodeCell(node);
                if (at.col == want.col && at.row == want.row) break;
                const int dc = (want.col > at.col) - (want.col < at.col);
                const int dr = (want.row > at.row) - (want.row < at.row);
                int dir = 0;
                while (kNavDirCol[dir] != dc || kNavDirRow[dir] != dr) ++dir;
                node = LinkedNode(node, dir);
            }

            // Still allowed to be anywhere (leaving a blocked start): pick
            // the layer up again where the line is now.
            if (node < 0 && leavingBlockedStart) node = NodeAt(sample);

            // The terrain's own walls apply on the ground (and where the line
            // has no layer, as off the map).
            if ((node < 0 || IsGroundNode(node)) && terrain.IsWall(sample)) return false;

            if (node < 0 || IsNodeBlocked(node)) {
                if (!leavingBlockedStart) return false;
            } else {
                leavingBlockedStart = false;
            }

        }

        // It must end on the layer at `to`, not just in its cell: from the
        // ground under a bridge to someone standing on its deck is not clear.
        // (With only the ground there is one layer per cell, so this never
        // fails there.)
        const int end = NodeAt(to);
        return node < 0 || end < 0 || node == end;

    }   // NavGrid::IsPathClear()

    int NavGrid::NodeAt(const NavCell& cell, float height) const {

        const int layers = LayerCount(cell);
        if (layers == 0) return -1;

        // Lowest first: keep climbing while the next layer is still at or
        // (a little) below the height.
        int node = NodeIndex(cell, 0);
        for (int layer = 1; layer < layers; ++layer) {
            const int next = NodeIndex(cell, layer);
            if (m_nodeHeight[static_cast<std::size_t>(next)] > height + kStandSlack) break;
            node = next;
        }
        return node;

    }   // NavGrid::NodeAt()

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
        struct Stretch { int cell; float bottom; float top; int box; };
        std::vector<Stretch> stretches;
        const float cellSize = m_settings.cellSize;
        for (std::size_t boxIndex = 0; boxIndex < walkables.size(); ++boxIndex) {

            const NavWalkable& box = walkables[boxIndex];

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
                    if (ThroughBox(box, toLocal, centre.x, centre.z, bottom, top)) stretches.push_back({ Index({ col, row }), bottom, top, static_cast<int>(boxIndex) });
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
        m_nodeSource.clear();
        m_nodeSource.reserve(cellCount);
        m_nodeCell.reserve(cellCount);
        m_nodeHeight.reserve(cellCount);
        m_nodeHeadroom.reserve(cellCount);

        struct Surface { float height; float headroom; int source; };   // source: -1 the ground, else the box
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
            surfaces.push_back({ ground, headroomAt(ground), -1 });
            for (std::size_t i = first; i < next; ++i) {
                if (stretches[i].top > ground + kSameHeight) surfaces.push_back({ stretches[i].top, headroomAt(stretches[i].top), stretches[i].box });
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
                if (surface.source < 0) m_groundNode[cell] = static_cast<int>(m_nodeCell.size());
                m_nodeCell.push_back(static_cast<int>(cell));
                m_nodeHeight.push_back(surface.height);
                m_nodeHeadroom.push_back(surface.headroom);
                m_nodeSource.push_back(surface.source);
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

    /*!
        \brief
        For every node and each of the 8 directions, the neighbour cell's
        layer it connects to, or none.

        \details
        The rules, per neighbour layer:
          - the same surface (both the ground, or the same Walkable box):
            always - the ground's own walls come from the generator, and a
            Walkable surface is walkable along its whole top (the tag);
          - a climb, two-way: somewhere on the line between the two cells'
            centres, both surfaces are there and within stepHeight of each
            other. Anywhere, not only at the centres or the shared edge: a
            ramp's tip lands on the ground part-way across a cell, already
            higher than a step by the time it reaches the edge;
          - a drop, one way (only from the higher layer): where the two
            cells meet (the middle of the shared edge, or the shared corner),
            lower by more than a step but at most maxDropHeight. A box that
            doesn't reach that point is measured at the furthest point it
            does reach on the way (its edge), found by halving;
          - otherwise: no link.
        Of several that qualify the same surface wins, then the closest in
        height. One link per direction (Recast's model).
    */
    void NavGrid::BuildLinks(const NavTerrain& terrain, const std::vector<NavWalkable>& walkables) {

        m_links.assign(m_nodeCell.size() * kNavDirections, kNoLink);

        std::vector<Math::mat4> toLocal;
        toLocal.reserve(walkables.size());
        for (const NavWalkable& box : walkables) toLocal.push_back(glm::inverse(box.world));

        // A node's surface height at (x, z), or NaN where its box isn't (the
        // ground is everywhere).
        const auto surfaceIfThere = [&](int node, float x, float z) {
            const int source = m_nodeSource[static_cast<std::size_t>(node)];
            if (source < 0) return terrain.GroundHeight({ x, 0.0f, z });
            float bottom = 0.0f, top = 0.0f;
            if (ThroughBox(walkables[static_cast<std::size_t>(source)], toLocal[static_cast<std::size_t>(source)], x, z, bottom, top)) return top;
            return std::numeric_limits<float>::quiet_NaN();
        };

        // A node's surface height at (x, z), or at the furthest point towards
        // it that its box still covers. Its own cell's centre is always covered.
        const auto surfaceAt = [&](int node, const Math::vec3& centre, float x, float z) {
            const int source = m_nodeSource[static_cast<std::size_t>(node)];
            if (source < 0) return terrain.GroundHeight({ x, 0.0f, z });
            const NavWalkable& box   = walkables[static_cast<std::size_t>(source)];
            const Math::mat4&  local = toLocal[static_cast<std::size_t>(source)];
            float bottom = 0.0f, top = 0.0f;
            if (ThroughBox(box, local, x, z, bottom, top)) return top;
            float covered = 0.0f, missed = 1.0f, height = m_nodeHeight[static_cast<std::size_t>(node)];
            for (int i = 0; i < 10; ++i) {
                const float t = (covered + missed) * 0.5f;
                if (ThroughBox(box, local, centre.x + (x - centre.x) * t, centre.z + (z - centre.z) * t, bottom, top)) { covered = t; height = top; }
                else                                                                                                   { missed  = t; }
            }
            return height;
        };

        const float step = m_settings.stepHeight;
        const float drop = std::max(m_settings.maxDropHeight, step);

        for (int node = 0; node < static_cast<int>(m_nodeCell.size()); ++node) {

            const int        cellIndex = m_nodeCell[static_cast<std::size_t>(node)];
            const NavCell    cell{ cellIndex % m_cols, cellIndex / m_cols };
            const Math::vec3 centre = CellToWorldCenter(cell);
            const int        source = m_nodeSource[static_cast<std::size_t>(node)];

            for (int dir = 0; dir < kNavDirections; ++dir) {

                const NavCell next{ cell.col + kNavDirCol[dir], cell.row + kNavDirRow[dir] };
                const int     layers = Contains(next) ? m_layerCount[static_cast<std::size_t>(Index(next))] : 0;
                if (layers == 0) continue;

                // Where the two cells meet: half a cell along the direction.
                const float meetX = centre.x + static_cast<float>(kNavDirCol[dir]) * m_settings.cellSize * 0.5f;
                const float meetZ = centre.z + static_cast<float>(kNavDirRow[dir]) * m_settings.cellSize * 0.5f;
                const Math::vec3 nextCentre = CellToWorldCenter(next);
                float here = std::numeric_limits<float>::quiet_NaN();   // this node's height there, worked out once if needed

                int   best      = -1;
                float bestDelta = 0.0f;
                for (int layer = 0; layer < layers; ++layer) {

                    const int other = m_layerStart[static_cast<std::size_t>(Index(next))] + layer;
                    const int otherSource = m_nodeSource[static_cast<std::size_t>(other)];

                    if (otherSource == source) { best = layer; break; }   // the same surface: always, and best

                    // A climb: the closest the two surfaces come along the
                    // line between the centres (9 points), where both are there.
                    float closest = std::numeric_limits<float>::infinity();
                    for (int k = 0; k <= kClimbSamples; ++k) {
                        const float t = static_cast<float>(k) / static_cast<float>(kClimbSamples);
                        const float x = centre.x + (nextCentre.x - centre.x) * t;
                        const float z = centre.z + (nextCentre.z - centre.z) * t;
                        const float a = surfaceIfThere(node, x, z);
                        const float b = surfaceIfThere(other, x, z);
                        if (!std::isnan(a) && !std::isnan(b)) closest = std::min(closest, std::abs(b - a));
                    }
                    const bool climb = closest <= step;

                    // A drop: lower by more than a step, measured where the cells meet.
                    bool  falls = false;
                    float delta = 0.0f;
                    if (!climb) {
                        if (std::isnan(here)) here = surfaceAt(node, centre, meetX, meetZ);
                        delta = surfaceAt(other, nextCentre, meetX, meetZ) - here;   // + up, - down
                        falls = delta < -step && delta >= -drop;
                    }
                    if (!climb && !falls) continue;

                    // Climbs before drops, then the closest in height.
                    const float score = climb ? closest : 1000.0f + std::abs(delta);
                    if (best < 0 || score < bestDelta) { best = layer; bestDelta = score; }

                }
                if (best >= 0) m_links[static_cast<std::size_t>(node) * kNavDirections + static_cast<std::size_t>(dir)] = static_cast<std::uint8_t>(best);

            }

        }

    }   // NavGrid::BuildLinks()

    int NavGrid::LinkedNode(int node, int dir) const {

        if (node < 0 || static_cast<std::size_t>(node) >= m_nodeCell.size() || dir < 0 || dir >= kNavDirections) return -1;
        const std::uint8_t layer = m_links[static_cast<std::size_t>(node) * kNavDirections + static_cast<std::size_t>(dir)];
        if (layer == kNoLink) return -1;
        const int     cellIndex = m_nodeCell[static_cast<std::size_t>(node)];
        const NavCell next{ cellIndex % m_cols + kNavDirCol[dir], cellIndex / m_cols + kNavDirRow[dir] };
        return NodeIndex(next, layer);

    }   // NavGrid::LinkedNode()

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

        // Entirely off the grid: nothing to block, but the bake still counts
        // whether it is in the band - judged against the ground under its centre.
        if (colFirst > colLast || rowFirst > rowLast) {
            const Math::vec3 centre = (obstacle.min + obstacle.max) * 0.5f;
            const float      ground = terrain.GroundHeight(centre);
            return !(obstacle.max.y < ground + m_settings.stepHeight || obstacle.min.y > ground + m_settings.agentHeight);
        }

        // Each covered cell's layers whose band the box reaches into, so it
        // is judged against the surface it stands on - the hill under it, or
        // the bridge deck. Entirely below a layer's step (a kerb, the floor
        // itself): walked over. Entirely above its agent height (a branch, the
        // bridge over the ground): walked under.
        bool inBand = false;
        for (int row = rowFirst; row <= rowLast; ++row) {
            for (int col = colFirst; col <= colLast; ++col) {
                for (int layer = 0; layer < static_cast<int>(m_layerCount[static_cast<std::size_t>(Index({ col, row }))]); ++layer) {
                    const int   node   = NodeIndex({ col, row }, layer);
                    const float height = m_nodeHeight[static_cast<std::size_t>(node)];
                    if (obstacle.max.y < height + m_settings.stepHeight || obstacle.min.y > height + m_settings.agentHeight) continue;
                    cells.push_back(node);
                    inBand = true;
                }
            }
        }
        return inBand;

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
