/*************************************************************************
* \file:         BehaviorTreeView.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Drawing a behaviour tree and turning clicks into edits.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "BehaviorTreeView.h"

// STL
#include <algorithm>    // std::find, std::max
#include <cfloat>       // FLT_MAX
#include <string_view>

// Crucible engine
#include "AI/Blackboard.h"
#include "AI/NodeRegistry.h"
#include "Diagnostics/Logger.h"

// Crucible editor
#include "Graph/InspectScript.h"
#include "Graph/NodeFieldEditor.h"

namespace Crucible {

    namespace {

        // A node's size on the canvas, in graph units.
        constexpr Math::vec2 kNodeSize{ 220.0f, 58.0f };

        // The header colour says what kind of node it is, at a glance.
        ImU32 HeaderColour(std::string_view category) {

            if (category == "Events")     return IM_COL32(40, 150, 90, 255);    // green, like Unity's On Start
            if (category == "Flow")       return IM_COL32(130, 80, 200, 255);   // purple, like Unity's Sequence
            if (category == "Navigation") return IM_COL32(50, 110, 190, 255);
            if (category == "Debug")      return IM_COL32(160, 130, 40, 255);
            return IM_COL32(85, 92, 105, 255);

        }   // HeaderColour()

        const NodeTypeEntry* EntryOf(const BehaviorNode& node) {

            return NodeRegistry::Instance().Find(node.Description().id);

        }   // EntryOf()

    }   // ANONYMOUS NAMESPACE

    bool BehaviorTreeView::Draw(BehaviorGraph& graph, bool readOnly) {

        bool changed = false;
        m_canvas.Begin("##BehaviorTreeCanvas");

        const ImVec2 nodeSize = m_canvas.Scaled(kNodeSize);
        const auto topLeftOf = [&](const BehaviorNode* node) { return m_canvas.ToScreen(node->m_editorPosition); };
        const auto inPortOf  = [&](const BehaviorNode* node) { const ImVec2 p = topLeftOf(node); return ImVec2{ p.x + nodeSize.x * 0.5f, p.y }; };
        const auto outPortOf = [&](const BehaviorNode* node) { const ImVec2 p = topLeftOf(node); return ImVec2{ p.x + nodeSize.x * 0.5f, p.y + nodeSize.y }; };

        // 1. What's under the mouse. Later nodes are drawn on top, so the last
        // hit wins - the one you can see is the one you get.
        BehaviorTreeEdit::Collect(graph, m_nodes);
        const ImVec2 mouse     = m_canvas.Mouse();
        const float  portReach = std::max(8.0f, 10.0f * m_canvas.Zoom());
        BehaviorNode* hovered     = nullptr;
        BehaviorNode* hoveredPort = nullptr;
        for (const BehaviorTreeEdit::NodeInfo& info : m_nodes) {
            const ImVec2 p = topLeftOf(info.node);
            if (mouse.x >= p.x && mouse.y >= p.y && mouse.x <= p.x + nodeSize.x && mouse.y <= p.y + nodeSize.y) hovered = info.node;
            if (info.node->MaxChildren() > 0) {
                const ImVec2 port = outPortOf(info.node);
                const float dx = mouse.x - port.x, dy = mouse.y - port.y;
                if (dx * dx + dy * dy <= portReach * portReach) hoveredPort = info.node;
            }
        }
        if (!m_canvas.Hovered()) { hovered = nullptr; hoveredPort = nullptr; }

        // 2. The mouse, turned into edits.
        const ImGuiIO& io = ImGui::GetIO();
        if (m_canvas.LeftClicked()) {

            if (hoveredPort && !readOnly) {
                m_connectFrom = hoveredPort;
                Select(hoveredPort);
            }
            else if (hovered && io.KeyShift) {
                // Shift+click: add it to the selection, or take it back out.
                // Adding also starts a drag, so Shift+click-and-drag moves the group.
                const auto it = std::find(m_selection.begin(), m_selection.end(), hovered);
                if (it != m_selection.end()) m_selection.erase(it);
                else { m_selection.push_back(hovered); m_dragging = !readOnly; m_moved = false; }
            }
            else if (hovered) {
                // A plain click on a node already in the selection keeps the
                // selection, so the drag that follows moves the whole group.
                if (!IsSelected(hovered)) Select(hovered);
                m_dragging = !readOnly;
                m_moved    = false;
            }
            else if (!io.KeyShift) {
                m_selection.clear();
            }
            m_status.clear();

        }

        // Dragging moves every selected node. With Alt held their children come
        // too - then only the selection's tops move, or a selected child would
        // be moved twice (once by itself, once with its parent).
        if (m_dragging && m_canvas.LeftHeld() && !m_selection.empty()) {
            const ImVec2 delta = io.MouseDelta;
            if (delta.x != 0.0f || delta.y != 0.0f) {
                const Math::vec2 step{ delta.x / m_canvas.Zoom(), delta.y / m_canvas.Zoom() };
                if (io.KeyAlt) { for (BehaviorNode* node : SelectionTops(graph)) BehaviorTreeEdit::Offset(*node, step, true); }
                else           { for (BehaviorNode* node : m_selection)          BehaviorTreeEdit::Offset(*node, step, false); }
                m_moved = true;
            }
        }

        if (m_canvas.LeftReleased()) {

            // A move ends: re-sort, since a sideways move can change the run order.
            if (m_dragging) {
                m_dragging = false;
                if (m_moved) { BehaviorTreeEdit::SortAll(graph); changed = true; }
            }

            // A link ends: connect to whatever node it was dropped on.
            if (m_connectFrom) {
                if (hovered && hovered != m_connectFrom) {
                    std::string reason;
                    switch (BehaviorTreeEdit::Connect(graph, m_connectFrom, hovered, reason)) {
                        case BehaviorTreeEdit::ConnectResult::Connected:   changed = true; break;
                        case BehaviorTreeEdit::ConnectResult::NeedsBranch: m_branchParent = m_connectFrom; m_branchChild = hovered; m_openBranch = true; break;
                        case BehaviorTreeEdit::ConnectResult::Refused:     m_status = reason; break;
                    }
                }
                m_connectFrom = nullptr;
            }

        }

        // Right-click: the node's menu, or the canvas's (Add Node). Right-clicking
        // a node outside the selection selects just it; inside, the menu acts on
        // the whole selection.
        if (m_canvas.RightClicked() && !readOnly) {
            m_menuNode     = hovered;
            m_menuPosition = m_canvas.MouseGraph();
            if (hovered && !IsSelected(hovered)) Select(hovered);
            ImGui::OpenPopup(hovered ? "BehaviorNodeMenu" : "BehaviorCanvasMenu");
        }

        // Keys, while this canvas has focus and no text box is being typed in:
        // Delete, Ctrl+C, Ctrl+V (pasted under the mouse when it's over the
        // canvas, otherwise just offset from the copied nodes), Ctrl+D.
        //
        // Through ImGui::Shortcut, NOT IsKeyPressed: the Hierarchy registers
        // these same keys as GLOBAL shortcuts (they work whatever has focus),
        // and only a focused window that claims a key with Shortcut takes it
        // away from them. Reading the key directly let both fire - Delete on a
        // node also deleted the entity selected in the Hierarchy.
        if (!readOnly && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
            if (ImGui::Shortcut(ImGuiKey_Delete) && DeleteSelection(graph)) changed = true;
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) CopySelection(graph);
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V) && !m_clipboard.empty()) {
                const Math::vec2 at = m_canvas.Hovered() ? m_canvas.MouseGraph() : m_clipboard.front()->m_editorPosition + Math::vec2(30.0f);
                if (Paste(graph, at)) changed = true;
            }
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D) && !m_selection.empty()) {
                std::vector<BehaviorNode*> copies;
                for (BehaviorNode* top : SelectionTops(graph)) copies.push_back(BehaviorTreeEdit::Duplicate(graph, *top, { 30.0f, 30.0f }));
                m_selection = std::move(copies);
                changed = true;
            }
        }

        // 3. Draw. Collected again: an edit above may have moved or removed nodes.
        BehaviorTreeEdit::Collect(graph, m_nodes);
        const BehaviorNode* root = graph.Root();

        // Links first, so nodes sit on top of them. A Composite's children are
        // numbered: the order they run in.
        for (const BehaviorTreeEdit::NodeInfo& info : m_nodes) {
            if (!info.parent) continue;
            const ImU32 colour = info.attached ? IM_COL32(200, 205, 215, 255) : IM_COL32(120, 125, 135, 160);
            const ImVec2 to = inPortOf(info.node);
            m_canvas.DrawLink(outPortOf(info.parent), to, colour, 2.0f);
            if (info.parent->ChildCount() > 1) {
                const std::string order = std::to_string(info.index + 1);
                m_canvas.DrawLabel({ to.x + 8.0f * m_canvas.Zoom(), to.y - 20.0f * m_canvas.Zoom() }, IM_COL32(255, 215, 120, 255), order.c_str(), 0.9f);
            }
        }

        // The link being drawn follows the mouse.
        if (m_connectFrom) m_canvas.DrawLink(outPortOf(m_connectFrom), mouse, IM_COL32(255, 215, 110, 255), 2.0f);

        for (const BehaviorTreeEdit::NodeInfo& info : m_nodes) {

            const NodeDescription& description = info.node->Description();
            const NodeTypeEntry*   entry       = EntryOf(*info.node);
            const std::string      summary     = entry ? NodeFieldEditor::Summary(entry->reflect(*info.node)) : std::string();

            m_canvas.DrawNode(topLeftOf(info.node), nodeSize, HeaderColour(description.category),
                              std::string(description.name).c_str(), summary.c_str(), IsSelected(info.node), !info.attached);

            if (info.node != root)              m_canvas.DrawPort(inPortOf(info.node), false);
            if (info.node->MaxChildren() > 0)   m_canvas.DrawPort(outPortOf(info.node), info.node == hoveredPort);

        }

        m_canvas.End();

        if (hovered && !m_connectFrom && !m_dragging) {
            const auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const BehaviorTreeEdit::NodeInfo& info) { return info.node == hovered; });
            if (it != m_nodes.end() && !it->attached) ImGui::SetTooltip("Not connected - it won't run until it's under On Start.");
        }

        if (!readOnly && DrawMenus(graph)) changed = true;
        return changed;

    }   // BehaviorTreeView::Draw()

    bool BehaviorTreeView::DrawMenus(BehaviorGraph& graph) {

        bool changed = false;

        // Canvas: Add Node (one submenu per registry category), Paste, Frame All.
        if (ImGui::BeginPopup("BehaviorCanvasMenu")) {

            if (ImGui::BeginMenu("Add Node")) {
                std::vector<std::string_view> categories;
                for (const NodeTypeEntry& entry : NodeRegistry::Instance().All()) {
                    if (std::find(categories.begin(), categories.end(), entry.description->category) == categories.end()) categories.push_back(entry.description->category);
                }
                for (const std::string_view category : categories) {
                    if (!ImGui::BeginMenu(std::string(category).c_str())) continue;
                    for (const NodeTypeEntry& entry : NodeRegistry::Instance().All()) {
                        if (entry.description->category != category) continue;
                        if (ImGui::MenuItem(std::string(entry.description->name).c_str())) {
                            std::unique_ptr<BehaviorNode> node = entry.create();
                            node->m_editorPosition = m_menuPosition;
                            Select(BehaviorTreeEdit::AddDetached(graph, std::move(node)));
                            changed = true;
                        }
                        ImGui::SetItemTooltip("%s", std::string(entry.description->description).c_str());
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboard.empty()) && Paste(graph, m_menuPosition)) changed = true;
            if (ImGui::MenuItem("Frame All")) FrameAll(graph);
            ImGui::EndPopup();

        }

        // Node: Duplicate, Copy, Disconnect, Set as Root, Delete, Inspect Script.
        // Duplicate, Copy and Delete act on the whole selection; the rest only
        // make sense for the one node that was right-clicked.
        if (ImGui::BeginPopup("BehaviorNodeMenu")) {

            BehaviorNode* node = m_menuNode;
            if (node) {

                // Everything about `node` is read up front: Delete below frees
                // it, and the items after Delete are still drawn that frame.
                // (The registry entry lives on - it describes the type.)
                const bool           hasParent = BehaviorTreeEdit::ParentOf(graph, node) != nullptr;
                const bool           isRoot    = graph.Root() == node;
                const NodeTypeEntry* entry     = EntryOf(*node);

                if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                    // Copy + paste 30 units down-right, without touching the clipboard.
                    std::vector<BehaviorNode*> copies;
                    for (BehaviorNode* top : SelectionTops(graph)) copies.push_back(BehaviorTreeEdit::Duplicate(graph, *top, { 30.0f, 30.0f }));
                    m_selection = std::move(copies);
                    changed = true;
                }
                if (ImGui::MenuItem("Copy", "Ctrl+C")) CopySelection(graph);
                if (ImGui::MenuItem("Disconnect", nullptr, false, hasParent)) { BehaviorTreeEdit::Disconnect(graph, node); changed = true; }
                if (ImGui::MenuItem("Set as Root", nullptr, false, !hasParent && !isRoot)) { BehaviorTreeEdit::MakeRoot(graph, node); changed = true; }
                ImGui::SetItemTooltip("The root is where the graph starts. Usually an On Start node.");
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del") && DeleteSelection(graph)) changed = true;   // `node` is gone after this
                ImGui::Separator();
                if (ImGui::MenuItem("Inspect Script", nullptr, false, entry && entry->sourceFile && *entry->sourceFile)) InspectScript::OpenFile(entry->sourceFile);

            }
            ImGui::EndPopup();

        }

        // Branch Options: Unity's dialog for "this node can only have one child".
        if (m_openBranch) { ImGui::OpenPopup("Branch Options"); m_openBranch = false; }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
        if (ImGui::BeginPopupModal("Branch Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

            ImGui::TextWrapped("To connect to more than one node, pick a flow node to go in between. How should the branches run?");
            ImGui::Separator();

            // Every node type that takes many children is a candidate - made
            // once just to ask (registry types can't be asked without one).
            for (const NodeTypeEntry& entry : NodeRegistry::Instance().All()) {
                if (entry.create()->MaxChildren() <= 1) continue;
                if (ImGui::Button(std::string(entry.description->name).c_str(), { 220.0f, 0.0f })) {
                    if (BehaviorTreeEdit::InsertBranch(graph, m_branchParent, m_branchChild, entry.create())) changed = true;
                    m_branchParent = m_branchChild = nullptr;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", std::string(entry.description->description).c_str());
            }

            ImGui::Separator();
            if (ImGui::Button("Cancel")) { m_branchParent = m_branchChild = nullptr; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();

        }

        return changed;

    }   // BehaviorTreeView::DrawMenus()

    bool BehaviorTreeView::IsSelected(const BehaviorNode* node) const {

        return std::find(m_selection.begin(), m_selection.end(), node) != m_selection.end();

    }   // BehaviorTreeView::IsSelected()

    std::vector<BehaviorNode*> BehaviorTreeView::SelectionTops(BehaviorGraph& graph) const {

        // Walk up from each selected node: if any ancestor is also selected,
        // that ancestor's subtree already includes it.
        std::vector<BehaviorNode*> tops;
        for (BehaviorNode* node : m_selection) {
            bool covered = false;
            for (BehaviorNode* up = BehaviorTreeEdit::ParentOf(graph, node); up && !covered; up = BehaviorTreeEdit::ParentOf(graph, up)) {
                covered = IsSelected(up);
            }
            if (!covered) tops.push_back(node);
        }
        return tops;

    }   // BehaviorTreeView::SelectionTops()

    void BehaviorTreeView::CopySelection(BehaviorGraph& graph) {

        if (m_selection.empty()) return;
        m_clipboard.clear();
        for (const BehaviorNode* top : SelectionTops(graph)) m_clipboard.push_back(top->Clone());   // each with its subtree
        m_status.clear();

    }   // BehaviorTreeView::CopySelection()

    bool BehaviorTreeView::Paste(BehaviorGraph& graph, const Math::vec2& at) {

        if (m_clipboard.empty()) return false;

        // Keep the copied nodes' layout: shift them all by the same amount, so
        // the top-left-most one lands at `at`.
        Math::vec2 anchor = m_clipboard.front()->m_editorPosition;
        for (const auto& copy : m_clipboard) anchor = glm::min(anchor, copy->m_editorPosition);

        std::vector<BehaviorNode*> pasted;
        for (const auto& copy : m_clipboard) pasted.push_back(BehaviorTreeEdit::Duplicate(graph, *copy, at - anchor));
        m_selection = std::move(pasted);    // the pasted nodes, ready to drag into place
        return true;

    }   // BehaviorTreeView::Paste()

    bool BehaviorTreeView::DeleteSelection(BehaviorGraph& graph) {

        if (m_selection.empty()) return false;

        // Deleting a node leaves its children on the canvas as loose nodes, so
        // a selected child is still valid after its selected parent is gone.
        for (BehaviorNode* node : m_selection) BehaviorTreeEdit::Delete(graph, node);
        m_selection.clear();

        // Any other pointer could be one of the freed nodes. They'd be used on a
        // later frame (the menu's node, the link being drawn), so drop them all.
        m_menuNode    = nullptr;
        m_connectFrom = nullptr;
        return true;

    }   // BehaviorTreeView::DeleteSelection()

    bool BehaviorTreeView::DrawInspector(BehaviorGraph& graph, const Blackboard& variables, bool readOnly) {

        if (m_selection.empty()) {
            ImGui::TextDisabled("Select a node to edit its fields.");
            return false;
        }
        if (m_selection.size() > 1) {
            ImGui::TextDisabled("%zu nodes selected. Select just one to edit its fields.", m_selection.size());
            return false;
        }

        BehaviorNode* selected = m_selection.front();
        const NodeDescription& description = selected->Description();
        ImGui::TextUnformatted(std::string(description.name).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", std::string(description.description).c_str());
        ImGui::PopStyleColor();

        if (graph.Root() == selected) ImGui::TextWrapped("This is the root: the graph starts here.");
        else if (!BehaviorTreeEdit::IsInSubtree(graph.Root(), selected)) ImGui::TextColored({ 1.0f, 0.75f, 0.4f, 1.0f }, "Not connected - it won't run.");
        ImGui::Separator();

        const NodeTypeEntry* entry = EntryOf(*selected);
        if (!entry) { ImGui::TextDisabled("Unknown node type."); return false; }
        if (entry->type.get_properties().empty()) { ImGui::TextDisabled("This node has no settings."); return false; }
        return NodeFieldEditor::Draw(entry->reflect(*selected), variables, readOnly);

    }   // BehaviorTreeView::DrawInspector()

    void BehaviorTreeView::ClearSelection() {

        m_selection.clear();
        m_menuNode     = nullptr;
        m_connectFrom  = nullptr;
        m_branchParent = nullptr;
        m_branchChild  = nullptr;
        m_dragging     = false;

    }   // BehaviorTreeView::ClearSelection()

    void BehaviorTreeView::FrameAll(BehaviorGraph& graph) {

        BehaviorTreeEdit::Collect(graph, m_nodes);
        if (m_nodes.empty()) return;

        Math::vec2 min( FLT_MAX);
        Math::vec2 max(-FLT_MAX);
        for (const BehaviorTreeEdit::NodeInfo& info : m_nodes) {
            min = glm::min(min, info.node->m_editorPosition);
            max = glm::max(max, info.node->m_editorPosition + kNodeSize);
        }
        m_canvas.FrameBox(min, max);

    }   // BehaviorTreeView::FrameAll()

}   // NAMESPACE CRUCIBLE
