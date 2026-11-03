/*************************************************************************
* \file:         BehaviorGraph.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                BehaviorGraph: a behaviour tree as an asset - its nodes and
*                its blackboard variables. BehaviorGraphInstance: one
*                enemy's private running copy of it.
*
* \details
*                The asset is shared: editing it changes every enemy that
*                uses it. At Play each enemy calls Instantiate() and gets
*                its own copy of the nodes and the blackboard, so each
*                tracks its own progress (Unity Behavior does the same).
*                The copy is never saved and goes away at Stop.
*
* \history
*    -v1.0       first version
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

    class BehaviorGraphInstance;

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

        /*!
            \brief
            One enemy's running copy: cloned nodes, a copy of the variables
            (plus "Self" = the enemy), all pointed at that enemy.

            \param world
            The world the enemy lives in. Must outlive the instance.

            \param self
            The enemy running it.
        */
        std::unique_ptr<BehaviorGraphInstance> Instantiate(World& world, Entity self) const;

    private:

        std::unique_ptr<BehaviorNode>              m_root;
        std::vector<std::unique_ptr<BehaviorNode>> m_detached;
        Blackboard                                 m_variables;

    };  // BEHAVIORGRAPH CLASS

    class BehaviorGraphInstance {

    public:

        // Its nodes keep a pointer to m_context, so it must never move or copy:
        // it only ever lives behind the unique_ptr Instantiate returns.
        BehaviorGraphInstance(const BehaviorGraphInstance&)            = delete;
        BehaviorGraphInstance& operator=(const BehaviorGraphInstance&) = delete;

        // One frame of the tree. Failure if it has no root.
        BehaviorStatus Tick(float deltaTime);

        // Cuts the whole tree short (the enemy left this state, or was removed).
        void Abort();

        // This enemy's private blackboard (per-enemy overrides are written here).
        Blackboard& Variables() { return m_variables; }

        const BehaviorNode* Root() const { return m_root.get(); }

    private:

        friend class BehaviorGraph;
        BehaviorGraphInstance() = default;

        Blackboard                    m_variables;
        BehaviorContext               m_context;
        std::unique_ptr<BehaviorNode> m_root;

    };  // BEHAVIORGRAPHINSTANCE CLASS

}   // NAMESPACE CRUCIBLE
