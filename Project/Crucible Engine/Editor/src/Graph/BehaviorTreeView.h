/*************************************************************************
* \file:         BehaviorTreeView.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                BehaviorTreeView: draws one behaviour tree on a GraphCanvas
*                and turns mouse work into edits - select, move, connect,
*                the right-click menus, Branch Options - plus the side panel
*                for the selected node's fields.
*
* \details
*                The tree reads top to bottom: a node's input dot is on top,
*                its output dot underneath (only on nodes that can have
*                children). Drag from an output dot onto a node to connect.
*                A Sequence's children run left to right, so moving a node
*                sideways changes when it runs; the order is shown on the
*                links.
*
*                Shift+click adds or removes a node from the selection, and
*                dragging any selected node moves them all (Alt-drag brings
*                their children too). Ctrl+C / Ctrl+V copy and paste the
*                selection, Delete removes it. Double-click a node to open
*                its C# script.
*
*                The node types in Add Node and Branch Options are the C#
*                ones (NodeCatalog); their fields come from the C# class.
*
*                It edits the BehaviorGraph it is given and reports "changed";
*                recording undo steps is the owner's job, so the same view
*                can edit a .behaviorgraph now and a state's tree later.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <memory>
#include <string>
#include <vector>

// Crucible editor
#include "Graph/BehaviorTreeEdit.h"
#include "Graph/GraphCanvas.h"
#include "Graph/NodeScriptDialog.h"

namespace Crucible {

    class Blackboard;

    class BehaviorTreeView {

    public:

        // The canvas and its menus. True if the graph changed this frame.
        bool Draw(BehaviorGraph& graph, bool readOnly);

        // The selected node's name, description and fields. True if a field changed.
        bool DrawInspector(BehaviorGraph& graph, const Blackboard& variables, bool readOnly);

        // Forget the selection (and any open menu's node): call after the graph
        // is rebuilt (undo, redo, load), since the old node pointers are gone.
        void ClearSelection();

        // Fit every node in view (next frame).
        void FrameAll(BehaviorGraph& graph);

        // Mid-gesture (dragging a node, drawing a link): the owner should wait
        // for it to finish before recording one undo step for the whole thing.
        bool IsBusy() const { return m_dragging || m_connectFrom != nullptr; }

        // The last refusal ("On Start can't have two children..."), for the toolbar.
        const std::string& Status() const { return m_status; }

    private:

        // The menus and the Branch Options dialog. True if the graph changed.
        bool DrawMenus(BehaviorGraph& graph);

        bool IsSelected(const BehaviorNode* node) const;
        void Select(BehaviorNode* node) { m_selection.assign(1, node); }

        // The selected nodes that aren't under another selected node: what a
        // copy, or an Alt-drag, should act on once each (children come along).
        std::vector<BehaviorNode*> SelectionTops(BehaviorGraph& graph) const;

        // Clipboard: copies of the selection's top nodes (with their subtrees).
        void CopySelection(BehaviorGraph& graph);
        // Pastes the clipboard so its top-left node lands at `at`. True if anything was pasted.
        bool Paste(BehaviorGraph& graph, const Math::vec2& at);
        // Deletes every selected node. True if anything was deleted.
        bool DeleteSelection(BehaviorGraph& graph);

        GraphCanvas                             m_canvas;
        std::vector<BehaviorTreeEdit::NodeInfo> m_nodes;            // this frame's nodes (reused)

        std::vector<BehaviorNode*>              m_selection;        // the last one clicked is at the back
        bool                                    m_dragging    = false;
        bool                                    m_moved       = false;
        BehaviorNode*                           m_connectFrom = nullptr;   // drawing a link from this node's output

        BehaviorNode*                           m_menuNode    = nullptr;   // the node a right-click menu is about
        Math::vec2                              m_menuPosition{ 0.0f };    // where on the graph it was opened

        BehaviorNode*                           m_branchParent = nullptr;  // a connection waiting for Branch Options
        BehaviorNode*                           m_branchChild  = nullptr;
        bool                                    m_openBranch   = false;

        std::vector<std::unique_ptr<BehaviorNode>> m_clipboard;   // Copy keeps copies here; Paste copies them again
        std::string                             m_status;

        // Add Node > Create new: the dialog, and the node waiting for its new
        // script to compile before it can be placed (at m_pendingPosition).
        NodeScriptDialog                        m_scriptDialog;
        std::string                             m_pendingType;
        Math::vec2                              m_pendingPosition{ 0.0f };

    };  // BEHAVIORTREEVIEW CLASS

}   // NAMESPACE CRUCIBLE
