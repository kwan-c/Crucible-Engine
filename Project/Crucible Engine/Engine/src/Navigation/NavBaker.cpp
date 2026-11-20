/*************************************************************************
* \file:         NavBaker.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          17 November 2026
* \version       1.2
* \brief         See NavBaker.h.
*
**************************************************************************/
#include "NavBaker.h"

// STL
#include <initializer_list>

// Crucible engine
#include "Diagnostics/Logger.h"

namespace Crucible {

    bool NavBaker::Bake(const NavBakeInput& input, const std::vector<NavObstacle>& obstacles, NavGrid& grid, FlowField& field) {

        NavArea area = input.surfaceArea;

        // With a terrain, the grid covers exactly the terrain: its own corners,
        // in world space, whatever the map size - so the surface's scale can't
        // be wrong, in Edit mode or Play. The surface still gives the settings.
        m_fittedToTerrain = input.terrainGrid != nullptr;
        if (m_fittedToTerrain) {
            const HeightGrid& heights = input.terrainGrid->heights;
            const float x0 = heights.MinX(), x1 = heights.MinX() + heights.Width();
            const float z0 = heights.MinZ(), z1 = heights.MinZ() + heights.Depth();
            Math::vec3 low(1e30f), high(-1e30f);
            for (const Math::vec3& corner : { Math::vec3(x0, 0.0f, z0), Math::vec3(x1, 0.0f, z0),
                                              Math::vec3(x0, 0.0f, z1), Math::vec3(x1, 0.0f, z1) }) {
                const Math::vec3 point(input.terrainMatrix * Math::vec4(corner, 1.0f));
                low  = glm::min(low, point);
                high = glm::max(high, point);
            }
            area.centre = Math::vec3((low.x + high.x) * 0.5f, input.terrainMatrix[3].y, (low.z + high.z) * 0.5f);
            area.width  = high.x - low.x;
            area.depth  = high.z - low.z;
        }
        m_bakedArea = area;

        // A freshly added surface sits on an entity of scale 1: a 1 x 1 m grid.
        // It still bakes (one cell is a valid grid), but say what to do about it.
        if (!m_fittedToTerrain && (area.width < input.cellSize * 2.0f || area.depth < input.cellSize * 2.0f)) {
            CRU_WARN("Navigation: the surface only covers {} x {} m - scale its entity so X and Z cover the level (scale = metres)",
                     area.width, area.depth);
        }

        // The ground: the generated terrain's own grid, the hand-off Unity's
        // generator made through NavTerrain.Register (F2). Its heights are the
        // exact triangles of the mesh and collider, and a nav cell is a wall
        // when the generator cell under its centre is a Cliff - one definition
        // of a cliff for the generator, its colours, the player and enemies.
        // No terrain: the flat plane at the surface's own height, so a designer
        // sits the surface on the floor, the way Unity's NavMeshSurface sits in
        // the level.
        m_flatTerrain = FlatNavTerrain(area.centre.y);
        m_terrain     = &m_flatTerrain;
        if (m_gridTerrain.Reset(input.terrainGrid, input.terrainMatrix, area.centre.y)) m_terrain = &m_gridTerrain;
        else                                                                         m_gridTerrain.Clear();
        CRU_INFO("Navigation: ground is {}; {:.0f} x {:.0f} m; {} tagged obstacle(s) baked",
                 m_terrain == &m_gridTerrain ? "the generated terrain (grid fitted to it)" : "flat",
                 area.width, area.depth, obstacles.size());

        NavGridSettings settings;
        settings.origin            = area.centre;
        settings.width             = area.width;
        settings.depth             = area.depth;
        settings.cellSize          = input.cellSize;
        settings.bodyRadiusPadding = input.bodyRadiusPadding;
        settings.stepHeight        = input.stepHeight;
        settings.agentHeight       = input.agentHeight;
        settings.maxLayersPerCell  = input.maxLayersPerCell;

        // The bake itself. NavGrid logs the result, or why it failed.
        if (!grid.Bake(settings, obstacles, *m_terrain, input.walkables)) {
            field.Clear();
            return false;
        }
        field.BuildCostField(grid, input.wallAdjacencyCost);
        return true;

    }   // NavBaker::Bake()

    void NavBaker::Clear() {

        m_gridTerrain.Clear();
        m_flatTerrain     = FlatNavTerrain(0.0f);
        m_terrain         = &m_flatTerrain;
        m_fittedToTerrain = false;
        m_bakedArea       = NavArea{};

    }   // NavBaker::Clear()

}   // NAMESPACE CRUCIBLE
