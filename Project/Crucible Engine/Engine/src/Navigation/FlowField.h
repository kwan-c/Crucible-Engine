/*************************************************************************
* \file:         FlowField.h
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          1 November 2026
* \version       1.2
* \brief
*                FlowField: one shared map of "which way to the player" for
*                every layer (node) of a NavGrid. The pathing layer of enemy
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
*                Three arrays, one value per node (a layer of a cell - most
*                cells have one, the ground; a bridge's cells have two):
*                  cost        - how expensive a node is to ENTER (built
*                                once per bake)
*                  integration - cheapest total cost from the node to the
*                                goal (Dijkstra distance)
*                  direction   - which of the 8 directions to step to next
*
*                The flood moves along the grid's LINKS (build step 6d):
*                from a node it reaches every neighbour node that links to
*                it - so a goal on the ground is reached from a platform an
*                enemy can drop off, but a goal on a platform is not reached
*                from the ground below its edge, which has no link up. On a
*                grid with only the ground every cell links to all its
*                neighbours and this is the flood it always was.
*
*                Only reads the NavGrid, never writes it, and knows nothing
*                about enemies or the ECS.
*
*                When dynamic obstacles change some cells (build step 4),
*                UpdateCosts fixes only those cells and their neighbours, and
*                TouchesFlow says whether the arrows could have changed.
*
* \history
*    -v1.0       first version
*    -v1.1       UpdateCosts and TouchesFlow (local updates for moving
*                obstacles); the cost rule moved into CostAt, shared with
*                BuildCostField, which gives the same costs as before
*    -v1.2       build step 6d-1: per node and along the grid's links.
*                Goals and SampleDirection pick the layer under the position
*                (NavGrid::NodeAt); DirectionAtNode for the debug arrows.
*                With only the ground, the same arrows as before
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

        /*!
            \brief
            Fixes the cost field after some nodes changed between open and
            blocked (NavGrid::AddDynamic / RemoveDynamic report them): each
            changed node and the nodes of its 4 straight neighbour cells,
            whose "next to a wall" extra may have changed too. Leaves the
            flow alone - see TouchesFlow.

            \param changed
            Node indices. Duplicates are fine.
        */
        void UpdateCosts(const std::vector<int>& changed);

        /*!
            \brief
            Could these changed nodes alter the current arrows? True if any
            of their cells, or a straight neighbour (whose cost changed with
            it), lies in the area the last flood covered. False with no flow.
            Outside that area nothing was reached, so a rebuild can wait for
            the player to come near.
        */
        bool TouchesFlow(const std::vector<int>& changed) const;

        // Forget the flow but keep the cost field (Stop, or a new bake coming).
        void ClearFlow();

        // Forget everything, the grid included (the grid was cleared).
        void Clear();

        bool HasFlow() const { return m_hasFlow; }

        /*!
            \brief
            The way to go from a world position: a flat unit vector (y = 0)
            along one of 8 directions, so diagonals come out as (0.707, 0, 0.707).
            The position's height picks the layer (NavGrid::NodeAt): on the
            bridge, the deck's arrow; under it, the ground's.

            \return
            Zero when there is no flow, the position is off the grid, or its
            node has no direction (blocked, not reached, outside the flood
            radius, or the goal's own node). The caller then heads straight at
            its target instead.
        */
        Math::vec3 SampleDirection(const Math::vec3& position) const;

        // The same, for one node (the debug arrows use it).
        Math::vec3 DirectionAtNode(int node) const;

    private:

        // One entry of the flood's binary min-heap.
        struct HeapEntry {

            float distance = 0.0f;
            int   index    = 0;

        };  // HEAPENTRY STRUCT

        bool  IsOpen(int node) const;            // -1 (no node) is not open
        bool  CornerClear(int node, int dir) const;   // a diagonal step: both straight steps beside it open
        void  ResetArea();
        float CostAt(int node) const;            // the cost rule: infinity / 1 + wallAdjacencyCost / 1

        float                     m_wallAdjacencyCost = 0.0f;   // from the bake, for UpdateCosts

        const NavGrid*            m_grid = nullptr;

        std::vector<float>        m_cost;          // per node: cost to enter; infinity = blocked
        std::vector<float>        m_integration;   // per node: cheapest cost to a goal; infinity = not reached
        std::vector<std::int8_t>  m_direction;     // per node: the direction (kNavDirCol/Row index) to step, or -1
        std::vector<HeapEntry>    m_heap;          // kept between rebuilds so its memory is reused

        // The last flood's square of cells. Only its nodes are reset before the
        // next flood, so a rebuild near the player never touches the whole grid.
        NavCell                   m_areaMin;
        NavCell                   m_areaMax{ -1, -1 };   // max < min: no area yet

        bool                      m_hasFlow = false;

    };  // FLOWFIELD CLASS

}   // NAMESPACE CRUCIBLE
