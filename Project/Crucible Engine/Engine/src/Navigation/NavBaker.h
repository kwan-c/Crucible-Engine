/*************************************************************************
* \file:         NavBaker.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          17 November 2026
* \version       1.0
* \brief
*                NavBaker: turns what the scene says (the Nav Surface's
*                settings and area, the obstacles, the generated terrain)
*                into a baked NavGrid and the flow field's cost field.
*
* \details
*                Split out of ECSNavigationSystem (build step 3). Like NavGrid
*                and FlowField it never sees the ECS: the system reads the
*                scene, fills a NavBakeInput with plain data, and hands it
*                over, so the bake can be tested on its own.
*
*                It also owns "the ground" that every other part asks
*                (Terrain()): the generated terrain's grid when there is one,
*                else the flat plane at the surface's height.
*
*                With a generated terrain the grid covers exactly the terrain
*                (its corners in world space), whatever the surface's scale;
*                without one, the surface's own area.
*
*                Not exported (no ENGINE_API), like NavGrid: only the engine
*                uses it.
*
* \history
*    -v1.0       first version (moved out of ECSNavigationSystem::Bake, no
*                behaviour change)
*
**************************************************************************/
#pragma once

// STL
#include <memory>       // std::shared_ptr
#include <vector>

// Crucible engine
#include "Math/Math.h"
#include "Navigation/FlowField.h"
#include "Navigation/NavGrid.h"
#include "Navigation/NavTerrain.h"
#include "Navigation/TerrainGridNavTerrain.h"

namespace Crucible {

    // A flat rectangle on the ground: centre, and size along X and Z in metres.
    struct NavArea {

        Math::vec3 centre = Math::vec3(0.0f);
        float      width  = 0.0f;
        float      depth  = 0.0f;

    };  // NAVAREA STRUCT

    // Everything a bake needs, read off the scene by ECSNavigationSystem.
    struct NavBakeInput {

        NavArea  surfaceArea;                 // the Nav Surface's own area (its position and X/Z scale)
        float    cellSize          = 2.0f;    // the surface's settings
        float    bodyRadiusPadding = 0.6f;
        float    stepHeight        = 0.3f;
        float    shoulderHeight    = 1.6f;
        float    wallAdjacencyCost = 4.0f;

        std::shared_ptr<const TerrainGrid> terrainGrid;                 // the generated terrain, or null
        Math::mat4                         terrainMatrix = Math::mat4(1.0f);   // its world matrix

    };  // NAVBAKEINPUT STRUCT

    class NavBaker {

    public:

        NavBaker() = default;

        // m_terrain points at a member: copying would leave it pointing at the old object.
        NavBaker(const NavBaker&)            = delete;
        NavBaker& operator=(const NavBaker&) = delete;

        /*!
            \brief
            Bakes the grid, then the flow field's cost field (it depends on
            nothing but the grid, so it is built once here, not on every
            flow rebuild).

            \param obstacles
            The tagged obstacles' world boxes.

            \return
            False if NavGrid refused the settings (it logs why); the field is
            then cleared.
        */
        bool Bake(const NavBakeInput& input, const std::vector<NavObstacle>& obstacles, NavGrid& grid, FlowField& field);

        // Forgets the terrain (the surface is gone): the ground is flat at 0 again.
        void Clear();

        // The ground every user asks: the terrain's grid, or the flat plane.
        const NavTerrain& Terrain() const { return *m_terrain; }

        // Whether the last bake covered a generated terrain's bounds (not the
        // surface's scale), and the area it covered - for the outline.
        bool           FittedToTerrain() const { return m_fittedToTerrain; }
        const NavArea& BakedArea() const       { return m_bakedArea; }

    private:

        FlatNavTerrain        m_flatTerrain;
        TerrainGridNavTerrain m_gridTerrain;
        const NavTerrain*     m_terrain = &m_flatTerrain;

        bool    m_fittedToTerrain = false;
        NavArea m_bakedArea;

    };  // NAVBAKER CLASS

}   // NAMESPACE CRUCIBLE
