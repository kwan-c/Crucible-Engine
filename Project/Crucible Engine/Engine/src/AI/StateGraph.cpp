/*************************************************************************
* \file:         StateGraph.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       2.0
* \brief
*                Running a state graph: checking the arrows, switching
*                state, and ticking the current state - in C#.
*
* \history
*    -v1.0       first version (ran C++ nodes)
*    -v2.0       builds and ticks C# copies through NodeHostBridge
*
**************************************************************************/
#include "StateGraph.h"

// Crucible engine
#include "AI/NodeHostBridge.h"

namespace Crucible {

    std::unique_ptr<StateGraphInstance> StateGraphInstance::Create(std::shared_ptr<const StateGraph> graph, NodeHostBridge& host, Entity self,
                                                                   const SubgraphLookup& subgraphs) {

        // new, not make_unique: the constructor is private.
        std::unique_ptr<StateGraphInstance> instance(new StateGraphInstance());
        instance->m_host = &host;
        if (!graph) return instance;    // runs as an empty graph: does nothing

        instance->m_graph     = std::move(graph);
        instance->m_variables = instance->m_graph->m_variables;
        instance->m_variables.Set("Self", self);

        // Every state's tree built up front, so switching state never builds
        // mid-game - and a state left and re-entered starts fresh from the
        // same copy (an aborted C# tree starts over on its next Tick).
        host.Ready();   // first, so the generation below is the one the ids get
        instance->m_generation = host.Generation();

        // A .behaviorgraph used by a Subgraph state or a Run Subgraph node. Its
        // variables join the enemy's - but a name the state graph already has
        // stays the state graph's, so the subgraph reads and writes the same
        // Target as everything else. Kept alive in `loaded` only while building:
        // the C# copies don't need the data afterwards.
        std::vector<std::shared_ptr<const BehaviorGraph>> loaded;
        const auto useSubgraph = [&](std::uint64_t asset) -> const BehaviorGraph* {
            std::shared_ptr<const BehaviorGraph> subgraph = subgraphs ? subgraphs(asset) : nullptr;
            if (!subgraph) return nullptr;
            for (const BlackboardVariable& variable : subgraph->Variables().Variables()) {
                if (!instance->m_variables.Has(variable.name)) instance->m_variables.Set(variable.name, variable.value);
            }
            loaded.push_back(std::move(subgraph));
            return loaded.back().get();
        };

        for (const GraphState& state : instance->m_graph->m_states) {
            const BehaviorGraph* behavior = state.m_subgraph != 0 ? useSubgraph(state.m_subgraph) : &state.m_behavior;
            instance->m_roots.push_back(behavior && behavior->Root() ? host.BuildTree(*behavior->Root(), self, useSubgraph) : 0);
            instance->m_enters.push_back(state.m_onEnter ? host.BuildTree(*state.m_onEnter, self, useSubgraph) : 0);
            instance->m_exits.push_back(state.m_onExit ? host.BuildTree(*state.m_onExit, self, useSubgraph) : 0);
        }

        instance->m_conditions.reserve(instance->m_graph->m_transitions.size());
        for (const StateTransition& transition : instance->m_graph->m_transitions) {
            std::vector<int>& ids = instance->m_conditions.emplace_back();
            for (const auto& condition : transition.m_conditions) ids.push_back(condition ? host.BuildCondition(*condition, self) : 0);
        }
        return instance;

    }   // StateGraphInstance::Create()

    StateGraphInstance::~StateGraphInstance() {

        if (!m_host) return;
        for (const int root : m_roots) m_host->Release(root, m_generation);
        for (const int enter : m_enters) m_host->Release(enter, m_generation);
        for (const int exit : m_exits) m_host->Release(exit, m_generation);
        for (const std::vector<int>& ids : m_conditions) for (const int id : ids) m_host->Release(id, m_generation);

    }   // StateGraphInstance::~StateGraphInstance()

    void StateGraphInstance::Enter(int state) {

        // Leaving: the old state's running nodes end now (Move To stops its
        // agent), so nothing from it carries on into the new state - an On
        // Enter still going included. Then its On Exit, after them: it gets one
        // frame (with no time passing) and anything still running is cut short,
        // because a state can't be held open while the enemy leaves it.
        if (m_current >= 0) {
            const std::size_t old = static_cast<std::size_t>(m_current);
            m_host->Abort(m_roots[old]);
            m_host->Abort(m_enters[old]);
            if (const int exit = m_exits[old]) {
                m_host->Tick(exit, 0.0f);
                m_host->Abort(exit);
            }
        }

        m_current  = state;
        m_info     = StateInfo{};    // its clock and "finished" start over
        m_entering = m_enters[static_cast<std::size_t>(state)] != 0;

    }   // StateGraphInstance::Enter()

    void StateGraphInstance::Tick(float deltaTime) {

        if (!m_graph || m_graph->m_states.empty()) return;
        const int stateCount = static_cast<int>(m_graph->m_states.size());

        // The first frame: into the entry state (or the first one, if the
        // entry index is broken).
        if (m_current < 0) Enter(m_graph->m_entryState >= 0 && m_graph->m_entryState < stateCount ? m_graph->m_entryState : 0);

        // 1. The arrows: Any State's first (as in Unity), then the current
        // state's, each set in priority order; the first whose conditions ALL
        // pass wins. Checked before running, so a state entered this frame also
        // runs this frame.
        const auto passes = [&](std::size_t i) {
            for (const int condition : m_conditions[i]) {
                if (!m_host->IsTrue(condition, m_info.timeInState, m_info.finished, m_info.finishedWith)) return false;
            }
            return true;
        };
        bool switched = false;
        for (const int from : { kAnyState, m_current }) {
            for (std::size_t i = 0; i < m_graph->m_transitions.size() && !switched; ++i) {
                const StateTransition& transition = m_graph->m_transitions[i];
                if (transition.m_from != from || transition.m_to == m_current) continue;
                if (transition.m_to < 0 || transition.m_to >= stateCount) continue;     // a broken arrow goes nowhere
                if (passes(i)) { Enter(transition.m_to); switched = true; }
            }
            if (switched) break;
        }

        // 2. Run the current state. When its tree or node finishes it simply
        // starts again next frame; "finished" is remembered for the arrows
        // (State Finished) until the enemy leaves the state.
        m_info.timeInState += deltaTime;

        // The state's On Enter first: its tree waits until it has finished.
        if (m_entering) {
            if (m_host->Tick(m_enters[static_cast<std::size_t>(m_current)], deltaTime) == BehaviorStatus::Running) return;
            m_entering = false;
        }

        if (const int root = m_roots[static_cast<std::size_t>(m_current)]) {
            const BehaviorStatus status = m_host->Tick(root, deltaTime);
            if (status != BehaviorStatus::Running) {
                m_info.finished     = true;
                m_info.finishedWith = status;
            }
        }

    }   // StateGraphInstance::Tick()

    void StateGraphInstance::Abort() {

        if (m_current < 0 || !m_host) return;
        m_host->Abort(m_roots[static_cast<std::size_t>(m_current)]);
        m_host->Abort(m_enters[static_cast<std::size_t>(m_current)]);

    }   // StateGraphInstance::Abort()

    std::string_view StateGraphInstance::CurrentStateName() const {

        if (!m_graph || m_current < 0) return {};
        return m_graph->m_states[static_cast<std::size_t>(m_current)].m_name;

    }   // StateGraphInstance::CurrentStateName()

}   // NAMESPACE CRUCIBLE
