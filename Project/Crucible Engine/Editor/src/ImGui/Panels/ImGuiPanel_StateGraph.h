/*************************************************************************
* \file:         ImGuiPanel_StateGraph.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The State Graph window: edit a .stategraph visually -
*                variables on the left, states and arrows in the middle, the
*                selected state's or arrow's settings on the right.
*
* \details
*                What a state runs is chosen in its side panel: nothing, a
*                behaviour tree kept inside the state, a single node, or a
*                Subgraph (a separate .behaviorgraph file). Double-click a
*                tree state to edit its tree right here - a breadcrumb leads
*                back - a Subgraph state to open its file in the Behavior
*                Graph window, or a single-node state to open the node's
*                source file. Dropping a .behaviorgraph from the Asset
*                Browser on a state makes it run that file; on empty canvas
*                it adds a new state that runs it.
*
*                A floating window (View > State Graph, or double-click a
*                .stategraph in the Asset Browser). Read-only during Play.
*                Closing only hides it.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <string>

// Crucible editor
#include "Graph/BehaviorTreeView.h"
#include "Graph/NodeScriptDialog.h"
#include "Graph/StateGraphDocument.h"
#include "Graph/StateGraphView.h"
#include "Graph/VariablesEditor.h"
#include "ImGui/ImGuiManager.h"

namespace Crucible {

    class Core;

    class StateGraphPanel final : public EditorPanel {

    public:

        explicit StateGraphPanel(Core& core) : m_core(core) {}

        void Init() override;
        void Update(float) override;

        void Show()             { m_visible = true; m_makeFocus = true; }
        bool IsOpen() const     { return m_visible; }
        void SetOpen(bool open) { m_visible = open; }

        // Opens a .stategraph by asset id (the Asset Browser's double-click).
        void RequestOpen(std::uint64_t asset);

    private:

        enum class Pending { None, New, Open };

        void NewGraph();
        bool Load(std::uint64_t asset);
        bool Save();
        bool SaveAs(std::string path);
        void Guard(Pending action, std::uint64_t asset = 0);

        // A state was double-clicked: edit its tree here, open its Subgraph
        // file, open its single node's source file, or just select it.
        void OpenState(int index);
        void DropSubgraph(const StateGraphView::Result& drop);   // a .behaviorgraph dropped on the canvas
        void LeaveTree();
        void AfterRebuild();    // after undo / redo / load: the old node pointers are gone

        void DrawToolbar(bool readOnly);
        bool DrawStateInspector(int index, bool readOnly);        // true if changed
        bool DrawTransitionInspector(int index, bool readOnly);   // true if changed
        bool DrawHook(const char* label, const char* help, std::unique_ptr<BehaviorNode>& hook, const Blackboard& variables, bool readOnly);   // On Enter / On Exit
        void DrawDialogs();

        std::string AssetPath(std::uint64_t asset) const;          // "" if unknown
        void SetMessage(std::string text, bool error) { m_message = std::move(text); m_messageIsError = error; }

        Core&              m_core;
        StateGraphDocument m_document;
        StateGraphView     m_view;
        BehaviorTreeView   m_treeView;      // a state's tree, when one is being edited
        VariablesEditor    m_variablesEditor;

        int                m_treeState      = -1;      // the state whose tree is open in the middle; -1 = the state graph
        std::uint64_t      m_asset          = 0;
        bool               m_hasDocument    = false;
        bool               m_visible        = false;
        bool               m_pendingCommit  = false;
        std::string        m_message;
        bool               m_messageIsError = false;

        Pending            m_pending          = Pending::None;
        std::uint64_t      m_pendingAsset     = 0;
        bool               m_openPicker       = false;
        bool               m_openSaveAs       = false;
        bool               m_openGuard        = false;
        bool               m_openSubgraphPick = false;
        int                m_subgraphState    = -1;    // the state the Subgraph picker is choosing for

        // Add Condition > Create new Condition: the dialog, and the condition
        // waiting for its script to compile before it goes on arrow m_pendingArrow.
        NodeScriptDialog   m_conditionDialog;
        std::string        m_pendingCondition;
        int                m_pendingArrow     = -1;
        char               m_savePath[512]    = "AI/New State Graph.stategraph";

    };  // STATEGRAPHPANEL CLASS

}   // NAMESPACE CRUCIBLE
