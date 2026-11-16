/*************************************************************************
* \file:         FlowField.h
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief
*                FlowField: one shared map of "which way to the player" for
*                every cell of a NavGrid. The pathing layer of enemy
*                navigation.
*
* \details
*                Ported from Jean David's Unity prototype (FlowField.cs; see
*                his "Enemy navigation: porting reference"). The path is
*                worked out once for the whole horde, not per enemy: a flood
*                spreads out from the goal and leaves, in every cell it
*                reaches, the direction of the next step. An enemy finds its
*                way with a single lookup - which cell am I in, which way
*                does it point.
*
*                Three arrays, one value per cell:
*                  cost        - how expensive a cell is to ENTER (built
*                                once per bake)
*                  integration - cheapest total cost from the cell to the
*                                goal (Dijkstra distance)
*                  direction   - which of the 8 neighbours to step to next
*
*                Only reads the NavGrid, never writes it, and knows nothing
*                about enemies or the ECS.
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
#include "Navigation/NavGrid.h"

namespace Crucible {

    class FlowField {

    public:

        /*!
            \brief
            Builds the cost field from a freshly baked grid, and forgets any old
            flow. Call after every bake, before Rebuild.

            \param grid
            The grid to flow over. Kept by pointer, so it must stay alive and in
            place (it is a sibling member in ECSNavigationSystem).

            \param wallAdjacencyCost
            Extra cost for an open cell touching a blocked one, so routes keep
            some clearance instead of hugging walls ("mud near walls").
        */
        void BuildCostField(const NavGrid& grid, float wallAdjacencyCost);

        /*!
            \brief
            Floods out from the goals and points every reached cell one step
            towards the nearest of them. Reuses the cost field.

            \param goals
            World positions to flow towards - normally the players. Several
            goals cost one flood, not one each, and each cell then points at its
            nearest goal. A goal off the grid is pulled onto its edge; a goal
            inside a blocked cell is moved to the nearest open cell.

            \param floodRadius
            Metres around the goals that get directions; 0 floods the whole
            grid. Outside it, SampleDirection returns zero.

            \return
            False if nothing could be flooded (no cost field, no goals, or no
            open cell near them). The old flow is gone either way.
        */
        bool Rebuild(const std::vector<Math::vec3>& goals, float floodRadius);

        // Forget the flow but keep the cost field (Stop, or a new bake coming).
        void ClearFlow();

        // Forget everything, the grid included (the grid was cleared).
        void Clear();

        bool HasFlow() const { return m_hasFlow; }

        /*!
            \brief
            The way to go from a world position: a flat unit vector (y = 0)
            along one of 8 directions, so diagonals come out as (0.707, 0, 0.707).

            \return
            Zero when there is no flow, the position is off the grid, or its
            cell has no direction (blocked, not reached, outside the flood
            radius, or the goal's own cell). The caller then heads straight at
            its target instead.
        */
        Math::vec3 SampleDirection(const Math::vec3& position) const;

        // The same, for one cell (the debug arrows use it).
        Math::vec3 DirectionAt(const NavCell& cell) const;

    private:

        // One entry of the flood's binary min-heap.
        struct HeapEntry {

            float distance = 0.0f;
            int   index    = 0;

        };  // HEAPENTRY STRUCT

        bool IsOpen(int col, int row) const;
        void ResetArea();

        const NavGrid*            m_grid = nullptr;

        std::vector<float>        m_cost;          // cost to enter; infinity = blocked
        std::vector<float>        m_integration;   // cheapest cost to a goal; infinity = not reached
        std::vector<std::int8_t>  m_dirX;          // -1, 0 or 1 along X (columns); 0 and 0 = no direction
        std::vector<std::int8_t>  m_dirZ;          // -1, 0 or 1 along Z (rows)
        std::vector<HeapEntry>    m_heap;          // kept between rebuilds so its memory is reused

        // The last flood's square of cells. Only this is reset before the next
        // flood, so a rebuild near the player never touches the whole grid.
        NavCell                   m_areaMin;
        NavCell                   m_areaMax{ -1, -1 };   // max < min: no area yet

        bool                      m_hasFlow = false;

    };  // FLOWFIELD CLASS

}   // NAMESPACE CRUCIBLE
