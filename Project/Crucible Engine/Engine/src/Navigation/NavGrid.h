/*************************************************************************
* \file:         NavGrid.h
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.3
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
*                dynamic count belong to nodes, not cells. Until the layered
*                bake (6b) every cell has one node, its ground, and node i is
*                cell i - which is what the cell-based callers (the flow
*                field, the debug view) rely on until 6d.
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

    // The bake's inputs. Defaults are Jean David's port baseline; later these
    // become FlowFieldSurface's Inspector fields.
    struct NavGridSettings {

        Math::vec3 origin            = Math::vec3(0.0f);  // centre of the grid
        float      width             = 500.0f;            // along X, metres
        float      depth             = 500.0f;            // along Z, metres
        float      cellSize          = 2.0f;              // 500 m at 2 m = 62,500 cells
        float      bodyRadiusPadding = 0.6f;              // the enemy's body radius: a cell is open only if a body fits
        float      stepHeight        = 0.3f;              // obstacles entirely below ground + this are stepped over
        float      agentHeight       = 1.6f;              // the enemies' height: obstacles entirely above ground + this are walked under

    };  // NAVGRIDSETTINGS STRUCT

    // A cell's column and row. May be outside the grid: check with Contains().
    struct NavCell {

        int col = 0;
        int row = 0;

    };  // NAVCELL STRUCT

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

            \return
            False if the settings make no sense; the grid is then left unbaked.
        */
        bool Bake(const NavGridSettings& settings, const std::vector<NavObstacle>& obstacles, const NavTerrain& terrain);

        bool  IsBaked()  const { return m_baked; }
        int   Cols()     const { return m_cols; }
        int   Rows()     const { return m_rows; }
        float CellSize() const { return m_settings.cellSize; }

        bool  Contains(const NavCell& cell) const { return cell.col >= 0 && cell.row >= 0 && cell.col < m_cols && cell.row < m_rows; }
        int   Index(const NavCell& cell)    const { return cell.row * m_cols + cell.col; }   // only valid when Contains(cell)

        // Out of range, or not baked yet, counts as blocked.
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

        int     NodeCount()                const { return static_cast<int>(m_nodeCell.size()); }
        NavCell NodeCell(int node)         const { return { m_nodeCell[static_cast<std::size_t>(node)] % m_cols,
                                                            m_nodeCell[static_cast<std::size_t>(node)] / m_cols }; }
        float   NodeHeight(int node)       const { return m_nodeHeight[static_cast<std::size_t>(node)]; }     // the surface's height at the cell's centre
        float   NodeHeadroom(int node)     const { return m_nodeHeadroom[static_cast<std::size_t>(node)]; }   // free height above it; infinity when nothing is above
        bool    IsNodeBlocked(int node)    const { return IsBlockedIndex(static_cast<std::size_t>(node)); }

        // --- Dynamic obstacles ---------------------------------------------- //

        /*!
            \brief
            The nodes an obstacle blocks: its box grown by bodyRadiusPadding,
            if it reaches into the height band [ground + stepHeight,
            ground + agentHeight] (ground under its centre). Today that is
            each covered cell's ground layer.

            \param cells
            Cleared, then filled with the node indices. Empty when it is
            outside the band or off the grid.

            \return
            Whether it is inside the height band (even if off the grid).
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

        NavGridSettings           m_settings;
        int                       m_cols    = 0;
        int                       m_rows    = 0;
        float                     m_minX    = 0.0f;   // the grid's left edge (smallest X)
        float                     m_minZ    = 0.0f;   // the grid's back edge (smallest Z)
        // Per cell: where its nodes start, and how many (lowest layer first).
        std::vector<int>           m_layerStart;
        std::vector<std::uint8_t>  m_layerCount;

        // Per node.
        std::vector<int>           m_nodeCell;      // the cell it is in (Index())
        std::vector<float>         m_nodeHeight;    // the surface's height at the cell's centre
        std::vector<float>         m_nodeHeadroom;  // free height above it; infinity when nothing is above
        std::vector<std::uint8_t>  m_static;        // 1 = terrain wall or StaticObstacle; uint8_t, not vector<bool>, so reads are plain loads
        std::vector<std::uint16_t> m_dynamic;       // how many DynamicObstacles cover the node

        std::vector<int>           m_footprint;     // one obstacle's nodes during a bake (memory reused)
        bool                       m_baked   = false;

    };  // NAVGRID CLASS

}   // NAMESPACE CRUCIBLE
