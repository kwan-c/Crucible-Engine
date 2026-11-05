/*************************************************************************
* \file:         StateGraphView.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Drawing a state graph and turning clicks into edits.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "StateGraphView.h"

// STL
#include <algorithm>    // std::clamp, std::max, std::min
#include <cfloat>       // FLT_MAX
#include <cmath>        // std::sqrt
#include <string>
#include <string_view>

// Crucible editor
#include "Graph/InspectScript.h"
#include "Graph/StateGraphEdit.h"
#include "ImGui/EditorWidgets.h"    // kAssetPayload: what the Asset Browser drags

namespace Crucible {

    namespace {

        constexpr Math::vec2 kStateSize{ 190.0f, 56.0f };   // graph units

        // Distance from p to the segment a-b, in pixels: how arrows are clicked.
        float DistanceToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {

            const float dx = b.x - a.x, dy = b.y - a.y;
            const float lengthSq = dx * dx + dy * dy;
            const float t  = lengthSq > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSq, 0.0f, 1.0f) : 0.0f;
            const float cx = a.x + t * dx - p.x, cy = a.y + t * dy - p.y;
            return std::sqrt(cx * cx + cy * cy);

        }   // DistanceToSegment()

        // Where a line leaving a box's centre in direction (dx, dy) crosses the
        // box's edge - so arrows start and end at the boxes, not inside them.
        ImVec2 EdgePoint(const ImVec2& centre, const ImVec2& half, float dx, float dy) {

            const float ax = std::abs(dx), ay = std::abs(dy);
            if (ax < 1e-4f && ay < 1e-4f) return centre;
            const float t = std::min(ax > 1e-4f ? half.x / ax : FLT_MAX, ay > 1e-4f ? half.y / ay : FLT_MAX);
            return { centre.x + dx * t, centre.y + dy * t };

        }   // EdgePoint()

        const char* ContentLabel(const GraphState& state) {

            switch (StateGraphEdit::ContentOf(state)) {
                case StateGraphEdit::Content::Tree:       return "Behaviour tree";
                case StateGraphEdit::Content::SingleNode: return nullptr;   // the node's own name is shown instead
                case StateGraphEdit::Content::Subgraph:   return "Subgraph";
                case StateGraphEdit::Content::Empty:      return "Empty";
            }
            return "";

        }   // ContentLabel()

    }   // ANONYMOUS NAMESPACE

    StateGraphView::Result StateGraphView::Draw(StateGraph& graph, bool readOnly) {

        Result result;
        m_canvas.Begin("##StateGraphCanvas");

        // A .behaviorgraph dragged in from the Asset Browser. This must come
        // straight after Begin: BeginDragDropTarget works on the last item,
        // the canvas's invisible button. The payload is peeked at first, so
        // other kinds of asset don't light the canvas up as a target.
        std::string dropped;
        if (!readOnly && ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (payload && payload->IsDataType(EditorWidgets::kAssetPayload)
                && std::string_view(static_cast<const char*>(payload->Data)).ends_with(".behaviorgraph")) {
                if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload)) dropped = static_cast<const char*>(accepted->Data);
            }
            ImGui::EndDragDropTarget();
        }

        const int    stateCount = static_cast<int>(graph.m_states.size());
        const ImVec2 size       = m_canvas.Scaled(kStateSize);
        const ImVec2 half{ size.x * 0.5f, size.y * 0.5f };
        const auto topLeftOf = [&](int i) { return m_canvas.ToScreen(graph.m_states[static_cast<std::size_t>(i)].m_editorPosition); };
        const auto centreOf  = [&](int i) { const ImVec2 p = topLeftOf(i); return ImVec2{ p.x + half.x, p.y + half.y }; };
        const auto portOf    = [&](int i) { const ImVec2 p = topLeftOf(i); return ImVec2{ p.x + size.x, p.y + half.y }; };

        // An arrow's two ends on screen. When the reverse arrow exists too, both
        // are nudged sideways so the pair reads as two arrows, not one line.
        const auto arrowEnds = [&](const StateTransition& t, ImVec2& from, ImVec2& to) {
            const ImVec2 a = centreOf(t.m_from), b = centreOf(t.m_to);
            float dx = b.x - a.x, dy = b.y - a.y;
            const float length = std::max(1e-4f, std::sqrt(dx * dx + dy * dy));
            dx /= length; dy /= length;
            ImVec2 offset{ 0.0f, 0.0f };
            const bool reverse = std::any_of(graph.m_transitions.begin(), graph.m_transitions.end(),
                [&](const StateTransition& o) { return o.m_from == t.m_to && o.m_to == t.m_from; });
            if (reverse) offset = { -dy * 7.0f * m_canvas.Zoom(), dx * 7.0f * m_canvas.Zoom() };
            from = EdgePoint({ a.x + offset.x, a.y + offset.y }, half, dx, dy);
            to   = EdgePoint({ b.x + offset.x, b.y + offset.y }, half, -dx, -dy);
        };

        // 1. What's under the mouse: a state's arrow dot, a state, or an arrow.
        const ImVec2 mouse     = m_canvas.Mouse();
        const float  portReach = std::max(8.0f, 10.0f * m_canvas.Zoom());
        int hoveredState = -1, hoveredPort = -1, hoveredArrow = -1;
        for (int i = 0; i < stateCount; ++i) {
            const ImVec2 p = topLeftOf(i);
            if (mouse.x >= p.x && mouse.y >= p.y && mouse.x <= p.x + size.x && mouse.y <= p.y + size.y) hoveredState = i;
            const ImVec2 port = portOf(i);
            if ((mouse.x - port.x) * (mouse.x - port.x) + (mouse.y - port.y) * (mouse.y - port.y) <= portReach * portReach) hoveredPort = i;
        }
        if (hoveredState < 0) {
            for (int i = 0; i < static_cast<int>(graph.m_transitions.size()); ++i) {
                const StateTransition& t = graph.m_transitions[static_cast<std::size_t>(i)];
                if (t.m_from < 0 || t.m_to < 0 || t.m_from >= stateCount || t.m_to >= stateCount) continue;
                ImVec2 from, to;
                arrowEnds(t, from, to);
                if (DistanceToSegment(mouse, from, to) < 6.0f) hoveredArrow = i;
            }
        }
        // A drop lands on the state under the mouse. Read it before the line
        // below: while an asset is being dragged, the canvas doesn't count as
        // hovered (the Asset Browser's item is the active one).
        if (!dropped.empty()) {
            result.droppedPath  = std::move(dropped);
            result.dropState    = hoveredState;
            result.dropPosition = m_canvas.MouseGraph() - kStateSize * 0.5f;   // a new state centred on the mouse
        }
        if (!m_canvas.Hovered()) { hoveredState = hoveredPort = hoveredArrow = -1; }

        // 2. The mouse, turned into edits.
        const ImGuiIO& io = ImGui::GetIO();
        if (m_canvas.LeftClicked()) {
            if (hoveredPort >= 0 && !readOnly) { m_connectFrom = hoveredPort; SelectState(hoveredPort); }
            else if (hoveredState >= 0 && io.KeyShift) {
                // Shift+click: add it to the selection, or take it back out.
                // Adding also starts a drag, so Shift+click-and-drag moves the group.
                m_transition = -1;
                const auto it = std::find(m_states.begin(), m_states.end(), hoveredState);
                if (it != m_states.end()) m_states.erase(it);
                else { m_states.push_back(hoveredState); m_dragging = !readOnly; m_moved = false; }
            }
            else if (hoveredState >= 0) {
                // A plain click on a state already selected keeps the
                // selection, so the drag that follows moves the whole group.
                if (!IsSelected(hoveredState)) SelectState(hoveredState);
                m_dragging = !readOnly;
                m_moved    = false;
            }
            else if (hoveredArrow >= 0) { SelectTransition(hoveredArrow); }
            else if (!io.KeyShift)      { ClearSelection(); }   // Shift+click on empty space keeps the selection
        }
        if (hoveredState >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) result.openState = hoveredState;

        // Dragging moves every selected state by the same amount.
        if (m_dragging && m_canvas.LeftHeld()) {
            const ImVec2 delta = io.MouseDelta;
            if (delta.x != 0.0f || delta.y != 0.0f) {
                for (int i : m_states) {
                    if (i < 0 || i >= stateCount) continue;
                    graph.m_states[static_cast<std::size_t>(i)].m_editorPosition += Math::vec2(delta.x, delta.y) / m_canvas.Zoom();
                }
                m_moved = true;
            }
        }

        if (m_canvas.LeftReleased()) {
            if (m_dragging) { m_dragging = false; if (m_moved) result.changed = true; }
            if (m_connectFrom >= 0) {
                if (hoveredState >= 0 && hoveredState != m_connectFrom) {
                    const int added = StateGraphEdit::AddTransition(graph, m_connectFrom, hoveredState);
                    if (added >= 0) { SelectTransition(added); result.changed = true; }
                }
                m_connectFrom = -1;
            }
        }

        if (m_canvas.RightClicked() && !readOnly) {
            m_menuState      = hoveredState;
            m_menuTransition = hoveredState < 0 ? hoveredArrow : -1;
            m_menuPosition   = m_canvas.MouseGraph();
            // Right-clicking a state outside the selection selects just it;
            // inside, the selection stays, so the menu acts on the group.
            if (m_menuState >= 0) { if (!IsSelected(m_menuState)) SelectState(m_menuState); }
            else if (m_menuTransition >= 0) SelectTransition(m_menuTransition);
            ImGui::OpenPopup(m_menuState >= 0 ? "StateMenu" : m_menuTransition >= 0 ? "ArrowMenu" : "StateCanvasMenu");
        }

        // Keys - claimed with Shortcut, so the Hierarchy's global ones stay out of it.
        if (!readOnly && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
            if (ImGui::Shortcut(ImGuiKey_Delete)) {
                if (!m_states.empty())      { DeleteSelection(graph); result.changed = true; }
                else if (m_transition >= 0) { StateGraphEdit::DeleteTransition(graph, m_transition); ClearSelection(); result.changed = true; }
            }
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C) && !m_states.empty()) CopySelection(graph);
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V) && !m_clipboard.Empty()) {
                // At the mouse if it's over the canvas; else just beside the originals.
                Paste(graph, m_canvas.Hovered() ? m_canvas.MouseGraph() : StateGraphEdit::TopLeftOf(m_clipboard) + Math::vec2(30.0f));
                result.changed = true;
            }
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D) && !m_states.empty()) { DuplicateSelection(graph); result.changed = true; }
        }

        // 3. Draw: arrows first (under the boxes), each with its head halfway along.
        const int drawCount = static_cast<int>(graph.m_states.size());   // an edit above may have changed it
        for (int i = 0; i < static_cast<int>(graph.m_transitions.size()); ++i) {
            const StateTransition& t = graph.m_transitions[static_cast<std::size_t>(i)];
            if (t.m_from < 0 || t.m_to < 0 || t.m_from >= drawCount || t.m_to >= drawCount) continue;

            ImVec2 from, to;
            arrowEnds(t, from, to);
            const bool  selected = i == m_transition;
            const ImU32 colour   = selected ? IM_COL32(255, 215, 120, 255) : i == hoveredArrow ? IM_COL32(230, 235, 245, 255) : IM_COL32(170, 178, 192, 255);
            m_canvas.DrawList()->AddLine(from, to, colour, selected ? 3.0f : 2.0f);

            // The head at the middle (Unity Animator style): easy to see which way it goes.
            float dx = to.x - from.x, dy = to.y - from.y;
            const float length = std::max(1e-4f, std::sqrt(dx * dx + dy * dy));
            dx /= length; dy /= length;
            const ImVec2 mid{ (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f };
            const float  head = 8.0f * m_canvas.Zoom();
            m_canvas.DrawList()->AddTriangleFilled({ mid.x + dx * head, mid.y + dy * head },
                                                   { mid.x - dx * head - dy * head * 0.7f, mid.y - dy * head + dx * head * 0.7f },
                                                   { mid.x - dx * head + dy * head * 0.7f, mid.y - dy * head - dx * head * 0.7f }, colour);
            if (t.m_conditions.empty()) m_canvas.DrawLabel({ mid.x + 8.0f, mid.y - 8.0f }, IM_COL32(255, 180, 120, 255), "!", 0.9f);
        }

        // The arrow being drawn follows the mouse.
        if (m_connectFrom >= 0 && m_connectFrom < drawCount) m_canvas.DrawList()->AddLine(portOf(m_connectFrom), mouse, IM_COL32(255, 215, 110, 255), 2.0f);

        for (int i = 0; i < drawCount; ++i) {
            const GraphState& state = graph.m_states[static_cast<std::size_t>(i)];
            const bool entry = i == graph.m_entryState;
            std::string subtitle;
            if (const char* label = ContentLabel(state)) subtitle = label;
            else subtitle = std::string("Node: ") + std::string(state.m_behavior.Root()->Description().name);
            if (entry) subtitle += "  (entry)";

            m_canvas.DrawNode(topLeftOf(i), size, entry ? IM_COL32(40, 150, 90, 255) : IM_COL32(60, 95, 150, 255),
                              state.m_name.c_str(), subtitle.c_str(), IsSelected(i), false);
            if (!readOnly) m_canvas.DrawPort(portOf(i), i == hoveredPort);
        }

        m_canvas.End();

        if (hoveredArrow >= 0 && graph.m_transitions[static_cast<std::size_t>(hoveredArrow)].m_conditions.empty()) {
            ImGui::SetTooltip("No conditions: switches the moment the enemy enters this state.");
        }

        if (!readOnly && DrawMenus(graph)) result.changed = true;
        return result;

    }   // StateGraphView::Draw()

    bool StateGraphView::DrawMenus(StateGraph& graph) {

        bool changed = false;
        const int stateCount = static_cast<int>(graph.m_states.size());

        if (ImGui::BeginPopup("StateCanvasMenu")) {
            if (ImGui::MenuItem("Add State")) {
                SelectState(StateGraphEdit::AddState(graph, "New State", m_menuPosition));
                if (stateCount == 0) graph.m_entryState = 0;   // the first state is the entry
                changed = true;
            }
            if (ImGui::MenuItem(m_clipboard.states.size() > 1 ? "Paste States" : "Paste State", "Ctrl+V", false, !m_clipboard.Empty())) {
                Paste(graph, m_menuPosition);
                changed = true;
            }
            if (ImGui::MenuItem("Frame All")) FrameAll(graph);
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("StateMenu")) {
            const int index = m_menuState;
            if (index >= 0 && index < stateCount) {
                const bool group = m_states.size() > 1;   // Duplicate / Copy / Delete act on all of them

                // Entry and Inspect Script are about one state: the one clicked.
                if (ImGui::MenuItem("Set as Entry", nullptr, false, graph.m_entryState != index)) { graph.m_entryState = index; changed = true; }
                ImGui::SetItemTooltip("The state an enemy starts in.");
                const GraphState& state = graph.m_states[static_cast<std::size_t>(index)];
                if (StateGraphEdit::ContentOf(state) == StateGraphEdit::Content::SingleNode) {
                    if (ImGui::MenuItem("Inspect Script")) InspectScript::OpenNodeSource(*state.m_behavior.Root());
                    ImGui::SetItemTooltip("Open the file this node is written in.");
                }
                ImGui::Separator();
                if (ImGui::MenuItem(group ? "Duplicate States" : "Duplicate", "Ctrl+D")) { DuplicateSelection(graph); changed = true; }
                if (ImGui::MenuItem(group ? "Copy States" : "Copy", "Ctrl+C")) CopySelection(graph);
                ImGui::Separator();
                if (ImGui::MenuItem(group ? "Delete States" : "Delete", "Del")) { DeleteSelection(graph); changed = true; }
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("ArrowMenu")) {
            const int index = m_menuTransition;
            if (index >= 0 && index < static_cast<int>(graph.m_transitions.size())) {
                if (ImGui::MenuItem("Check Earlier")) { SelectTransition(StateGraphEdit::MovePriority(graph, index, -1)); changed = true; }
                ImGui::SetItemTooltip("Arrows out of the same state are checked in order; the first that passes wins.");
                if (ImGui::MenuItem("Check Later"))   { SelectTransition(StateGraphEdit::MovePriority(graph, index, +1)); changed = true; }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del")) { StateGraphEdit::DeleteTransition(graph, index); ClearSelection(); changed = true; }
            }
            ImGui::EndPopup();
        }

        return changed;

    }   // StateGraphView::DrawMenus()

    bool StateGraphView::IsSelected(int state) const {

        return std::find(m_states.begin(), m_states.end(), state) != m_states.end();

    }   // StateGraphView::IsSelected()

    void StateGraphView::CopySelection(const StateGraph& graph) {

        m_clipboard = StateGraphEdit::CopyStates(graph, m_states);

    }   // StateGraphView::CopySelection()

    void StateGraphView::Paste(StateGraph& graph, const Math::vec2& topLeft) {

        const bool wasEmpty = graph.m_states.empty();
        m_states = StateGraphEdit::PasteStates(graph, m_clipboard, topLeft);   // the copies, ready to drag into place
        m_transition = -1;
        if (wasEmpty && !graph.m_states.empty()) graph.m_entryState = 0;   // a graph with states needs an entry

    }   // StateGraphView::Paste()

    void StateGraphView::DuplicateSelection(StateGraph& graph) {

        // Copy and paste in one, 30 units down-right, without touching the clipboard.
        const StateGraphEdit::StateClipboard copies = StateGraphEdit::CopyStates(graph, m_states);
        m_states = StateGraphEdit::PasteStates(graph, copies, StateGraphEdit::TopLeftOf(copies) + Math::vec2(30.0f));
        m_transition = -1;

    }   // StateGraphView::DuplicateSelection()

    void StateGraphView::DeleteSelection(StateGraph& graph) {

        StateGraphEdit::DeleteStates(graph, m_states);
        ClearSelection();

    }   // StateGraphView::DeleteSelection()

    void StateGraphView::ClearSelection() {

        m_states.clear();
        m_transition = m_connectFrom = -1;
        m_menuState = m_menuTransition = -1;
        m_dragging = false;

    }   // StateGraphView::ClearSelection()

    void StateGraphView::FrameAll(const StateGraph& graph) {

        if (graph.m_states.empty()) return;
        Math::vec2 min( FLT_MAX);
        Math::vec2 max(-FLT_MAX);
        for (const GraphState& state : graph.m_states) {
            min = glm::min(min, state.m_editorPosition);
            max = glm::max(max, state.m_editorPosition + kStateSize);
        }
        m_canvas.FrameBox(min, max);

    }   // StateGraphView::FrameAll()

}   // NAMESPACE CRUCIBLE
