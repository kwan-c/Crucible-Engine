/*************************************************************************
* \file:         AIComponents.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                StateGraphAgent: puts a state graph (.stategraph) in
*                charge of an enemy - its brain, next to FlowFieldAgent's
*                legs.
*
* \details
*                ECSStateGraphSystem runs each enemy's own copy of the
*                graph every frame while Playing (ECS/ECSStateGraphSystem.h).
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>

// Crucible engine
#include "ECS/Components/ComponentKind.h"

namespace Crucible {

    /*
        The enemy's brain. Pick a .stategraph in the Inspector; at Play the
        enemy gets its own running copy, which decides when to patrol, chase
        or search and gives the FlowFieldAgent its orders.

        Changing the graph during Play swaps it straight away (the old one is
        cut short). Per-enemy overrides of the graph's variables (patrol
        points, radii) come with the graph editor.
    */
    struct StateGraphAgent {

        std::uint64_t m_graph           = 0;       // the .stategraph's asset id; 0 = no brain (the enemy does nothing)
        bool          m_logStateChanges = false;   // write each state switch to the log - for checking a graph; costs ~3 ms a line in Debug, so not on a whole horde

    };  // STATEGRAPHAGENT STRUCT

    CRUCIBLE_COMPONENT(StateGraphAgent, StateGraphAgent)

}   // NAMESPACE CRUCIBLE
