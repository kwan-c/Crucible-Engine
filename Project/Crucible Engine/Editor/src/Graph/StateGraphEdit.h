/*************************************************************************
* \file:         StateGraphEdit.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The editing operations on a state graph - states, arrows,
*                arrow priority, what a state contains - with the rules that
*                keep its numbering consistent.
*
* \details
*                States and arrows refer to each other by number (their place
*                in the lists), so deleting a state must remove its arrows
*                and renumber the rest, and keep the entry state valid. Plain
*                functions, no ImGui: the view and the panel's side panel
*                both call them.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <string>
#include <vector>

// Crucible engine
#include "AI/StateGraph.h"

namespace Crucible::StateGraphEdit {

    // What a state runs, as the editor offers it.
    enum class Content { Empty, Tree, SingleNode, Subgraph };

    Content ContentOf(const GraphState& state);

    // `base`, or "base 1", "base 2"... - the first name no state has.
    std::string UniqueName(const StateGraph& graph, const std::string& base);

    // A new empty state; returns its number.
    int AddState(StateGraph& graph, const std::string& name, const Math::vec2& position);

    // Removes a state and every arrow from or to it; the rest are renumbered,
    // and the entry state moves to the first one if it was this one.
    void DeleteState(StateGraph& graph, int index);

    // A deep copy (its tree included), with a fresh name. Arrows aren't copied.
    GraphState CloneState(const GraphState& state);
    int        AddCopy(StateGraph& graph, const GraphState& state, const Math::vec2& position);

    // Several states copied together, with the arrows that run BETWEEN them
    // (arrows to states left behind can't come along). The arrows' numbers
    // point into `states`, not into any graph.
    struct StateClipboard {

        std::vector<GraphState>      states;
        std::vector<StateTransition> arrows;

        bool Empty() const { return states.empty(); }

    };  // STATECLIPBOARD STRUCT

    StateClipboard CopyStates(const StateGraph& graph, std::vector<int> indices);

    // Adds copies of the clipboard's states, laid out as they were, with the
    // group's top-left corner at `topLeft`. Returns the new states' numbers.
    std::vector<int> PasteStates(StateGraph& graph, const StateClipboard& clipboard, const Math::vec2& topLeft);

    // The clipboard group's top-left corner (where PasteStates measures from).
    Math::vec2 TopLeftOf(const StateClipboard& clipboard);

    // DeleteState on each, highest number first, so the numbers still to go
    // aren't shifted by the ones already gone.
    void DeleteStates(StateGraph& graph, std::vector<int> indices);

    // A new arrow from -> to, last in priority; returns its number, or -1 if
    // from == to (skipped at runtime anyway) or that arrow already exists.
    int  AddTransition(StateGraph& graph, int from, int to);
    void DeleteTransition(StateGraph& graph, int index);

    // Swaps an arrow with the previous (direction -1) or next (+1) arrow out of
    // the SAME state - priority only matters among those. Returns its new number.
    int MovePriority(StateGraph& graph, int index, int direction);

    /*!
        \brief
        Changes what a state runs. Switching to Tree gives it an On Start root,
        to Single Node a Follow Field node, to Subgraph or Empty clears its own
        nodes (a Subgraph's file is picked separately: m_subgraph).
    */
    void SetContent(GraphState& state, Content content);

}   // NAMESPACE CRUCIBLE::STATEGRAPHEDIT
