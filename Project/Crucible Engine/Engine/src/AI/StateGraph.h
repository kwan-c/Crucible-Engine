/*************************************************************************
* \file:         StateGraph.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                StateGraph: the state machine at the top of an enemy's
*                behaviour (Idle, Patrol, Chase, Search), as an asset.
*                StateGraphInstance: one enemy's running copy.
*
* \details
*                Each state holds a behaviour tree, or just a single node
*                (Follow Field) when a whole tree would be overkill, or
*                points at a separate .behaviorgraph file (a Subgraph) that
*                several states or enemies can share. Arrows
*                (StateTransition) lead from one state to another, each
*                with conditions; every frame the first arrow out of the
*                current state whose conditions all pass switches state.
*
*                All the states share the state graph's one set of
*                variables, so what Patrol learns (Target) Chase still
*                knows.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Crucible engine
#include "AI/BehaviorGraph.h"
#include "AI/TransitionCondition.h"

namespace Crucible {

    struct GraphState {

        std::string   m_name;
        BehaviorGraph m_behavior;              // the tree - or the single node, as its root. Its own variables go unused: the state graph's are shared
        bool          m_singleNode = false;    // editor: drawn as one node rather than a tree. Runs the same either way
        std::uint64_t m_subgraph   = 0;        // a .behaviorgraph's asset id: run that instead of m_behavior (0 = use m_behavior)
        Math::vec2    m_editorPosition{ 0.0f };

    };  // GRAPHSTATE STRUCT

    // Finds a Subgraph state's .behaviorgraph by asset id (ResourceManager::GetBehaviorGraph
    // in the game). A function, not a ResourceManager&, so this file stays free of the
    // resource system - and a test can hand it graphs directly.
    using SubgraphLookup = std::function<std::shared_ptr<const BehaviorGraph>(std::uint64_t)>;

    struct StateTransition {

        int                                               m_from = 0;   // state indices
        int                                               m_to   = 0;
        std::vector<std::unique_ptr<TransitionCondition>> m_conditions; // ALL must pass. None: switches at once

    };  // STATETRANSITION STRUCT

    class StateGraphInstance;

    // Plain data: the editor and StateGraphIO fill it in, StateGraphInstance runs it.
    struct StateGraph {

        std::vector<GraphState>      m_states;
        std::vector<StateTransition> m_transitions;   // in priority order: earlier arrows are checked first
        int                          m_entryState = 0;
        Blackboard                   m_variables;

    };  // STATEGRAPH STRUCT

    class StateGraphInstance {

    public:

        /*!
            \brief
            One enemy's running copy: its own copy of every state's nodes and
            of the variables (plus "Self"). It keeps `graph` alive, so the
            asset can't vanish while the enemy uses it.

            \param subgraphs
            How Subgraph states find their .behaviorgraph. Each one's
            variables are added to the enemy's (unless the state graph already
            has one by that name), and the graph is kept alive like `graph`.
            Empty: Subgraph states run nothing.
        */
        static std::unique_ptr<StateGraphInstance> Create(std::shared_ptr<const StateGraph> graph, World& world, Entity self,
                                                          const SubgraphLookup& subgraphs = {});

        // Nodes keep a pointer to m_context: never moved or copied.
        StateGraphInstance(const StateGraphInstance&)            = delete;
        StateGraphInstance& operator=(const StateGraphInstance&) = delete;

        /*!
            \brief
            One frame: switch state if an arrow out of the current one passes,
            then run the current state's tree or node.

            \details
            An arrow from a state back to itself is skipped: its conditions
            would usually stay true, and it would restart the state every frame.
        */
        void Tick(float deltaTime);

        // Cuts the current state short (the enemy was removed, or Play stopped).
        void Abort();

        int              CurrentState() const { return m_current; }
        std::string_view CurrentStateName() const;   // "" before the first Tick
        Blackboard&      Variables() { return m_variables; }

    private:

        StateGraphInstance() = default;

        // Leaves the current state (aborting its tree) and enters `state`.
        void Enter(int state);

        std::shared_ptr<const StateGraph>                 m_graph;
        std::vector<std::shared_ptr<const BehaviorGraph>> m_subgraphs;   // kept alive while their nodes run
        Blackboard                                        m_variables;
        BehaviorContext                            m_context;
        std::vector<std::unique_ptr<BehaviorNode>> m_roots;     // this enemy's copy of each state's root (null for an empty state)
        int                                        m_current = -1;
        StateInfo                                  m_info;

    };  // STATEGRAPHINSTANCE CLASS

}   // NAMESPACE CRUCIBLE
