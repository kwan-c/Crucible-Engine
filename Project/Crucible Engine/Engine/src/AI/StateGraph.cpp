/*************************************************************************
* \file:         StateGraph.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Running a state graph: checking the arrows, switching
*                state, and ticking the current state.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "StateGraph.h"

// STL
#include <algorithm>    // std::all_of

namespace Crucible {

    std::unique_ptr<StateGraphInstance> StateGraphInstance::Create(std::shared_ptr<const StateGraph> graph, World& world, Entity self,
                                                                   const SubgraphLookup& subgraphs) {

        // new, not make_unique: the constructor is private.
        std::unique_ptr<StateGraphInstance> instance(new StateGraphInstance());
        if (!graph) return instance;    // runs as an empty graph: does nothing

        instance->m_graph     = std::move(graph);
        instance->m_variables = instance->m_graph->m_variables;
        instance->m_variables.Set("Self", self);
        instance->m_context   = BehaviorContext{ &world, self, &instance->m_variables };

        // Every state's nodes cloned up front, so switching state never
        // allocates mid-game - and a state left and re-entered starts fresh
        // from the same copy.
        instance->m_roots.reserve(instance->m_graph->m_states.size());
        for (const GraphState& state : instance->m_graph->m_states) {

            // A Subgraph state runs the shared .behaviorgraph instead of its own
            // tree. Its variables join the enemy's - but a name the state graph
            // already has stays the state graph's, so the subgraph reads and
            // writes the same Target as everything else.
            const BehaviorGraph* behavior = &state.m_behavior;
            if (state.m_subgraph != 0) {
                behavior = nullptr;
                if (std::shared_ptr<const BehaviorGraph> subgraph = subgraphs ? subgraphs(state.m_subgraph) : nullptr) {
                    for (const BlackboardVariable& variable : subgraph->Variables().Variables()) {
                        if (!instance->m_variables.Has(variable.name)) instance->m_variables.Set(variable.name, variable.value);
                    }
                    behavior = subgraph.get();
                    instance->m_subgraphs.push_back(std::move(subgraph));
                }
            }

            std::unique_ptr<BehaviorNode> root = behavior && behavior->Root() ? behavior->Root()->Clone() : nullptr;
            if (root) root->Bind(&instance->m_context);
            instance->m_roots.push_back(std::move(root));

        }
        return instance;

    }   // StateGraphInstance::Create()

    void StateGraphInstance::Enter(int state) {

        // Leaving: the old state's running nodes end now (Move To stops its
        // agent), so nothing from it carries on into the new state.
        if (m_current >= 0 && m_roots[static_cast<std::size_t>(m_current)]) m_roots[static_cast<std::size_t>(m_current)]->Abort();

        m_current = state;
        m_info    = StateInfo{};    // its clock and "finished" start over

    }   // StateGraphInstance::Enter()

    void StateGraphInstance::Tick(float deltaTime) {

        if (!m_graph || m_graph->m_states.empty()) return;
        const int stateCount = static_cast<int>(m_graph->m_states.size());

        // The first frame: into the entry state (or the first one, if the
        // entry index is broken).
        if (m_current < 0) Enter(m_graph->m_entryState >= 0 && m_graph->m_entryState < stateCount ? m_graph->m_entryState : 0);

        // 1. The arrows out of the current state, in priority order: the first
        // whose conditions ALL pass wins. Checked before running, so a state
        // entered this frame also runs this frame.
        for (const StateTransition& transition : m_graph->m_transitions) {

            if (transition.m_from != m_current || transition.m_to == m_current) continue;
            if (transition.m_to < 0 || transition.m_to >= stateCount) continue;     // a broken arrow goes nowhere

            const bool passes = std::all_of(transition.m_conditions.begin(), transition.m_conditions.end(),
                [&](const std::unique_ptr<TransitionCondition>& condition) { return condition && condition->Check(m_context, m_info); });
            if (passes) {
                Enter(transition.m_to);
                break;
            }

        }

        // 2. Run the current state. When its tree or node finishes it simply
        // starts again next frame; "finished" is remembered for the arrows
        // (State Finished) until the enemy leaves the state.
        m_info.timeInState += deltaTime;
        if (BehaviorNode* root = m_roots[static_cast<std::size_t>(m_current)].get()) {
            const BehaviorStatus status = root->Tick(deltaTime);
            if (status != BehaviorStatus::Running) {
                m_info.finished     = true;
                m_info.finishedWith = status;
            }
        }

    }   // StateGraphInstance::Tick()

    void StateGraphInstance::Abort() {

        if (m_current >= 0 && m_roots[static_cast<std::size_t>(m_current)]) m_roots[static_cast<std::size_t>(m_current)]->Abort();

    }   // StateGraphInstance::Abort()

    std::string_view StateGraphInstance::CurrentStateName() const {

        if (!m_graph || m_current < 0) return {};
        return m_graph->m_states[static_cast<std::size_t>(m_current)].m_name;

    }   // StateGraphInstance::CurrentStateName()

}   // NAMESPACE CRUCIBLE
