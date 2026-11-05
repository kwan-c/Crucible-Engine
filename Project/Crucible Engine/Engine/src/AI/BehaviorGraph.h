/*************************************************************************
* \file:         BehaviorGraph.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       2.0
* \brief
*                BehaviorGraph: a behaviour tree asset (.behaviorgraph) - its
*                root, the editor's unconnected nodes, and its variables.
*
* \details
*                Plain data, like BehaviorNode. It never runs by itself: a
*                state graph's state runs it (its own tree, or a Subgraph
*                state's .behaviorgraph), through NodeHostBridge, in C#.
*
* \history
*    -v1.0       first version (with BehaviorGraphInstance, which ran C++ nodes)
*    -v2.0       data only; trees run in C#
*
**************************************************************************/
#pragma once

// STL
#include <memory>
#include <vector>

// Crucible engine
#include "AI/BehaviorNode.h"
#include "AI/Blackboard.h"

namespace Crucible {

    class BehaviorGraph {

    public:

        // The tree's root: normally an On Start node. The graph owns it.
        void                          SetRoot(std::unique_ptr<BehaviorNode> root) { m_root = std::move(root); }
        const BehaviorNode*           Root() const { return m_root.get(); }
        BehaviorNode*                 Root()       { return m_root.get(); }
        std::unique_ptr<BehaviorNode> ReleaseRoot() { return std::move(m_root); }   // the editor taking it out

        // Nodes on the canvas that aren't connected to the tree (yet). Editor-only:
        // kept and saved so half-built work isn't lost, never run.
        std::vector<std::unique_ptr<BehaviorNode>>&       Detached()       { return m_detached; }
        const std::vector<std::unique_ptr<BehaviorNode>>& Detached() const { return m_detached; }

        // The variables and their defaults: each enemy's blackboard starts as a copy.
        Blackboard&       Variables()       { return m_variables; }
        const Blackboard& Variables() const { return m_variables; }

    private:

        std::unique_ptr<BehaviorNode>              m_root;
        std::vector<std::unique_ptr<BehaviorNode>> m_detached;
        Blackboard                                 m_variables;

    };  // BEHAVIORGRAPH CLASS

}   // NAMESPACE CRUCIBLE
