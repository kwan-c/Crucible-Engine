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
#include "AI/NodeRegistry.h"
#include "AI/StateGraphIO.h"

namespace Crucible::GraphAssets {

    std::string NewBehaviorGraphJson() {

        // The root comes from the registry: the node classes live in
        // Engine.dll, and only its "make one" functions can build them.
        BehaviorGraph graph;
        if (const NodeTypeEntry* start = NodeRegistry::Instance().Find("start")) graph.SetRoot(start->create());
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
