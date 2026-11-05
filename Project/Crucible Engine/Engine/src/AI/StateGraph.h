/*************************************************************************
* \file:         StateGraph.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       2.0
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
*                The trees and conditions are data here; each enemy's
*                instance builds C# copies of them (NodeHostBridge) and C#
*                runs them. The instance only decides WHICH state runs.
*
* \history
*    -v1.0       first version (ran C++ nodes)
*    -v2.0       trees and conditions run in C#
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
#include "ECS/Core/ECSCore.h"   // Entity

namespace Crucible {

    class NodeHostBridge;

    struct GraphState {

        std::string   m_name;
        BehaviorGraph m_behavior;              // the tree - or the single node, as its root. Its own variables go unused: the state graph's are shared
        bool          m_singleNode = false;    // editor: drawn as one node rather than a tree. Runs the same either way
        std::uint64_t m_subgraph   = 0;        // a .behaviorgraph's asset id: run that instead of m_behavior (0 = use m_behavior)
        Math::vec2    m_editorPosition{ 0.0f };

        // Unity Visual Scripting's On Enter State / On Exit State, as one node each
        // (null = none). On Enter runs first and the tree waits for it to finish;
        // On Exit gets one frame as the enemy leaves, then is cut short - so give
        // it instant nodes (Log, Stop, a variable set), not ones that take time.
        std::unique_ptr<BehaviorNode> m_onEnter;
        std::unique_ptr<BehaviorNode> m_onExit;

    };  // GRAPHSTATE STRUCT

    // Finds a Subgraph state's .behaviorgraph by asset id (ResourceManager::GetBehaviorGraph
    // in the game). A function, not a ResourceManager&, so this file stays free of the
    // resource system - and a test can hand it graphs directly.
    using SubgraphLookup = std::function<std::shared_ptr<const BehaviorGraph>(std::uint64_t)>;

    // An arrow's m_from for Unity's "Any State": checked whatever state the enemy
    // is in, before that state's own arrows (health 0 -> Dead, from anywhere).
    inline constexpr int kAnyState = -1;

    struct StateTransition {

        int                                        m_from = 0;   // state indices; m_from may be kAnyState
        int                                        m_to   = 0;
        std::vector<std::unique_ptr<BehaviorNode>> m_conditions; // condition-kind nodes, ALL must pass. None: switches at once

    };  // STATETRANSITION STRUCT

    // Plain data: the editor and StateGraphIO fill it in, StateGraphInstance runs it.
    struct StateGraph {

        std::vector<GraphState>      m_states;
        std::vector<StateTransition> m_transitions;   // in priority order: earlier arrows are checked first
        int                          m_entryState = 0;
        Blackboard                   m_variables;
        Math::vec2                   m_anyStatePosition{ -260.0f, 0.0f };   // where the editor draws the Any State box

    };  // STATEGRAPH STRUCT

    // The state the enemy is in, as the arrows out of it see it.
    struct StateInfo {

        float          timeInState  = 0.0f;                    // seconds since it entered
        bool           finished     = false;                   // its tree or node has finished at least once since then
        BehaviorStatus finishedWith = BehaviorStatus::Success; // how it last finished (if it has)

    };  // STATEINFO STRUCT

    class StateGraphInstance {

    public:

        /*!
            \brief
            One enemy's running copy: a C# copy of every state's tree and
            every arrow's conditions, and its own copy of the variables
            (plus "Self"). It keeps `graph` alive, so the asset can't vanish
            while the enemy uses it.

            \param host
            Builds and runs the C# copies. Must outlive the instance.

            \param subgraphs
            How Subgraph states find their .behaviorgraph. Each one's
            variables are added to the enemy's (unless the state graph already
            has one by that name). Empty: Subgraph states run nothing.
        */
        static std::unique_ptr<StateGraphInstance> Create(std::shared_ptr<const StateGraph> graph, NodeHostBridge& host, Entity self,
                                                          const SubgraphLookup& subgraphs = {});

        // Frees its C# copies.
        ~StateGraphInstance();

        StateGraphInstance(const StateGraphInstance&)            = delete;
        StateGraphInstance& operator=(const StateGraphInstance&) = delete;

        /*!
            \brief
            One frame: switch state if an Any State arrow, or an arrow out of
            the current state, passes; then run the current state's tree or node.

            \details
            Any State arrows are checked first, as in Unity. An arrow into the
            state the enemy is already in is skipped (a state's own arrow back
            to itself, or Any State -> the current state): its conditions would
            usually stay true, and it would restart the state every frame.
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

        std::shared_ptr<const StateGraph> m_graph;
        NodeHostBridge*                   m_host       = nullptr;
        std::uint64_t                     m_generation = 0;   // the script reload its C# ids belong to
        Blackboard                        m_variables;
        std::vector<int>                  m_roots;            // C# tree id per state (0: runs nothing)
        std::vector<int>                  m_enters;           // C# On Enter id per state (0: none)
        std::vector<int>                  m_exits;            // C# On Exit id per state (0: none)
        std::vector<std::vector<int>>     m_conditions;       // C# condition ids per arrow (0: couldn't be built - never passes)
        int                               m_current  = -1;
        bool                              m_entering = false; // the current state's On Enter hasn't finished: its tree waits
        StateInfo                         m_info;

    };  // STATEGRAPHINSTANCE CLASS

}   // NAMESPACE CRUCIBLE
