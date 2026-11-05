/*************************************************************************
* \file:         ImGuiPanel_BehaviorGraph.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The Behavior Graph window: edit a .behaviorgraph visually -
*                variables on the left, the tree in the middle, the
*                selected node's fields on the right.
*
* \details
*                A floating window (View > Behavior Graph, or double-click a
*                .behaviorgraph in the Asset Browser). Read-only during Play:
*                running enemies use the graph as it was when Play started.
*
*                Closing the window only hides it - the open graph, unsaved
*                edits included, is still there when it is reopened.
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
#include "Graph/BehaviorGraphDocument.h"
#include "Graph/BehaviorTreeView.h"
#include "Graph/VariablesEditor.h"
#include "ImGui/ImGuiManager.h"

namespace Crucible {

    class Core;

    class BehaviorGraphPanel final : public EditorPanel {

    public:

        explicit BehaviorGraphPanel(Core& core) : m_core(core) {}

        void Init() override;
        void Update(float) override;

        void Show()               { m_visible = true; m_makeFocus = true; }
        bool IsOpen() const       { return m_visible; }
        void SetOpen(bool open)   { m_visible = open; }

        // Opens a .behaviorgraph by asset id (the Asset Browser's double-click).
        void RequestOpen(std::uint64_t asset);

    private:

        // What an unsaved-changes prompt is guarding.
        enum class Pending { None, New, Open };

        void NewGraph();
        bool Load(std::uint64_t asset);
        bool Save();
        bool SaveAs(std::string path);

        // New or Open, or ask first if there are unsaved changes.
        void Guard(Pending action, std::uint64_t asset = 0);

        void DrawToolbar(bool readOnly);
        bool DrawVariables(bool readOnly);   // true if a variable changed
        void DrawDialogs();

        void SetMessage(std::string text, bool error) { m_message = std::move(text); m_messageIsError = error; }

        Core&                 m_core;
        BehaviorGraphDocument m_document;
        BehaviorTreeView      m_view;

        std::uint64_t         m_asset          = 0;       // the file being edited; 0 = never saved
        bool                  m_hasDocument    = false;
        bool                  m_visible        = false;
        bool                  m_pendingCommit  = false;   // edited since the last undo step
        std::string           m_message;
        bool                  m_messageIsError = false;

        Pending               m_pending       = Pending::None;
        std::uint64_t         m_pendingAsset  = 0;
        bool                  m_openPicker    = false;
        bool                  m_openSaveAs    = false;
        bool                  m_openGuard     = false;
        char                  m_savePath[512] = "AI/New Behavior.behaviorgraph";
        VariablesEditor       m_variablesEditor;

    };  // BEHAVIORGRAPHPANEL CLASS

}   // NAMESPACE CRUCIBLE
