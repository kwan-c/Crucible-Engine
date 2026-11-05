/*************************************************************************
* \file:         GraphAssets.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Starter graphs for Asset Browser > Create.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "GraphAssets.h"

// Crucible engine
#include "AI/BehaviorGraphIO.h"
#include "AI/StateGraphIO.h"

namespace Crucible::GraphAssets {

    std::string NewBehaviorGraphJson() {

        // An On Start root. Made by id, so it works even before C# has listed
        // the node types (C# fills in its defaults when it runs).
        BehaviorGraph graph;
        graph.SetRoot(MakeNode("start"));
        return EncodeBehaviorGraph(graph);

    }   // NewBehaviorGraphJson()

    std::string NewStateGraphJson() {

        // One state with nothing in it yet: an enemy given this graph simply
        // stands in Idle until states and arrows are added.
        StateGraph graph;
        GraphState idle;
        idle.m_name = "Idle";
        graph.m_states.push_back(std::move(idle));
        graph.m_entryState = 0;
        return EncodeStateGraph(graph);

    }   // NewStateGraphJson()

}   // NAMESPACE CRUCIBLE::GRAPHASSETS
