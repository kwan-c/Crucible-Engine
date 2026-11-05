/*************************************************************************
* \file:         NavGrid.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.0
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
* \history
*    -v1.0       first version
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
        float      shoulderHeight    = 1.6f;              // obstacles entirely above ground + this are walked under

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
            Rebuilds every cell from scratch: obstacles first, then terrain walls.
            Call at startup, and again whenever the terrain is regenerated.

            \param settings
            Size, cell size, body padding and the step/shoulder height band.

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

    private:

        NavGridSettings           m_settings;
        int                       m_cols    = 0;
        int                       m_rows    = 0;
        float                     m_minX    = 0.0f;   // the grid's left edge (smallest X)
        float                     m_minZ    = 0.0f;   // the grid's back edge (smallest Z)
        std::vector<std::uint8_t> m_blocked;           // 1 = blocked, one per cell; uint8_t, not vector<bool>, so reads are plain loads
        bool                      m_baked   = false;

    };  // NAVGRID CLASS

}   // NAMESPACE CRUCIBLE
