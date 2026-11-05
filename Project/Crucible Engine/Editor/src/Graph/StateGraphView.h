/*************************************************************************
* \file:         StateGraphView.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                StateGraphView: draws a state graph on a GraphCanvas - states
*                as boxes, arrows between them - and turns mouse work into
*                edits: select, move, draw arrows, the right-click menus.
*
* \details
*                Drag from a state's right-hand dot onto another state to add
*                an arrow. Click an arrow to select it (its conditions are
*                edited in the panel's side panel). Double-click a state to
*                open what it runs. The entry state has a green header.
*
*                Shift+click adds a state to the selection or takes it out;
*                dragging any selected state moves them all. Delete, Ctrl+C /
*                Ctrl+V / Ctrl+D work on every selected state (Delete on a
*                selected arrow too) - claimed with ImGui::Shortcut, so the
*                Hierarchy's global shortcuts don't also fire on the scene.
*
*                A .behaviorgraph dragged in from the Asset Browser is handed
*                to the panel through Result (the view can't look up assets).
*
*                Selection is by number (a state's or arrow's place in the
*                lists), so it survives edits that don't renumber.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <string>
#include <vector>

// Crucible engine
#include "AI/StateGraph.h"

// Crucible editor
#include "Graph/GraphCanvas.h"
#include "Graph/StateGraphEdit.h"

namespace Crucible {

    class StateGraphView {

    public:

        struct Result {

            bool        changed   = false;   // the graph was edited this frame
            int         openState = -1;      // a state was double-clicked: open what it runs

            // A .behaviorgraph was dropped on the canvas: its Assets path,
            // where a new state for it would go, and the state it was dropped
            // ON (-1 for empty canvas). The panel turns the path into an id.
            std::string droppedPath;
            Math::vec2  dropPosition{ 0.0f };
            int         dropState = -1;

        };  // RESULT STRUCT

        Result Draw(StateGraph& graph, bool readOnly);

        // The one selected state, or -1 if none or several are selected.
        int  SelectedState() const { return m_states.size() == 1 ? m_states.front() : -1; }
        const std::vector<int>& SelectedStates() const { return m_states; }
        int  SelectedTransition() const { return m_transition; }
        void SelectState(int index)      { m_states.assign(1, index); m_transition = -1; }
        void SelectTransition(int index) { m_transition = index; m_states.clear(); }
        void ClearSelection();

        void FrameAll(const StateGraph& graph);

        // Mid-gesture (moving states, drawing an arrow): wait before an undo step.
        bool IsBusy() const { return m_dragging || m_connectFrom >= 0 || m_draggingAny || m_connectAny; }

    private:

        bool DrawMenus(StateGraph& graph);   // true if the graph changed
        bool IsSelected(int state) const;

        // The group operations on the selection; each selects what it made.
        void CopySelection(const StateGraph& graph);
        void Paste(StateGraph& graph, const Math::vec2& topLeft);
        void DuplicateSelection(StateGraph& graph);
        void DeleteSelection(StateGraph& graph);

        GraphCanvas                    m_canvas;
        std::vector<int>               m_states;              // selected states, the last one clicked at the back
        int                            m_transition  = -1;    // selected arrow, or -1
        bool                           m_dragging    = false;
        bool                           m_moved       = false;
        int                            m_connectFrom = -1;    // drawing an arrow from this state
        bool                           m_connectAny  = false; // drawing an arrow from the Any State box (kAnyState can't go in m_connectFrom: -1 there means none)
        bool                           m_draggingAny = false; // moving the Any State box

        int                            m_menuState      = -1;
        int                            m_menuTransition = -1;
        Math::vec2                     m_menuPosition{ 0.0f };

        StateGraphEdit::StateClipboard m_clipboard;           // Ctrl+C keeps copies of the selected states

    };  // STATEGRAPHVIEW CLASS

}   // NAMESPACE CRUCIBLE
