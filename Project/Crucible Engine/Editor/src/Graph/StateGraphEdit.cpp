/*************************************************************************
* \file:         StateGraphEdit.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The state graph editing operations.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "StateGraphEdit.h"

// STL
#include <algorithm>    // std::any_of, std::swap, std::sort, std::unique, std::lower_bound
#include <functional>   // std::greater

// Crucible engine
#include "AI/NodeRegistry.h"
#include "AI/TransitionCondition.h"   // Clone() on the copied arrows' conditions

namespace Crucible::StateGraphEdit {

    Content ContentOf(const GraphState& state) {

        if (state.m_subgraph != 0) return Content::Subgraph;
        if (state.m_behavior.Root() && state.m_singleNode) return Content::SingleNode;
        if (state.m_behavior.Root() || !state.m_behavior.Detached().empty()) return Content::Tree;
        return Content::Empty;

    }   // ContentOf()

    std::string UniqueName(const StateGraph& graph, const std::string& base) {

        const auto taken = [&](const std::string& name) {
            return std::any_of(graph.m_states.begin(), graph.m_states.end(), [&](const GraphState& state) { return state.m_name == name; });
        };
        std::string name = base;
        for (int i = 1; taken(name) && i < 10000; ++i) name = base + " " + std::to_string(i);
        return name;

    }   // UniqueName()

    int AddState(StateGraph& graph, const std::string& name, const Math::vec2& position) {

        GraphState state;
        state.m_name           = UniqueName(graph, name);
        state.m_editorPosition = position;
        graph.m_states.push_back(std::move(state));
        return static_cast<int>(graph.m_states.size()) - 1;

    }   // AddState()

    void DeleteState(StateGraph& graph, int index) {

        if (index < 0 || index >= static_cast<int>(graph.m_states.size())) return;

        // Its arrows go; arrows pointing at later states shift down one.
        std::erase_if(graph.m_transitions, [&](const StateTransition& t) { return t.m_from == index || t.m_to == index; });
        for (StateTransition& t : graph.m_transitions) {
            if (t.m_from > index) --t.m_from;
            if (t.m_to > index)   --t.m_to;
        }

        graph.m_states.erase(graph.m_states.begin() + index);

        // The entry follows its state's new number - or, if it WAS this state,
        // falls back to the first one (a graph with states must have an entry).
        if (graph.m_entryState == index)     graph.m_entryState = 0;
        else if (graph.m_entryState > index) --graph.m_entryState;

    }   // DeleteState()

    GraphState CloneState(const GraphState& state) {

        // GraphState can't be copied plainly (its tree is owned by unique_ptrs),
        // so: copy the plain fields, Clone the nodes.
        GraphState copy;
        copy.m_name           = state.m_name;
        copy.m_singleNode     = state.m_singleNode;
        copy.m_subgraph       = state.m_subgraph;
        copy.m_editorPosition = state.m_editorPosition;
        if (state.m_behavior.Root()) copy.m_behavior.SetRoot(state.m_behavior.Root()->Clone());
        for (const auto& detached : state.m_behavior.Detached()) copy.m_behavior.Detached().push_back(detached->Clone());
        return copy;

    }   // CloneState()

    int AddCopy(StateGraph& graph, const GraphState& state, const Math::vec2& position) {

        GraphState copy = CloneState(state);
        copy.m_name           = UniqueName(graph, state.m_name);
        copy.m_editorPosition = position;
        graph.m_states.push_back(std::move(copy));
        return static_cast<int>(graph.m_states.size()) - 1;

    }   // AddCopy()

    StateClipboard CopyStates(const StateGraph& graph, std::vector<int> indices) {

        // Ascending and once each: the copies keep the graph's order.
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        const int count = static_cast<int>(graph.m_states.size());
        std::erase_if(indices, [&](int i) { return i < 0 || i >= count; });

        // A state's place in the clipboard, found from its number in the graph.
        const auto clipIndexOf = [&](int graphIndex) {
            const auto it = std::lower_bound(indices.begin(), indices.end(), graphIndex);
            return (it != indices.end() && *it == graphIndex) ? static_cast<int>(it - indices.begin()) : -1;
        };

        StateClipboard clipboard;
        for (int i : indices) clipboard.states.push_back(CloneState(graph.m_states[static_cast<std::size_t>(i)]));

        // Arrows in priority order, so the pasted ones are checked in the same order.
        for (const StateTransition& t : graph.m_transitions) {
            const int from = clipIndexOf(t.m_from), to = clipIndexOf(t.m_to);
            if (from < 0 || to < 0) continue;
            StateTransition copy;
            copy.m_from = from;
            copy.m_to   = to;
            for (const auto& condition : t.m_conditions) if (condition) copy.m_conditions.push_back(condition->Clone());
            clipboard.arrows.push_back(std::move(copy));
        }
        return clipboard;

    }   // CopyStates()

    Math::vec2 TopLeftOf(const StateClipboard& clipboard) {

        if (clipboard.states.empty()) return Math::vec2(0.0f);
        Math::vec2 corner = clipboard.states.front().m_editorPosition;
        for (const GraphState& state : clipboard.states) corner = glm::min(corner, state.m_editorPosition);
        return corner;

    }   // TopLeftOf()

    std::vector<int> PasteStates(StateGraph& graph, const StateClipboard& clipboard, const Math::vec2& topLeft) {

        // Each state keeps its offset from the group's corner, so the layout survives.
        const Math::vec2 shift = topLeft - TopLeftOf(clipboard);
        std::vector<int> added;
        for (const GraphState& state : clipboard.states) added.push_back(AddCopy(graph, state, state.m_editorPosition + shift));

        // The arrows, renumbered from clipboard places to the new states.
        for (const StateTransition& t : clipboard.arrows) {
            const int index = AddTransition(graph, added[static_cast<std::size_t>(t.m_from)], added[static_cast<std::size_t>(t.m_to)]);
            if (index < 0) continue;
            for (const auto& condition : t.m_conditions) if (condition) graph.m_transitions[static_cast<std::size_t>(index)].m_conditions.push_back(condition->Clone());
        }
        return added;

    }   // PasteStates()

    void DeleteStates(StateGraph& graph, std::vector<int> indices) {

        std::sort(indices.begin(), indices.end(), std::greater<int>());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (int i : indices) DeleteState(graph, i);

    }   // DeleteStates()

    int AddTransition(StateGraph& graph, int from, int to) {

        const int count = static_cast<int>(graph.m_states.size());
        if (from == to || from < 0 || to < 0 || from >= count || to >= count) return -1;
        for (const StateTransition& t : graph.m_transitions) {
            if (t.m_from == from && t.m_to == to) return -1;   // one arrow per pair: its conditions say everything
        }

        StateTransition transition;
        transition.m_from = from;
        transition.m_to   = to;
        graph.m_transitions.push_back(std::move(transition));
        return static_cast<int>(graph.m_transitions.size()) - 1;

    }   // AddTransition()

    void DeleteTransition(StateGraph& graph, int index) {

        if (index < 0 || index >= static_cast<int>(graph.m_transitions.size())) return;
        graph.m_transitions.erase(graph.m_transitions.begin() + index);

    }   // DeleteTransition()

    int MovePriority(StateGraph& graph, int index, int direction) {

        const int count = static_cast<int>(graph.m_transitions.size());
        if (index < 0 || index >= count || direction == 0) return index;

        // The nearest arrow that way out of the same state - arrows from other
        // states in between don't matter (only same-state order is priority).
        const int from = graph.m_transitions[static_cast<std::size_t>(index)].m_from;
        for (int other = index + direction; other >= 0 && other < count; other += direction) {
            if (graph.m_transitions[static_cast<std::size_t>(other)].m_from != from) continue;
            std::swap(graph.m_transitions[static_cast<std::size_t>(index)], graph.m_transitions[static_cast<std::size_t>(other)]);
            return other;
        }
        return index;   // already first / last

    }   // MovePriority()

    void SetContent(GraphState& state, Content content) {

        const auto clearNodes = [&]() {
            state.m_behavior.ReleaseRoot();
            state.m_behavior.Detached().clear();
        };

        switch (content) {

            case Content::Empty:
                clearNodes();
                state.m_singleNode = false;
                state.m_subgraph   = 0;
                break;

            case Content::Tree:
                state.m_subgraph = 0;
                if (state.m_singleNode) clearNodes();   // a single node isn't the start of a tree
                state.m_singleNode = false;
                if (!state.m_behavior.Root()) {
                    if (const NodeTypeEntry* start = NodeRegistry::Instance().Find("start")) state.m_behavior.SetRoot(start->create());
                }
                break;

            case Content::SingleNode:
                state.m_subgraph = 0;
                if (!state.m_singleNode) clearNodes();
                state.m_singleNode = true;
                if (!state.m_behavior.Root()) {
                    if (const NodeTypeEntry* follow = NodeRegistry::Instance().Find("follow_field")) state.m_behavior.SetRoot(follow->create());
                }
                break;

            case Content::Subgraph:
                clearNodes();
                state.m_singleNode = false;
                break;

        }

    }   // SetContent()

}   // NAMESPACE CRUCIBLE::STATEGRAPHEDIT
