/*************************************************************************
* \file:         NavGrid.h
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.7
* \brief
*                NavGrid: a flat 2D grid over the map where every cell is
*                open or blocked. The bottom layer of enemy navigation.
*
* \details
*                Ported from Jean David's Unity prototype (NavGrid.cs; see
*                his "Enemy navigation: porting reference"). Baked once from
*                a list of obstacle boxes and the terrain, then only ever
*                asked "is this cell blocked?" by the flow field above it.
*
*                Knows nothing about the ECS or physics: whoever bakes it
*                collects the obstacle boxes first. That keeps it testable
*                on its own and lets the procedural generator re-bake it.
*
*                Two layers per cell (build step 4): a static flag (terrain
*                walls and StaticObstacles, stamped once at bake) and a
*                dynamic count (how many DynamicObstacles cover it). A cell
*                is blocked if either says so. A count, not a flag, so that
*                when one of two overlapping obstacles moves away the cell
*                stays blocked by the other. Dynamic obstacles are stamped
*                and unstamped by their footprint: the list of cell indices
*                they block (a list, not a rectangle, so the layered grid
*                can later name one layer of a column and not another).
*
*                Layers (build step 6): a cell is a column that can hold
*                several walkable surfaces - a tunnel floor, the ground, a
*                bridge deck. Each surface is a NODE; a cell's nodes are
*                stored together, lowest first (Recast's compact heightfield:
*                per cell a start index and a count). The static flag and the
*                dynamic count belong to nodes, not cells.
*
*                Where layers come from: the ground (the terrain, or the flat
*                plane), plus the top face of every box collider tagged
*                Walkable (a bridge deck, a ramp, a platform), sampled at
*                each cell's centre - no physics. A surface is a layer only
*                if an enemy fits on it: at least agentHeight of headroom
*                below the next Walkable box above it. So the ground under a
*                platform resting on it is not a layer, and neither is a
*                tunnel floor too low to stand in.
*
*                Links (how an enemy gets from one layer to the next cell's):
*                per node, one link per direction (8), naming one layer of
*                that neighbour or none - Recast's model. Along the same
*                surface (the ground, one ramp, one deck) always; between
*                different surfaces when the height difference where the two
*                cells meet is within stepHeight (both ways), or a drop of up
*                to maxDropHeight (one way, down only). Anything bigger is no
*                link: a wall between them.
*
*                Until the layered flow field (6d), the cell-level questions
*                (IsBlocked, the flow field, obstacles, terrain walls) are
*                about each cell's GROUND layer; a cell whose ground was
*                dropped (a platform sits on it) counts as blocked there. The
*                upper layers are baked and drawn, but nothing walks on them
*                yet.
*
* \history
*    -v1.0       first version
*    -v1.1       static flag + dynamic count per cell (was one blocked
*                byte); Footprint, AddDynamic, RemoveDynamic. The bake
*                blocks the same cells as before
*    -v1.2       shoulderHeight renamed agentHeight
*    -v1.3       build step 6a: layer storage. Each cell holds a list of
*                layers (walkable surfaces), each layer a node with its own
*                height, headroom, static flag and dynamic count. Today every
*                cell has exactly one layer (the ground), so node i is cell
*                i's ground and everything blocks the same cells as before
*    -v1.4       build step 6b: layers from Walkable boxes (their top faces),
*                kept when they have agentHeight of headroom, at most
*                maxLayersPerCell per cell; the ground layer is dropped where
*                a Walkable box sits on it. GroundNode(); the cell-level
*                questions read the ground layer
*    -v1.5       build step 6c: links. Each node stores, per neighbour
*                direction, which layer it connects to there (climb, walk
*                along, or a one-way drop of up to maxDropHeight); LinkedNode
*    -v1.6       build step 6d-1: NodeAt (the layer under a position);
*                obstacles block the layers whose height band they reach
*    -v1.7       build step 6d-2: IsWalkable and IsPathClear (moved here
*                from ECSNavigationSystem) go by layers and links
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <vector>

// Crucible engine
#include "Math/Math.h"

// ---------------------------------------------------------------------------
// Layout (top view, XZ plane - Y is ignored):
//
//   The grid is a width x depth rectangle centred on origin. Columns run along
//   +X, rows along +Z, and the cells live in one flat array, row by row:
//   index = row * cols + col. Anything outside the rectangle counts as blocked,
//   so an enemy can never be routed off the map.
//
// Not exported to Game (no ENGINE_API): gameplay only ever touches the
// components, so this class stays inside the engine and can hold a std::vector
// without the pimpl the exported physics class needed to avoid warning C4251.
// ---------------------------------------------------------------------------

namespace Crucible {

    class NavTerrain;

    // One obstacle's footprint: its world axis-aligned bounding box. A rotated
    // box gives a slightly bigger AABB than itself - deliberately conservative.
    struct NavObstacle {

        Math::vec3 min = Math::vec3(0.0f);
        Math::vec3 max = Math::vec3(0.0f);
        bool       dynamic = false;   // DynamicObstacle: counted in the dynamic layer, so it can be unstamped later

    };  // NAVOBSTACLE STRUCT

    // A box collider tagged Walkable: its top face is a walkable surface. The
    // shape's local box (centre, half size), placed by the entity's world matrix.
    struct NavWalkable {

        Math::mat4 world  = Math::mat4(1.0f);
        Math::vec3 centre = Math::vec3(0.0f);
        Math::vec3 half   = Math::vec3(0.5f);

    };  // NAVWALKABLE STRUCT

    // The bake's inputs. Defaults are Jean David's port baseline; later these
    // become FlowFieldSurface's Inspector fields.
    struct NavGridSettings {

        Math::vec3 origin            = Math::vec3(0.0f);  // centre of the grid
        float      width             = 500.0f;            // along X, metres
        float      depth             = 500.0f;            // along Z, metres
        float      cellSize          = 2.0f;              // 500 m at 2 m = 62,500 cells
        float      bodyRadiusPadding = 0.6f;              // the enemy's body radius: a cell is open only if a body fits
        float      stepHeight        = 0.3f;              // obstacles entirely below ground + this are stepped over
        float      agentHeight       = 1.6f;              // the enemies' height: obstacles entirely above ground + this are walked under; also the headroom a layer needs
        int        maxLayersPerCell  = 4;                 // walkable surfaces kept per cell (the lowest ones), ground included
        float      maxDropHeight     = 3.0f;              // the furthest an enemy drops to a lower surface (one-way); more is a wall

    };  // NAVGRIDSETTINGS STRUCT

    // A cell's column and row. May be outside the grid: check with Contains().
    struct NavCell {

        int col = 0;
        int row = 0;

    };  // NAVCELL STRUCT

    // The 8 neighbour directions, in Jean David's order: right, left, up, down,
    // then the four diagonals (indices 4-7). Links are stored in this order,
    // the same one FlowField floods in.
    inline constexpr int kNavDirections        = 8;
    inline constexpr int kNavDirCol[kNavDirections] = { 1, -1, 0,  0, 1, -1,  1, -1 };
    inline constexpr int kNavDirRow[kNavDirections] = { 0,  0, 1, -1, 1,  1, -1, -1 };
    inline constexpr int kNavOpposite[kNavDirections] = { 1, 0, 3, 2, 7, 6, 5, 4 };   // the direction back

    class NavGrid {

    public:

        /*!
            \brief
            Rebuilds every cell from scratch: obstacles first (static ones
            into the static flag, dynamic ones into the dynamic count), then
            terrain walls (static) on the cells still open. Call at startup,
            and again whenever the terrain is regenerated.

            \param settings
            Size, cell size, body padding and the step / agent-height band.

            \param obstacles
            World AABBs of everything that might block. The height band decides
            which of them really do.

            \param terrain
            Wall test and ground height. FlatNavTerrain when there is no terrain.

            \param walkables
            Box colliders tagged Walkable: each one's top face is a layer
            where an enemy fits on it (see the file's details).

            \return
            False if the settings make no sense; the grid is then left unbaked.
        */
        bool Bake(const NavGridSettings& settings, const std::vector<NavObstacle>& obstacles, const NavTerrain& terrain,
                  const std::vector<NavWalkable>& walkables = {});

        bool  IsBaked()  const { return m_baked; }
        int   Cols()     const { return m_cols; }
        int   Rows()     const { return m_rows; }
        float CellSize() const { return m_settings.cellSize; }

        bool  Contains(const NavCell& cell) const { return cell.col >= 0 && cell.row >= 0 && cell.col < m_cols && cell.row < m_rows; }
        int   Index(const NavCell& cell)    const { return cell.row * m_cols + cell.col; }   // only valid when Contains(cell)

        // The cell's ground layer is blocked - or it has none (a Walkable box
        // sits on it). Out of range, or not baked yet, counts as blocked.
        bool  IsBlocked(const NavCell& cell) const;
        bool  IsBlockedWorld(const Math::vec3& position) const { return IsBlocked(WorldToCell(position)); }

        // The cell under a world position. Can be outside the grid.
        NavCell    WorldToCell(const Math::vec3& position) const;

        // The nearest cell inside the grid (for a goal that wandered off the edge).
        NavCell    ClampToGrid(const NavCell& cell) const;

        // The middle of a cell, at the grid origin's height.
        Math::vec3 CellToWorldCenter(const NavCell& cell) const;

        // --- Layers (nodes) -------------------------------------------------- //

        // How many walkable surfaces the cell holds (0 off the grid or unbaked).
        int   LayerCount(const NavCell& cell) const;

        // The node of a cell's layer, counted from the lowest (0); -1 if there is none.
        int   NodeIndex(const NavCell& cell, int layer) const;

        /*!
            \brief
            The layer under a position: the highest one at or a little below
            its height (feet or body centre both work - layers in one cell
            are at least agentHeight apart). Lower than every layer: the
            lowest one. -1 off the grid, unbaked, or a cell with no layer.
        */
        int   NodeAt(const Math::vec3& position) const { return NodeAt(WorldToCell(position), position.y); }
        int   NodeAt(const NavCell& cell, float height) const;

        // --- Queries (C# Navigation, through ECSNavigationSystem) ----------- //

        /*!
            \brief
            Could an enemy stand here? The layer under the position is open,
            and on the ground the terrain is not a wall at that exact point.
            False unbaked, off the grid, or in a cell with no layer.
        */
        bool IsWalkable(const Math::vec3& position, const NavTerrain& terrain) const;

        /*!
            \brief
            Could an enemy walk in a straight line from `from` to `to`? Samples
            every half cell (at least 20 cm), following the links from the
            layer under `from`: no link that way (a wall, a ledge up, a drop
            higher than maxDropHeight, the map's edge) or a blocked layer
            fails it, and so does a terrain wall on the ground. It must end
            on the layer under `to`: from the ground under a bridge to its
            deck is not a clear path.

            \details
            An enemy pushed into a blocked cell (by the crowd or a blast) must
            still be able to walk OUT, so blocked samples at the very start of
            the line are allowed; once it has reached an open layer, the next
            blocked one fails it. Without that it would be stuck forever,
            every way out "blocked". On a grid with only the ground this is
            exactly the cell-by-cell check it was before layers.
        */
        bool IsPathClear(const Math::vec3& from, const Math::vec3& to, const NavTerrain& terrain) const;

        // The node of the cell's ground (terrain or flat floor); -1 if the
        // ground there is not a layer (a Walkable box sits on it) or off the grid.
        int   GroundNode(const NavCell& cell) const;

        int     NodeCount()                const { return static_cast<int>(m_nodeCell.size()); }
        NavCell NodeCell(int node)         const { return { m_nodeCell[static_cast<std::size_t>(node)] % m_cols,
                                                            m_nodeCell[static_cast<std::size_t>(node)] / m_cols }; }
        float   NodeHeight(int node)       const { return m_nodeHeight[static_cast<std::size_t>(node)]; }     // the surface's height at the cell's centre
        float   NodeHeadroom(int node)     const { return m_nodeHeadroom[static_cast<std::size_t>(node)]; }   // free height above it; infinity when nothing is above
        bool    IsNodeBlocked(int node)    const { return IsBlockedIndex(static_cast<std::size_t>(node)); }

        // The node this one links to in direction `dir` (kNavDirCol/Row), or
        // -1: no layer there an enemy can step, walk or drop to.
        int     LinkedNode(int node, int dir) const;

        // Whether the node is the ground (terrain or flat floor), not a Walkable top.
        bool    IsGroundNode(int node)     const { return m_nodeSource[static_cast<std::size_t>(node)] < 0; }

        // Whether two nodes are on the same surface: both the ground, or the same Walkable box.
        bool    IsSameSurface(int a, int b) const { return m_nodeSource[static_cast<std::size_t>(a)] == m_nodeSource[static_cast<std::size_t>(b)]; }

        // --- Dynamic obstacles ---------------------------------------------- //

        /*!
            \brief
            The nodes an obstacle blocks: in every cell under its box grown
            by bodyRadiusPadding, each layer whose height band [layer +
            stepHeight, layer + agentHeight] the box reaches into. A crate on
            the bridge blocks the deck, not the ground under it; a tall wall
            beside it blocks both.

            \param terrain
            Only for an obstacle entirely off the grid: its band is then
            judged against the ground under its centre, for the bake's count.

            \param cells
            Cleared, then filled with the node indices. Empty when it is
            outside every band or off the grid.

            \return
            Whether it reaches into a band (even if off the grid).
        */
        bool Footprint(const NavObstacle& obstacle, const NavTerrain& terrain, std::vector<int>& cells) const;

        /*!
            \brief
            Adds / removes one dynamic obstacle's footprint (+1 / -1 on each
            node's count). Always remove exactly what was added.

            \param changed
            If given, every node that went from open to blocked (Add) or
            blocked to open (Remove) is appended - what the cost field and
            the flow must hear about. A static node never changes.
        */
        void AddDynamic(const std::vector<int>& cells, std::vector<int>* changed = nullptr);
        void RemoveDynamic(const std::vector<int>& cells, std::vector<int>* changed = nullptr);

    private:

        bool IsBlockedIndex(std::size_t index) const { return m_static[index] != 0 || m_dynamic[index] != 0; }
        void BuildLayers(const NavTerrain& terrain, const std::vector<NavWalkable>& walkables);
        void BuildLinks(const NavTerrain& terrain, const std::vector<NavWalkable>& walkables);

        NavGridSettings           m_settings;
        int                       m_cols    = 0;
        int                       m_rows    = 0;
        float                     m_minX    = 0.0f;   // the grid's left edge (smallest X)
        float                     m_minZ    = 0.0f;   // the grid's back edge (smallest Z)
        // Per cell: where its nodes start, and how many (lowest layer first).
        std::vector<int>           m_layerStart;
        std::vector<std::uint8_t>  m_layerCount;
        std::vector<int>           m_groundNode;    // its ground layer's node, or -1

        // Per node.
        std::vector<int>           m_nodeCell;      // the cell it is in (Index())
        std::vector<float>         m_nodeHeight;    // the surface's height at the cell's centre
        std::vector<float>         m_nodeHeadroom;  // free height above it; infinity when nothing is above
        std::vector<int>           m_nodeSource;    // where the surface comes from: -1 the ground, else the Walkable box's index
        std::vector<std::uint8_t>  m_links;         // per node, 8 entries: the linked layer in that neighbour, or kNoLink
        std::vector<std::uint8_t>  m_static;        // 1 = terrain wall or StaticObstacle; uint8_t, not vector<bool>, so reads are plain loads
        std::vector<std::uint16_t> m_dynamic;       // how many DynamicObstacles cover the node

        std::vector<int>           m_footprint;     // one obstacle's nodes during a bake (memory reused)
        bool                       m_baked   = false;

    };  // NAVGRID CLASS

}   // NAMESPACE CRUCIBLE
