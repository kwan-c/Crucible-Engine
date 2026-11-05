/*************************************************************************
* \file:         ImGuiPanel_StateGraph.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The State Graph window: layout, the state and arrow side
*                panels, editing a state's tree in place, open / save.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "ImGuiPanel_StateGraph.h"

// STL
#include <algorithm>    // std::any_of
#include <array>
#include <filesystem>   // a dropped file's name, for its new state
#include <string_view>
#include <vector>

// ImGui
#include "misc/cpp/imgui_stdlib.h"   // ImGui::InputText(std::string*)

// Crucible engine
#include "AI/NodeCatalog.h"   // the C# node and condition types
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include "Resources/ResourceManager.h"
#include "Scene/SceneManager.h"

// Crucible editor
#include "Graph/InspectScript.h"
#include "Graph/NodeCatalogSync.h"
#include "Graph/NodeFieldEditor.h"
#include "Graph/StateGraphEdit.h"
#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "ImGui/Panels/ImGuiPanel_BehaviorGraph.h"

namespace Crucible {

    namespace {

        using StateGraphEdit::Content;

        constexpr std::array<const char*, 4> kContentNames{ "Nothing", "Behaviour tree", "Single node", "Subgraph (.behaviorgraph)" };

        int ContentIndex(Content content) {

            switch (content) {
                case Content::Empty:      return 0;
                case Content::Tree:       return 1;
                case Content::SingleNode: return 2;
                case Content::Subgraph:   return 3;
            }
            return 0;

        }   // ContentIndex()

        int CountNodes(const BehaviorNode* node) {

            if (!node) return 0;
            int count = 1;
            for (std::size_t i = 0; i < node->ChildCount(); ++i) count += CountNodes(node->ChildAt(i));
            return count;

        }   // CountNodes()

    }   // ANONYMOUS NAMESPACE

    void StateGraphPanel::Init() {

        m_panelName     = "State Graph";
        m_windowPadding = { 8.0f, 8.0f };
        m_docked        = false;    // floating, like the Behavior Graph window
        m_flags         = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    }   // StateGraphPanel::Init()

    void StateGraphPanel::Update(float) {

        if (!m_visible) return;
        SyncNodeCatalog(m_core.GetScriptRuntime());   // the C# node and condition types, fresh after a script reload

        ImGui::SetNextWindowSize({ 1200.0f, 680.0f }, ImGuiCond_FirstUseEver);
        if (m_makeFocus) { ImGui::SetNextWindowFocus(); m_makeFocus = false; }

        const bool readOnly = m_core.GetScenes().IsPlaySessionActive();
        const bool unsaved  = m_hasDocument && m_document.Dirty();

        bool open = true;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, m_windowPadding);
        if (ImGui::Begin(m_panelName.c_str(), &open, m_flags | (unsaved ? ImGuiWindowFlags_UnsavedDocument : 0))) {

            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (focused && !readOnly && m_hasDocument && !ImGui::GetIO().WantTextInput
                && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) Save();
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z) && m_document.Undo()) AfterRebuild();
                if ((ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) && m_document.Redo()) AfterRebuild();
            }

            DrawToolbar(readOnly);

            const std::string& status = (m_treeState >= 0 && !m_treeView.Status().empty()) ? m_treeView.Status() : m_message;
            const bool isError = (m_treeState >= 0 && !m_treeView.Status().empty()) || m_messageIsError;
            if (!status.empty()) ImGui::TextColored(isError ? ImVec4(1.0f, 0.55f, 0.43f, 1.0f) : ImVec4(0.6f, 0.85f, 0.6f, 1.0f), "%s", status.c_str());

            if (m_hasDocument) {

                StateGraph& graph = m_document.Graph();
                if (m_treeState >= static_cast<int>(graph.m_states.size())) m_treeState = -1;   // its state is gone

                const float width = ImGui::GetContentRegionAvail().x;
                const float left  = 210.0f;
                const float right = 300.0f;
                const float gap   = ImGui::GetStyle().ItemSpacing.x;

                // Left: the state graph's variables - shared by every state and its tree.
                ImGui::BeginChild("Variables", { left, 0.0f }, ImGuiChildFlags_Borders);
                std::string message;
                if (m_variablesEditor.Draw(graph.m_variables, readOnly, message)) m_pendingCommit = true;
                if (!message.empty()) SetMessage(message, true);
                ImGui::EndChild();

                // Middle: the state graph, or one state's tree with a breadcrumb back.
                ImGui::SameLine();
                ImGui::BeginChild("Canvas", { std::max(100.0f, width - left - right - gap * 2.0f), 0.0f }, ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                if (m_treeState >= 0) {
                    GraphState& state = graph.m_states[static_cast<std::size_t>(m_treeState)];
                    if (ImGui::SmallButton("< State Graph")) LeaveTree();
                    ImGui::SameLine();
                    ImGui::Text("/  %s  (its behaviour tree)", state.m_name.c_str());
                    ImGui::TextDisabled("Right-click to add nodes. Drag from a bottom dot to connect. Shift+click selects several.");
                    if (m_treeView.Draw(state.m_behavior, readOnly)) m_pendingCommit = true;
                } else {
                    ImGui::TextDisabled("Right-click to add states. Drag from a state's right dot onto another to add an arrow. Double-click a state to open what it runs.");
                    const StateGraphView::Result result = m_view.Draw(graph, readOnly);
                    if (result.changed) m_pendingCommit = true;
                    if (result.openState >= 0) OpenState(result.openState);
                    if (!result.droppedPath.empty()) DropSubgraph(result);
                }
                ImGui::EndChild();

                // Right: what's selected.
                ImGui::SameLine();
                ImGui::BeginChild("Details", { 0.0f, 0.0f }, ImGuiChildFlags_Borders);
                if (m_treeState >= 0) {
                    if (m_treeView.DrawInspector(graph.m_states[static_cast<std::size_t>(m_treeState)].m_behavior, graph.m_variables, readOnly)) m_pendingCommit = true;
                } else if (m_view.SelectedStates().size() > 1) {
                    ImGui::Text("%zu states selected.", m_view.SelectedStates().size());
                    ImGui::TextWrapped("Drag one to move them all. Delete, Ctrl+C and Ctrl+D work on all of them; arrows between them are copied too.");
                    ImGui::TextDisabled("Select just one to edit it.");
                } else if (m_view.SelectedState() >= 0 && m_view.SelectedState() < static_cast<int>(graph.m_states.size())) {
                    if (DrawStateInspector(m_view.SelectedState(), readOnly)) m_pendingCommit = true;
                } else if (m_view.SelectedTransition() >= 0 && m_view.SelectedTransition() < static_cast<int>(graph.m_transitions.size())) {
                    if (DrawTransitionInspector(m_view.SelectedTransition(), readOnly)) m_pendingCommit = true;
                } else {
                    ImGui::TextDisabled("Select a state or an arrow.");
                }
                ImGui::EndChild();

                // A condition made with Create new: onto its arrow the first frame C#
                // knows its type (or dropped, if that arrow is gone by then).
                if (!m_pendingCondition.empty() && !readOnly) {
                    if (const NodeTypeInfo* type = NodeCatalog::Instance().Find(m_pendingCondition)) {
                        if (m_pendingArrow >= 0 && m_pendingArrow < static_cast<int>(graph.m_transitions.size())) {
                            graph.m_transitions[static_cast<std::size_t>(m_pendingArrow)].m_conditions.push_back(MakeNode(*type));
                            m_view.SelectTransition(m_pendingArrow);
                            m_pendingCommit = true;
                            SetMessage("Added " + type->name + " to the arrow.", false);
                        }
                        m_pendingCondition.clear();
                        m_pendingArrow = -1;
                    }
                }

                // One undo step per finished gesture.
                const bool busy = m_treeState >= 0 ? m_treeView.IsBusy() : m_view.IsBusy();
                if (m_pendingCommit && !ImGui::IsAnyItemActive() && !busy) {
                    m_document.Commit();
                    m_pendingCommit = false;
                }

            } else {
                ImGui::TextWrapped("Click New to start a state graph, Open to pick one, or double-click a .stategraph in the Asset Browser.");
            }

            DrawDialogs();

        }
        ImGui::End();
        ImGui::PopStyleVar();

        if (!open) m_visible = false;

    }   // StateGraphPanel::Update()

    void StateGraphPanel::DrawToolbar(bool readOnly) {

        ImGui::BeginDisabled(readOnly);
        if (ImGui::Button("New")) Guard(Pending::New);
        ImGui::SameLine();
        if (ImGui::Button("Open")) m_openPicker = true;
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_hasDocument);
        if (ImGui::Button("Save")) Save();
        ImGui::SameLine();
        if (ImGui::Button("Save As")) m_openSaveAs = true;
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_document.CanUndo());
        if (ImGui::Button("Undo") && m_document.Undo()) AfterRebuild();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_document.CanRedo());
        if (ImGui::Button("Redo") && m_document.Redo()) AfterRebuild();
        ImGui::EndDisabled();
        ImGui::EndDisabled();   // !m_hasDocument
        ImGui::EndDisabled();   // readOnly

        ImGui::SameLine();
        ImGui::BeginDisabled(!m_hasDocument);
        if (ImGui::Button("Frame All")) {
            if (m_treeState >= 0) m_treeView.FrameAll(m_document.Graph().m_states[static_cast<std::size_t>(m_treeState)].m_behavior);
            else m_view.FrameAll(m_document.Graph());
        }
        ImGui::EndDisabled();

        if (m_hasDocument) {
            ImGui::SameLine();
            const std::string path = AssetPath(m_asset);
            ImGui::TextDisabled("%s%s", path.empty() ? "(not saved yet)" : path.c_str(), m_document.Dirty() ? " *" : "");
        }
        if (readOnly) { ImGui::SameLine(); ImGui::TextDisabled("- read-only during Play"); }

    }   // StateGraphPanel::DrawToolbar()

    bool StateGraphPanel::DrawStateInspector(int index, bool readOnly) {

        StateGraph& graph = m_document.Graph();
        GraphState& state = graph.m_states[static_cast<std::size_t>(index)];
        bool changed = false;

        ImGui::BeginDisabled(readOnly);

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##stateName", &state.m_name)) changed = true;

        if (graph.m_entryState == index) ImGui::TextColored({ 0.5f, 0.9f, 0.6f, 1.0f }, "Entry state: enemies start here.");
        else if (ImGui::Button("Set as Entry")) { graph.m_entryState = index; changed = true; }
        ImGui::Separator();

        // What it runs. Picking Subgraph opens the file picker; the state only
        // becomes a Subgraph once a file is chosen.
        const Content content = StateGraphEdit::ContentOf(state);
        int choice = ContentIndex(content);
        ImGui::TextUnformatted("Runs");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##content", &choice, kContentNames.data(), static_cast<int>(kContentNames.size())) && choice != ContentIndex(content)) {
            switch (choice) {
                case 0: StateGraphEdit::SetContent(state, Content::Empty);      changed = true; break;
                case 1: StateGraphEdit::SetContent(state, Content::Tree);       changed = true; break;
                case 2: StateGraphEdit::SetContent(state, Content::SingleNode); changed = true; break;
                case 3: m_subgraphState = index; m_openSubgraphPick = true;                    break;
                default: break;
            }
        }

        switch (StateGraphEdit::ContentOf(state)) {

            case Content::Empty:
                ImGui::TextDisabled("Runs nothing: the enemy just waits here until an arrow passes.");
                break;

            case Content::Tree:
                ImGui::Text("A behaviour tree of %d nodes, kept in this state.", CountNodes(state.m_behavior.Root()));
                if (ImGui::Button("Edit Tree", { -1.0f, 0.0f })) OpenState(index);
                ImGui::SetItemTooltip("Or double-click the state.");
                break;

            case Content::SingleNode: {
                // Which node: every C# action (a node that takes no children).
                BehaviorNode* node = state.m_behavior.Root();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##nodeType", std::string(node->Description().name).c_str())) {
                    for (const NodeTypeInfo& type : NodeCatalog::Instance().All()) {
                        if (type.kind != "action") continue;
                        if (ImGui::Selectable(type.name.c_str(), type.id == node->TypeId())) {
                            state.m_behavior.SetRoot(MakeNode(type));
                            node = state.m_behavior.Root();
                            changed = true;
                        }
                        ImGui::SetItemTooltip("%s", type.description.c_str());
                    }
                    ImGui::EndCombo();
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", std::string(node->Description().description).c_str());
                ImGui::PopStyleColor();
                if (NodeFieldEditor::Draw(*node, graph.m_variables, readOnly)) changed = true;
                ImGui::EndDisabled();   // opening the script is fine during Play
                if (ImGui::Button("Inspect Script", { -1.0f, 0.0f }) && !InspectScript::OpenNodeSource(*node)) SetMessage("C# doesn't know this node, so there's no script to open.", true);
                ImGui::SetItemTooltip("Open the C# file this node is written in. Or double-click the state.");
                ImGui::BeginDisabled(readOnly);
                break;
            }

            case Content::Subgraph: {
                const std::string path = AssetPath(state.m_subgraph);
                ImGui::TextWrapped("Runs %s", path.empty() ? "(a missing file)" : path.c_str());
                if (ImGui::Button("Change...")) { m_subgraphState = index; m_openSubgraphPick = true; }
                ImGui::SameLine();
                ImGui::EndDisabled();   // opening it is fine during Play
                if (ImGui::Button("Open")) OpenState(index);
                ImGui::BeginDisabled(readOnly);
                ImGui::TextDisabled("Its variables join this graph's at Play.");
                break;
            }

        }
        ImGui::EndDisabled();

        // On Enter / On Exit: one node each, run as the enemy enters and leaves.
        ImGui::Separator();
        if (DrawHook("On Enter", "Runs when the enemy enters this state, before its tree - which waits for it to finish.", state.m_onEnter, graph.m_variables, readOnly)) changed = true;
        if (DrawHook("On Exit", "Runs when the enemy leaves this state. It gets one frame, so use instant nodes (Log, Stop).", state.m_onExit, graph.m_variables, readOnly)) changed = true;

        // The arrows out of it, in the order they're checked.
        ImGui::Separator();
        ImGui::TextUnformatted("Arrows out (checked in this order)");
        int rank = 0;
        for (int i = 0; i < static_cast<int>(graph.m_transitions.size()); ++i) {
            const StateTransition& t = graph.m_transitions[static_cast<std::size_t>(i)];
            if (t.m_from != index || t.m_to < 0 || t.m_to >= static_cast<int>(graph.m_states.size())) continue;
            const std::string label = std::to_string(++rank) + ".  -> " + graph.m_states[static_cast<std::size_t>(t.m_to)].m_name
                                    + (t.m_conditions.empty() ? "   (no conditions!)" : "") + "##arrow" + std::to_string(i);
            if (ImGui::Selectable(label.c_str())) m_view.SelectTransition(i);
        }
        if (rank == 0) ImGui::TextDisabled("None yet - drag from this state's right dot onto another state.");

        return changed;

    }   // StateGraphPanel::DrawStateInspector()

    /*!
        \brief
        A state's On Enter or On Exit: which action (or none), its fields, and
        Inspect Script. True if anything changed.
    */
    bool StateGraphPanel::DrawHook(const char* label, const char* help, std::unique_ptr<BehaviorNode>& hook, const Blackboard& variables, bool readOnly) {

        bool changed = false;
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SetItemTooltip("%s", help);
        ImGui::SameLine();

        ImGui::BeginDisabled(readOnly);
        ImGui::SetNextItemWidth(-1.0f);
        const std::string current = hook ? std::string(hook->Description().name) : std::string("(nothing)");
        if (ImGui::BeginCombo("##hook", current.c_str())) {
            if (ImGui::Selectable("(nothing)", !hook)) { hook.reset(); changed = true; }
            for (const NodeTypeInfo& type : NodeCatalog::Instance().All()) {
                if (type.kind != "action") continue;   // one node, no children: an action
                if (ImGui::Selectable(type.name.c_str(), hook && hook->TypeId() == type.id)) { hook = MakeNode(type); changed = true; }
                ImGui::SetItemTooltip("%s", type.description.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        if (hook) {
            ImGui::Indent();
            if (NodeFieldEditor::Draw(*hook, variables, readOnly)) changed = true;
            if (ImGui::SmallButton("Inspect Script") && !InspectScript::OpenNodeSource(*hook)) SetMessage("C# doesn't know this node, so there's no script to open.", true);
            ImGui::Unindent();
        }
        ImGui::PopID();
        return changed;

    }   // StateGraphPanel::DrawHook()

    bool StateGraphPanel::DrawTransitionInspector(int index, bool readOnly) {

        StateGraph& graph = m_document.Graph();
        StateTransition& transition = graph.m_transitions[static_cast<std::size_t>(index)];
        const int stateCount = static_cast<int>(graph.m_states.size());
        const bool fromAny = transition.m_from == kAnyState;
        if ((!fromAny && (transition.m_from < 0 || transition.m_from >= stateCount)) || transition.m_to < 0 || transition.m_to >= stateCount) {
            ImGui::TextDisabled("This arrow points at a missing state.");
            return false;
        }
        bool changed = false;

        ImGui::Text("%s  ->  %s", fromAny ? "Any State" : graph.m_states[static_cast<std::size_t>(transition.m_from)].m_name.c_str(),
                                  graph.m_states[static_cast<std::size_t>(transition.m_to)].m_name.c_str());

        // Its place among the arrows out of the same state (or out of Any State).
        int rank = 0, total = 0;
        for (int i = 0; i < static_cast<int>(graph.m_transitions.size()); ++i) {
            if (graph.m_transitions[static_cast<std::size_t>(i)].m_from != transition.m_from) continue;
            ++total;
            if (i == index) rank = total;
        }
        if (fromAny) ImGui::TextDisabled("Checked %d of %d Any State arrows - before the current state's own arrows.", rank, total);
        else         ImGui::TextDisabled("Checked %d of %d arrows out of this state.", rank, total);

        ImGui::BeginDisabled(readOnly);
        if (ImGui::Button("Check Earlier")) { m_view.SelectTransition(StateGraphEdit::MovePriority(graph, index, -1)); changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Check Later"))   { m_view.SelectTransition(StateGraphEdit::MovePriority(graph, index, +1)); changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Delete Arrow"))  { StateGraphEdit::DeleteTransition(graph, index); m_view.ClearSelection(); ImGui::EndDisabled(); return true; }

        ImGui::Separator();
        ImGui::TextUnformatted("Conditions - ALL must pass");
        if (transition.m_conditions.empty()) ImGui::TextColored({ 1.0f, 0.75f, 0.4f, 1.0f }, "None: switches the moment the enemy enters.");

        int removeAt = -1;
        for (int i = 0; i < static_cast<int>(transition.m_conditions.size()); ++i) {

            BehaviorNode* condition = transition.m_conditions[static_cast<std::size_t>(i)].get();   // a condition is a node with no children
            if (!condition) continue;
            ImGui::PushID(i);

            const NodeDescription description = condition->Description();
            ImGui::TextUnformatted(std::string(description.name).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) removeAt = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("Script") && !InspectScript::OpenNodeSource(*condition)) SetMessage("C# doesn't know this condition, so there's no script to open.", true);
            ImGui::SetItemTooltip("Open the C# file this condition is written in.");
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", std::string(description.description).c_str());
            ImGui::PopStyleColor();
            if (NodeFieldEditor::Draw(*condition, graph.m_variables, readOnly)) changed = true;
            ImGui::Separator();
            ImGui::PopID();

        }
        if (removeAt >= 0) { transition.m_conditions.erase(transition.m_conditions.begin() + removeAt); changed = true; }

        if (ImGui::Button("Add Condition", { -1.0f, 0.0f })) ImGui::OpenPopup("AddCondition");
        if (ImGui::BeginPopup("AddCondition")) {
            bool any = false;
            for (const NodeTypeInfo& type : NodeCatalog::Instance().All()) {
                if (!type.IsCondition()) continue;
                any = true;
                if (ImGui::MenuItem(type.name.c_str())) {
                    transition.m_conditions.push_back(MakeNode(type));
                    changed = true;
                }
                ImGui::SetItemTooltip("%s", type.description.c_str());
            }
            if (!any) ImGui::TextDisabled("No conditions: C# scripting isn't running, or the scripts didn't build.");
            ImGui::Separator();
            if (ImGui::MenuItem("Create new Condition...")) { m_conditionDialog.Open(NodeScriptDialog::Kind::Condition); m_pendingArrow = index; }
            ImGui::SetItemTooltip("Write a C# condition of your own; it goes on this arrow once the scripts compile.");
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();

        return changed;

    }   // StateGraphPanel::DrawTransitionInspector()

    void StateGraphPanel::DrawDialogs() {

        // Create new Condition: once its script is written, wait for it to compile.
        if (const std::string created = m_conditionDialog.Draw(); !created.empty()) {
            m_pendingCondition = created;
            SetMessage("Script written - the condition goes on the arrow once the scripts have compiled.", false);
        }

        // Open: every .stategraph in Assets.
        if (m_openPicker) { ImGui::OpenPopup("Open State Graph"); m_openPicker = false; }
        if (ImGui::BeginPopup("Open State Graph")) {
            bool any = false;
            for (const AssetEntry& entry : m_core.GetAssets().GetAll()) {
                if (entry.type != AssetType::StateGraph) continue;
                any = true;
                if (ImGui::Selectable(entry.virtualPath.c_str())) { Guard(Pending::Open, entry.guid.m_Value); ImGui::CloseCurrentPopup(); }
            }
            if (!any) ImGui::TextDisabled("No .stategraph files in Assets yet.");
            ImGui::EndPopup();
        }

        // Subgraph: every .behaviorgraph in Assets.
        if (m_openSubgraphPick) { ImGui::OpenPopup("Choose Subgraph"); m_openSubgraphPick = false; }
        if (ImGui::BeginPopup("Choose Subgraph")) {
            bool any = false;
            StateGraph& graph = m_document.Graph();
            for (const AssetEntry& entry : m_core.GetAssets().GetAll()) {
                if (entry.type != AssetType::BehaviorGraph) continue;
                any = true;
                if (ImGui::Selectable(entry.virtualPath.c_str()) && m_subgraphState >= 0 && m_subgraphState < static_cast<int>(graph.m_states.size())) {
                    GraphState& state = graph.m_states[static_cast<std::size_t>(m_subgraphState)];
                    StateGraphEdit::SetContent(state, Content::Subgraph);   // its own nodes go: the file runs instead
                    state.m_subgraph = entry.guid.m_Value;
                    m_pendingCommit = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!any) ImGui::TextDisabled("No .behaviorgraph files yet - Asset Browser > Create Behavior Graph.");
            ImGui::EndPopup();
        }

        // Save As.
        if (m_openSaveAs) { ImGui::OpenPopup("Save State Graph As"); m_openSaveAs = false; }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
        if (ImGui::BeginPopupModal("Save State Graph As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Path inside Assets:");
            ImGui::SetNextItemWidth(420.0f);
            ImGui::InputText("##path", m_savePath, sizeof m_savePath);
            if (ImGui::Button("Save") && SaveAs(m_savePath)) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Unsaved changes.
        if (m_openGuard) { ImGui::OpenPopup("Unsaved State Graph"); m_openGuard = false; }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
        if (ImGui::BeginPopupModal("Unsaved State Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("This state graph has unsaved changes. Discard them?");
            if (ImGui::Button("Discard")) {
                const Pending action = m_pending;
                m_pending = Pending::None;
                if (action == Pending::New) NewGraph();
                else if (action == Pending::Open) Load(m_pendingAsset);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { m_pending = Pending::None; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

    }   // StateGraphPanel::DrawDialogs()

    void StateGraphPanel::OpenState(int index) {

        StateGraph& graph = m_document.Graph();
        if (index < 0 || index >= static_cast<int>(graph.m_states.size())) return;
        GraphState& state = graph.m_states[static_cast<std::size_t>(index)];

        switch (StateGraphEdit::ContentOf(state)) {
            case Content::Tree:
                m_treeState = index;
                m_treeView.ClearSelection();
                m_treeView.FrameAll(state.m_behavior);
                break;
            case Content::Subgraph:
                if (auto behavior = EditorManager::GetInstance().GetPanel<BehaviorGraphPanel>(ImguiPanels::BEHAVIORGRAPH)) behavior->RequestOpen(state.m_subgraph);
                break;
            case Content::SingleNode:
                // "Inspect Script": the file the node is written in (a .cpp
                // for the built-in nodes). Its fields stay in the side panel.
                m_view.SelectState(index);
                if (!InspectScript::OpenNodeSource(*state.m_behavior.Root())) SetMessage("This node's source file isn't known.", true);
                break;
            default:
                m_view.SelectState(index);   // nothing to open: its side panel says what it runs
                break;
        }

    }   // StateGraphPanel::OpenState()

    /*!
        \brief
        A .behaviorgraph dropped from the Asset Browser. On a state: that state
        now runs it. On empty canvas: a new state, named after the file, that
        runs it.
    */
    void StateGraphPanel::DropSubgraph(const StateGraphView::Result& drop) {

        const AssetEntry* entry = m_core.GetAssets().Find(drop.droppedPath);
        if (!entry || entry->type != AssetType::BehaviorGraph) { SetMessage("Could not find " + drop.droppedPath + " in Assets", true); return; }

        StateGraph& graph = m_document.Graph();
        int index = drop.dropState;
        if (index < 0 || index >= static_cast<int>(graph.m_states.size())) {
            const bool first = graph.m_states.empty();
            index = StateGraphEdit::AddState(graph, std::filesystem::path(entry->virtualPath).stem().string(), drop.dropPosition);
            if (first) graph.m_entryState = 0;   // the first state is the entry
        }

        GraphState& state = graph.m_states[static_cast<std::size_t>(index)];
        StateGraphEdit::SetContent(state, Content::Subgraph);   // its own nodes go: the file runs instead
        state.m_subgraph = entry->guid.m_Value;
        m_view.SelectState(index);
        m_pendingCommit = true;
        SetMessage(state.m_name + " now runs " + entry->virtualPath, false);

    }   // StateGraphPanel::DropSubgraph()

    void StateGraphPanel::LeaveTree() {

        if (m_treeState >= 0) m_view.SelectState(m_treeState);
        m_treeState = -1;
        m_treeView.ClearSelection();

    }   // StateGraphPanel::LeaveTree()

    void StateGraphPanel::AfterRebuild() {

        m_treeView.ClearSelection();
        const int stateCount = static_cast<int>(m_document.Graph().m_states.size());
        if (m_treeState >= stateCount) m_treeState = -1;
        const std::vector<int>& states = m_view.SelectedStates();
        if (std::any_of(states.begin(), states.end(), [&](int i) { return i >= stateCount; })
            || m_view.SelectedTransition() >= static_cast<int>(m_document.Graph().m_transitions.size())) m_view.ClearSelection();

    }   // StateGraphPanel::AfterRebuild()

    void StateGraphPanel::Guard(Pending action, std::uint64_t asset) {

        if (m_hasDocument && m_document.Dirty()) {
            m_pending      = action;
            m_pendingAsset = asset;
            m_openGuard    = true;
            return;
        }
        if (action == Pending::New) NewGraph();
        else if (action == Pending::Open) Load(asset);

    }   // StateGraphPanel::Guard()

    void StateGraphPanel::RequestOpen(std::uint64_t asset) {

        Show();
        if (m_hasDocument && asset == m_asset) return;
        Guard(Pending::Open, asset);

    }   // StateGraphPanel::RequestOpen()

    void StateGraphPanel::NewGraph() {

        m_document.New();
        m_asset         = 0;
        m_hasDocument   = true;
        m_pendingCommit = false;
        m_treeState     = -1;
        m_view.ClearSelection();
        m_treeView.ClearSelection();
        m_view.FrameAll(m_document.Graph());
        SetMessage("New state graph. Right-click to add states; drag from a state's right dot to add arrows.", false);

    }   // StateGraphPanel::NewGraph()

    bool StateGraphPanel::Load(std::uint64_t asset) {

        const AssetEntry* entry = m_core.GetAssets().FindByGuid(xresource::instance_guid{ asset });
        std::vector<unsigned char> bytes;
        if (!entry || entry->type != AssetType::StateGraph || !m_core.GetAssets().ReadBytes(entry->virtualPath, bytes)) {
            SetMessage("Could not read that state graph", true);
            return false;
        }

        std::string error;
        if (!m_document.Load(bytes, error)) { SetMessage(entry->virtualPath + ": " + error, true); return false; }

        m_asset         = asset;
        m_hasDocument   = true;
        m_pendingCommit = false;
        m_treeState     = -1;
        m_view.ClearSelection();
        m_treeView.ClearSelection();
        m_view.FrameAll(m_document.Graph());
        SetMessage("Opened " + entry->virtualPath, false);
        return true;

    }   // StateGraphPanel::Load()

    bool StateGraphPanel::Save() {

        if (!m_hasDocument) return false;
        if (!m_asset) { m_openSaveAs = true; return false; }

        m_document.Commit();
        m_pendingCommit = false;

        const std::string path = AssetPath(m_asset);
        if (path.empty()) { SetMessage("The file is gone from Assets - use Save As", true); return false; }

        std::string error;
        if (!m_core.GetResources().SaveTextAsset(path, m_document.ToJson(), false, &error)) { SetMessage(error, true); return false; }

        m_document.MarkSaved();
        SetMessage("Saved " + path + " - enemies use it from the next Play.", false);
        if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) browser->Refresh();
        return true;

    }   // StateGraphPanel::Save()

    bool StateGraphPanel::SaveAs(std::string path) {

        if (!m_hasDocument) return false;
        constexpr std::string_view kExtension = ".stategraph";
        if (path.size() < kExtension.size() || path.compare(path.size() - kExtension.size(), kExtension.size(), kExtension) != 0) path += kExtension;

        m_document.Commit();
        m_pendingCommit = false;

        std::string error;
        const AssetId id = m_core.GetResources().SaveTextAsset(path, m_document.ToJson(), true, &error);
        if (!id) { SetMessage(error, true); return false; }

        m_asset = id;
        m_document.MarkSaved();
        SetMessage("Saved " + path, false);
        if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) browser->Refresh();
        return true;

    }   // StateGraphPanel::SaveAs()

    std::string StateGraphPanel::AssetPath(std::uint64_t asset) const {

        if (!asset) return {};
        const AssetEntry* entry = m_core.GetAssets().FindByGuid(xresource::instance_guid{ asset });
        return entry ? entry->virtualPath : std::string();

    }   // StateGraphPanel::AssetPath()

}   // NAMESPACE CRUCIBLE
