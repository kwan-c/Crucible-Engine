/*************************************************************************
* \file:         NavDebugDraw.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          17 November 2026
* \version       1.2
* \brief
*                NavDebugDraw: the navigation overlay - the surface's
*                outline, the grid's cells and blocked cells, and the flow
*                field's arrows - as debug lines.
*
* \details
*                Split out of ECSNavigationSystem (build step 3). Draws from
*                the grid, the field and the ground only; it never sees the
*                ECS. Everything is drawn on top (depthTest off), like Unity's
*                NavMesh overlay: a floor slightly higher than the surface, or
*                the obstacles themselves, would otherwise hide the very cells
*                the designer wants to check.
*
*                Gizmos > Nav grid toggles the outline, cells and blocked
*                cells; Gizmos > Flow field arrows toggles the arrows. Use
*                WantsGrid() / WantsArrows() to skip the work entirely when
*                they are off: on a 500 m map the loops visit 62,500 cells.
*
* \history
*    -v1.0       first version (moved out of ECSNavigationSystem, no
*                behaviour change)
*    -v1.1       raised layers drawn in green at their own height
*    -v1.2       arrows per layer at its own height (no terrain needed)
*
**************************************************************************/
#pragma once

// Crucible engine
#include "Navigation/FlowField.h"
#include "Navigation/NavBaker.h"     // NavArea
#include "Navigation/NavGrid.h"
#include "Navigation/NavTerrain.h"

namespace Crucible::NavDebugDraw {

    // Whether the grid / the arrows would be shown this frame.
    bool WantsGrid();
    bool WantsArrows();

    // The surface's (or the baked area's) edge as a bright cyan rectangle at its own height.
    void Outline(const NavArea& area);

    // The cell lines in blue (not grey, which the editor's own world grid
    // uses), each blocked cell as a red square just above the ground, and
    // each raised layer (a Walkable top) as a green square at its height.
    void Grid(const NavGrid& grid, const NavTerrain& ground);

    // One yellow arrow per layer that has a direction, at that layer's
    // height, pointing the way an enemy standing there would go. Only flooded
    // layers have one, so the arrows also show the flood radius.
    void Arrows(const NavGrid& grid, const FlowField& field);

}   // NAMESPACE CRUCIBLE::NAVDEBUGDRAW
