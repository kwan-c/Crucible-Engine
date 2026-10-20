#include "ImGui/Panels/ImGuiPanel_Animator.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"
#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "ImGui/EditorWidgets.h"
#include "Resources/AnimatorControllerIO.h"
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace Crucible {
    namespace {
        ImVec2 Add(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
        ImVec2 Sub(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }
        ImVec2 Mul(ImVec2 a, float b) { return {a.x * b, a.y * b}; }
        float LengthSq(ImVec2 a) { return a.x * a.x + a.y * a.y; }
        bool Inside(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.y >= a.y && p.x <= b.x && p.y <= b.y; }
        bool EditText(const char* label, std::string& text) {
            std::vector<char> buffer(std::max(size_t(256), text.size() + 128), 0);
            std::copy(text.begin(), text.end(), buffer.begin());
            if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
            text = buffer.data(); return true;
        }
        bool StateCombo(const char* label, uint32_t& state, const AnimatorController& controller, bool allowAny) {
            const char* name = state < controller.states.size() ? controller.states[state].name.c_str() : "Any State";
            bool changed = false;
            if (ImGui::BeginCombo(label, name)) {
                if (allowAny && ImGui::Selectable("Any State", state == InvalidModelIndex)) { state = InvalidModelIndex; changed = true; }
                for (uint32_t i = 0; i < controller.states.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(controller.states[i].name.c_str(), state == i)) { state = i; changed = true; }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }
        const char* parameterTypes[] = {"Float", "Integer", "Bool", "Trigger"};
        const char* conditionModes[] = {"If", "IfNot", "Greater", "Less", "Equals", "NotEqual"};
    }
    void AnimatorPanel::Init() {
        m_panelName = "Animator"; m_windowPadding = {8, 8};
        m_docked = true; m_dockArea = DockArea::Center;
        m_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }
    void AnimatorPanel::ResetSelection() {
        m_state = m_transition = m_parameter = -1; m_anySelected = m_connecting = false; m_dragNode = -2;
    }
    void AnimatorPanel::RequestOpen(uint64_t asset) {
        Show(); if (m_hasDocument && asset == m_asset) return;
        Request(Pending::Open, asset);
    }
    void AnimatorPanel::RequestNew() { Show(); Request(Pending::New); }
    void AnimatorPanel::Request(Pending action, uint64_t asset) {
        m_document.EndEdit(); m_pending = action; m_pendingAsset = asset;
        if (IsUnsaved()) { m_openGuard = true; Show(); } else ApplyPending();
    }
    bool AnimatorPanel::GuardExit(std::function<void()> continuation) {
        if (!IsUnsaved()) return true;
        m_exitContinuation = std::move(continuation); Request(Pending::Exit); return false;
    }
    void AnimatorPanel::ApplyPending() {
        const auto action = m_pending; const auto asset = m_pendingAsset;
        m_pending = Pending::None; m_continueAfterSave = false;
        if (action == Pending::Open || action == Pending::Reload) Load(asset);
        else if (action == Pending::New) { m_document.New(); m_hasDocument = true; m_asset = 0; m_savedText.clear(); m_error.clear(); ResetSelection(); m_pan = {45, 175}; m_zoom = 1; m_fitRequested = true; }
        else if (action == Pending::Close) m_visible = false;
        else if (action == Pending::Exit) { auto callback = std::move(m_exitContinuation); if (callback) callback(); }
    }
    bool AnimatorPanel::Load(uint64_t asset) {
        const auto* entry = m_core.GetAssets().FindByGuid(xresource::instance_guid{asset});
        std::string text;
        if (!entry || entry->type != AssetType::AnimatorController || entry->sizeBytes > MaxAnimatorControllerBytes ||
            !m_core.GetAssets().ReadText(entry->virtualPath, text)) { m_error = "Could not read controller asset"; return false; }
        if (!m_document.Load(text, m_error)) return false;
        m_savedText = std::move(text); m_asset = asset; m_hasDocument = true;
        ResetSelection(); m_pan = {45, 175}; m_zoom = 1.0f; m_fitRequested = true; return true;
    }
    bool AnimatorPanel::ValidateForSave(std::string& text) {
        if (m_core.GetScenes().IsPlaySessionActive()) { m_error = "Stop Play Mode to save a controller"; return false; }
        m_document.EndEdit();
        const auto model = m_document.Data().previewModel ? m_core.GetResources().GetModel(m_document.Data().previewModel) : nullptr;
        if (m_document.Data().previewModel && !model) { m_error = "Preview model is missing; choose another model or None"; return false; }
        if (!ValidateAnimatorController(m_document.Data().controller, m_error, model.get())) return false;
        return m_document.Serialize(text, m_error);
    }
    bool AnimatorPanel::Save() {
        if (!m_hasDocument) return false;
        if (!m_asset) { m_openSaveAs = true; return false; }
        std::string text;
        if (!ValidateForSave(text)) return false;
        const auto* entry = m_core.GetAssets().FindByGuid(xresource::instance_guid{m_asset});
        std::string onDisk;
        if (!entry || entry->sizeBytes > MaxAnimatorControllerBytes || !m_core.GetAssets().ReadText(entry->virtualPath, onDisk)) {
            m_error = "Controller was removed; use Save As"; return false;
        }
        if (onDisk != m_savedText) { m_error = "Controller changed on disk. Reload it or use Save As to preserve both versions."; return false; }
        if (!m_core.GetResources().SaveAnimatorController(m_asset, text, &m_error)) return false;
        m_savedText = text; m_document.MarkSaved();
        return true;
    }
    bool AnimatorPanel::SaveAs(const std::string& path) {
        std::string text; if (!m_hasDocument || !ValidateForSave(text)) return false;
        const auto id = m_core.GetResources().CreateAnimatorControllerAsset(path, text, &m_error);
        if (!id) return false;
        m_asset = id; m_savedText = std::move(text); m_document.MarkSaved();
        if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) browser->Refresh();
        return true;
    }
    void AnimatorPanel::Update(float) {
        if (!m_visible) { m_focused = false; return; }
        bool open = true;
        const bool playing = m_core.GetScenes().IsPlaySessionActive();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, m_windowPadding);
        const bool expanded = ImGui::Begin(m_panelName.c_str(), &open, m_flags | (IsUnsaved() ? ImGuiWindowFlags_UnsavedDocument : 0));
        m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (expanded) {
            m_document.BeginEdit();
            if (m_focused && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
                if (!playing && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) Save();
                if (!playing && !ImGui::GetIO().WantTextInput) {
                    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) { m_document.Undo(); ResetSelection(); }
                    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) { m_document.Redo(); ResetSelection(); }
                    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N)) RequestNew();
                }
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) ImGui::OpenPopup("Choose controller");
            }
            DrawToolbar(playing);
            if (!m_error.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 140, 110, 255)); ImGui::TextWrapped("%s", m_error.c_str()); ImGui::PopStyleColor(); }
            if (m_hasDocument) {
                const float fullWidth = ImGui::GetContentRegionAvail().x;
                const float left = fullWidth > 780 ? 185.0f : 150.0f;
                const float right = fullWidth > 780 ? 285.0f : 230.0f;
                ImGui::BeginChild("Parameters", {left, 0}, ImGuiChildFlags_Borders); DrawParameters(playing); ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("Graph", {std::max(90.0f, fullWidth - left - right - ImGui::GetStyle().ItemSpacing.x * 2), 0}, ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse); DrawCanvas(playing); ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("Properties", {0, 0}, ImGuiChildFlags_Borders); DrawDetails(playing); DrawLive(); ImGui::EndChild();
            } else ImGui::TextWrapped("Create a controller, open one from the picker, double-click an .animcontroller in Assets, or choose Use Selected Entity.");
            m_document.EndEdit(ImGui::IsAnyItemActive() || m_dragNode != -2);
        }
        DrawDialogs(playing);
        ImGui::End(); ImGui::PopStyleVar();
        if (!open) Request(Pending::Close);
    }
    void AnimatorPanel::DrawToolbar(bool playing) {
        ImGui::BeginDisabled(playing);
        if (ImGui::Button("New")) RequestNew();
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Open")) ImGui::OpenPopup("Choose controller");
        if (ImGui::BeginPopup("Choose controller")) {
            for (const auto& asset : m_core.GetResources().List(ResourceKind::AnimatorController)) {
                ImGui::PushID(static_cast<int>(asset.id));
                if (ImGui::Selectable(asset.name.c_str(), m_asset == asset.id)) RequestOpen(asset.id);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Use Selected Entity")) {
            auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
            const auto entity = inspector ? inspector->GetSelectedEntity() : kNullEntity;
            const auto* animation = m_core.GetWorld().IsAlive(entity) ? m_core.GetWorld().Read<AnimatedModel>(entity) : nullptr;
            if (animation && animation->controllerAsset) RequestOpen(animation->controllerAsset);
            else m_error = "Select an entity with an assigned Animator Controller";
        }
        ImGui::BeginDisabled(playing || !m_hasDocument);
        if (ImGui::Button("Save")) Save(); ImGui::SameLine();
        if (ImGui::Button("Save As")) { m_openSaveAs = true; m_continueAfterSave = false; }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_asset);
        if (ImGui::Button("Reload")) Request(Pending::Reload, m_asset);
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(!m_document.CanUndo()); if (ImGui::Button("Undo")) { m_document.Undo(); ResetSelection(); } ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(!m_document.CanRedo()); if (ImGui::Button("Redo")) { m_document.Redo(); ResetSelection(); } ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (m_hasDocument) ImGui::TextWrapped("%s%s", m_asset ? m_core.GetResources().GetName(m_asset).c_str() : "New controller", IsUnsaved() ? " *" : "");
        if (playing) ImGui::TextDisabled("Play Mode: graph authoring is read-only; live parameters remain available.");
    }
    void AnimatorPanel::DrawParameters(bool readOnly) {
        auto& c = m_document.Data().controller;
        ImGui::SeparatorText("Parameters");
        ImGui::BeginDisabled(readOnly);
        if (ImGui::Button("+ Parameter") && c.parameters.size() < 4096) {
            size_t index = c.parameters.size() + 1; std::string name;
            do { name = "Parameter " + std::to_string(index++); } while (std::any_of(c.parameters.begin(), c.parameters.end(), [&](const auto& p) { return p.name == name; }));
            c.parameters.push_back({name}); m_parameter = static_cast<int>(c.parameters.size() - 1);
        }
        for (int i = 0; i < static_cast<int>(c.parameters.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(c.parameters[i].name.c_str(), m_parameter == i)) m_parameter = i;
            ImGui::PopID();
        }
        if (m_parameter >= 0 && m_parameter < static_cast<int>(c.parameters.size())) {
            auto& p = c.parameters[m_parameter];
            ImGui::Separator(); ImGui::SetNextItemWidth(-1); EditText("##ParameterName", p.name);
            int type = static_cast<int>(p.type);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##Type", &type, parameterTypes, 4)) m_document.ChangeParameterType(static_cast<uint32_t>(m_parameter), static_cast<AnimatorParameterType>(type));
            ImGui::SetNextItemWidth(-1);
            if (p.type == AnimatorParameterType::Float) ImGui::DragFloat("##DefaultFloat", &p.initial.number, .01f);
            else if (p.type == AnimatorParameterType::Integer) ImGui::InputInt("##DefaultInteger", &p.initial.integer);
            else if (p.type == AnimatorParameterType::Bool) ImGui::Checkbox("Default", &p.initial.boolean);
            else ImGui::TextDisabled("Trigger starts unset");
            ImGui::TextDisabled("Authored default");
            ImGui::TextWrapped("Deleting a parameter also removes conditions that reference it. Undo restores both.");
            if (ImGui::Button("Delete parameter")) { m_document.RemoveParameter(static_cast<uint32_t>(m_parameter)); m_parameter = -1; }
        }
        ImGui::EndDisabled();
    }
    void AnimatorPanel::DeleteSelection() {
        auto& c = m_document.Data().controller;
        if (m_transition >= 0 && m_transition < static_cast<int>(c.transitions.size())) c.transitions.erase(c.transitions.begin() + m_transition);
        else if (m_state >= 0) m_document.RemoveState(static_cast<uint32_t>(m_state));
        m_state = m_transition = -1; m_anySelected = m_connecting = false;
    }
    void AnimatorPanel::DrawCanvas(bool readOnly) {
        auto& data = m_document.Data(); auto& c = data.controller;
        ImGui::BeginDisabled(readOnly);
        if (ImGui::Button("+ State")) { m_state = static_cast<int>(m_document.AddState({-m_pan.x / m_zoom + 40, -m_pan.y / m_zoom + 50})); m_transition = -1; m_anySelected = false; }
        ImGui::EndDisabled(); ImGui::SameLine();
        bool fit = ImGui::Button("Fit");
        if (m_fitRequested) {
            m_fitRequested = false; const auto space = ImGui::GetContentRegionAvail();
            const auto outside = [&](AnimatorGraphPoint p) { return m_pan.x + p.x * m_zoom < 0 || m_pan.y + p.y * m_zoom < 0 || m_pan.x + (p.x + 200) * m_zoom > space.x || m_pan.y + (p.y + 85) * m_zoom > space.y - 50; };
            fit = fit || outside(data.anyState) || std::any_of(data.positions.begin(), data.positions.end(), outside);
        }
        if (fit) {
            float minX = data.anyState.x, minY = data.anyState.y, maxX = minX + 200, maxY = minY + 85;
            for (auto p : data.positions) { minX = std::min(minX, p.x); minY = std::min(minY, p.y); maxX = std::max(maxX, p.x + 200); maxY = std::max(maxY, p.y + 85); }
            const auto size = ImGui::GetContentRegionAvail();
            m_zoom = std::clamp(std::min((size.x - 30) / (maxX - minX), (size.y - 50) / (maxY - minY)), .25f, 1.5f);
            m_pan = {15 - minX * m_zoom, 15 - minY * m_zoom};
        }
        ImGui::TextWrapped(m_connecting ? "Choose a destination. Esc cancels." : "Drag nodes. Connect from the right dot. Middle drag pans; wheel zooms.");
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const auto avail = ImGui::GetContentRegionAvail(); const ImVec2 size{std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)};
        ImGui::InvisibleButton("Canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        const bool hover = ImGui::IsItemHovered();
        auto& io = ImGui::GetIO(); const ImVec2 mouse = io.MousePos;
        if (hover && io.MouseWheel != 0) {
            const float zoom = std::clamp(m_zoom * std::pow(1.15f, io.MouseWheel), .25f, 2.0f);
            m_pan = Sub(Sub(mouse, origin), Mul(Sub(Sub(mouse, origin), m_pan), zoom / m_zoom)); m_zoom = zoom;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) m_pan = Add(m_pan, io.MouseDelta);
        const auto screen = [&](AnimatorGraphPoint p) { return Add(origin, Add(m_pan, {p.x * m_zoom, p.y * m_zoom})); };
        const auto point = [&](int node) { return node < 0 ? data.anyState : data.positions[node]; };
        const ImVec2 nodeSize{200 * m_zoom, 85 * m_zoom};
        int hovered = -2; bool port = false;
        for (int i = -1; i < static_cast<int>(data.positions.size()); ++i) {
            const auto p = screen(point(i));
            if (Inside(mouse, p, Add(p, nodeSize))) { hovered = i; port = false; }
            const auto dot = Add(p, {nodeSize.x, nodeSize.y * .5f});
            if (LengthSq(Sub(mouse, dot)) < std::pow(std::max(8.0f, 9 * m_zoom), 2)) { hovered = i; port = true; }
        }
        auto* draw = ImGui::GetWindowDrawList(); const auto end = Add(origin, size);
        draw->PushClipRect(origin, end, true); draw->AddRectFilled(origin, end, IM_COL32(26, 29, 35, 255));
        const float grid = 40 * m_zoom;
        for (float x = std::fmod(m_pan.x, grid); x < size.x; x += grid) draw->AddLine({origin.x + x, origin.y}, {origin.x + x, end.y}, IM_COL32(42, 45, 53, 255));
        for (float y = std::fmod(m_pan.y, grid); y < size.y; y += grid) draw->AddLine({origin.x, origin.y + y}, {end.x, origin.y + y}, IM_COL32(42, 45, 53, 255));
        struct EdgeLabel { ImVec2 position; int index; ImU32 colour; };
        std::vector<EdgeLabel> edgeLabels;
        std::unordered_map<uint64_t, std::pair<int, int>> edgeLanes;
        edgeLanes.reserve(c.transitions.size());
        const auto edgeKey = [](const auto& t) { return (uint64_t(t.from) << 32) | t.to; };
        for (const auto& t : c.transitions) ++edgeLanes[edgeKey(t)].first;
        int edgeHover = -1; float closest = 8.0f * 8;
        for (int i = 0; i < static_cast<int>(c.transitions.size()); ++i) {
            const auto& t = c.transitions[i]; if (t.to >= data.positions.size() || (t.from != InvalidModelIndex && t.from >= data.positions.size())) continue;
            const auto start = screen(t.from == InvalidModelIndex ? data.anyState : data.positions[t.from]); const auto finish = screen(data.positions[t.to]);
            ImVec2 a = Add(start, {nodeSize.x, nodeSize.y * .5f}), b = Add(finish, {0, nodeSize.y * .5f});
            auto& lanes = edgeLanes[edgeKey(t)]; const int siblings = lanes.first, ordinal = lanes.second++;
            const float lane = (float(ordinal) - float(siblings - 1) * .5f) * 48 * m_zoom;
            const float bend = std::max(10 * m_zoom, std::abs(b.x - a.x) * .45f);
            ImVec2 p = Add(a, {bend, 0}), q = Add(b, {-bend, 0});
            p.y += lane; q.y += lane;
            if (b.x < a.x) {
                a = Add(start, {nodeSize.x * .5f, nodeSize.y}); b = Add(finish, {nodeSize.x * .5f, nodeSize.y});
                const float routeY = std::max(a.y, b.y) + (80 + ordinal * 35) * m_zoom;
                p = {a.x, routeY}; q = {b.x, routeY};
            }
            if (t.from == t.to) { a = Add(start, {nodeSize.x, nodeSize.y * .25f}); b = Add(start, {nodeSize.x, nodeSize.y * .75f}); p = Add(a, {(90 + ordinal * 35) * m_zoom, -50 * m_zoom}); q = Add(b, {(90 + ordinal * 35) * m_zoom, 50 * m_zoom}); }
            if (std::min({a.x, b.x, p.x, q.x}) > end.x || std::max({a.x, b.x, p.x, q.x}) < origin.x ||
                std::min({a.y, b.y, p.y, q.y}) > end.y || std::max({a.y, b.y, p.y, q.y}) < origin.y) continue;
            const auto curve = [&](float u) { float v = 1 - u; return Add(Add(Mul(a, v*v*v), Mul(p, 3*v*v*u)), Add(Mul(q, 3*v*u*u), Mul(b, u*u*u))); };
            const auto colour = i == m_transition ? IM_COL32(255, 200, 90, 255) : IM_COL32(140, 159, 182, 255);
            draw->AddBezierCubic(a, p, q, b, colour, i == m_transition ? 3.0f : 1.7f);
            const auto mid = curve(.5f); auto direction = Sub(curve(.53f), curve(.47f)); const float length = std::sqrt(LengthSq(direction));
            if (length > 0) { direction = Mul(direction, 1 / length); const ImVec2 normal{-direction.y, direction.x}; draw->AddTriangleFilled(Add(mid, Mul(direction, 7)), Add(Sub(mid, Mul(direction, 6)), Mul(normal, 5)), Sub(Sub(mid, Mul(direction, 6)), Mul(normal, 5)), colour); }
            if (m_zoom >= .65f) edgeLabels.push_back({Add(mid, {0, 10}), i, colour});
            if (hover && hovered == -2) {
                auto previous = a;
                for (int sample = 1; sample <= 40; ++sample) {
                    const auto at = curve(float(sample) / 40); const auto segment = Sub(at, previous); const auto relative = Sub(mouse, previous);
                    const float lengthSq = LengthSq(segment);
                    const float along = lengthSq > 0 ? std::clamp((relative.x * segment.x + relative.y * segment.y) / lengthSq, 0.0f, 1.0f) : 0.0f;
                    const float distance = LengthSq(Sub(mouse, Add(previous, Mul(segment, along))));
                    if (distance < closest) { edgeHover = i; closest = distance; }
                    previous = at;
                }
            }
        }
        const uint32_t current = LiveCurrent(), next = LiveNext();
        for (int i = -1; i < static_cast<int>(data.positions.size()); ++i) {
            const auto p = screen(point(i)); const auto bottom = Add(p, nodeSize);
            if (p.x > end.x || bottom.x < origin.x || p.y > end.y || bottom.y < origin.y) continue;
            const bool selected = i == -1 ? m_anySelected : m_state == i;
            const bool active = i >= 0 && current == static_cast<uint32_t>(i), destination = i >= 0 && next == static_cast<uint32_t>(i);
            ImU32 fill = i < 0 ? IM_COL32(68, 54, 100, 255) : static_cast<uint32_t>(i) == c.entryState ? IM_COL32(112, 79, 34, 255) : IM_COL32(53, 62, 78, 255);
            if (active) fill = IM_COL32(35, 100, 66, 255); else if (destination) fill = IM_COL32(32, 86, 115, 255);
            draw->AddRectFilled(p, bottom, fill, 6); draw->AddRect(p, bottom, selected ? IM_COL32(255, 215, 120, 255) : IM_COL32(126, 141, 162, 255), 6, 0, selected ? 3.0f : 1.2f);
            draw->PushClipRect(Add(p, {7, 3}), Sub(bottom, {6, 3}), true);
            const char* name = i < 0 ? "Any State" : c.states[i].name.c_str();
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * m_zoom, Add(p, {10 * m_zoom, 9 * m_zoom}), IM_COL32(245, 246, 249, 255), name);
            std::string info = i < 0 ? "Source only" : c.states[i].clip == InvalidModelIndex ? "Rest pose" : "Clip " + std::to_string(c.states[i].clip);
            if (i >= 0 && static_cast<uint32_t>(i) == c.entryState) info += " | Entry";
            if (active) info += " | Current"; if (destination) info += " | Next";
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * .85f * m_zoom, Add(p, {10 * m_zoom, 43 * m_zoom}), IM_COL32(210, 215, 224, 255), info.c_str());
            draw->PopClipRect(); draw->AddCircleFilled(Add(p, {nodeSize.x, nodeSize.y * .5f}), std::max(4.0f, 6 * m_zoom), IM_COL32(240, 208, 125, 255));
        }
        for (const auto& label : edgeLabels) {
            const auto text = "#" + std::to_string(label.index); const auto textSize = ImGui::CalcTextSize(text.c_str());
            const auto p = Sub(label.position, Mul(textSize, .5f));
            draw->AddRectFilled(Sub(p, {2, 1}), Add(Add(p, textSize), {2, 1}), IM_COL32(26, 29, 35, 245), 2);
            draw->AddText(p, label.colour, text.c_str());
            if (hover && hovered == -2 && Inside(mouse, Sub(p, {3, 3}), Add(Add(p, textSize), {3, 3}))) { edgeHover = label.index; closest = 0; }
        }
        if (m_connecting && (m_connectFrom == InvalidModelIndex || m_connectFrom < data.positions.size())) {
            auto start = Add(screen(m_connectFrom == InvalidModelIndex ? data.anyState : data.positions[m_connectFrom]), {nodeSize.x, nodeSize.y * .5f});
            draw->AddLine(start, mouse, IM_COL32(255, 215, 110, 255), 2);
        }
        draw->PopClipRect();
        if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!readOnly && port) { m_connecting = true; m_connectFrom = hovered < 0 ? InvalidModelIndex : static_cast<uint32_t>(hovered); }
            else if (!m_connecting) {
                m_state = hovered >= 0 ? hovered : -1; m_anySelected = hovered == -1; m_transition = hovered == -2 ? edgeHover : -1;
                if (!readOnly && hovered >= -1) m_dragNode = hovered;
            }
        }
        if (m_dragNode != -2 && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !readOnly) {
            auto& p = m_dragNode < 0 ? data.anyState : data.positions[m_dragNode];
            p.x = std::clamp(p.x + io.MouseDelta.x / m_zoom, -1e6f, 1e6f); p.y = std::clamp(p.y + io.MouseDelta.y / m_zoom, -1e6f, 1e6f);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_dragNode = -2;
            if (m_connecting && hover && hovered >= 0 && !readOnly) { m_transition = static_cast<int>(m_document.AddTransition(m_connectFrom, static_cast<uint32_t>(hovered))); m_state = -1; m_anySelected = false; m_connecting = false; }
            else if (m_connecting && hover && !port) m_connecting = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_connecting = false; m_dragNode = -2; }
        if (!readOnly && ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelection();
        if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (hovered != -2) { m_state = hovered >= 0 ? hovered : -1; m_anySelected = hovered == -1; m_transition = -1; }
            else { m_state = -1; m_anySelected = false; m_transition = edgeHover; }
            m_contextPosition = {(mouse.x - origin.x - m_pan.x) / m_zoom, (mouse.y - origin.y - m_pan.y) / m_zoom};
            ImGui::OpenPopup("Graph actions");
        }
        if (ImGui::BeginPopup("Graph actions")) {
            ImGui::BeginDisabled(readOnly);
            if (ImGui::MenuItem("Add state")) { m_state = static_cast<int>(m_document.AddState(m_contextPosition)); m_transition = -1; m_anySelected = false; }
            if (m_state >= 0 || m_anySelected) {
                if (ImGui::MenuItem("Create transition")) { m_connectFrom = m_anySelected ? InvalidModelIndex : static_cast<uint32_t>(m_state); m_connecting = true; }
            }
            if (m_state >= 0) {
                if (ImGui::MenuItem("Set as entry")) c.entryState = static_cast<uint32_t>(m_state);
                if (ImGui::MenuItem("Duplicate state")) m_state = static_cast<int>(m_document.DuplicateState(static_cast<uint32_t>(m_state)));
            }
            if (ImGui::MenuItem("Delete selected", "Delete", false, m_transition >= 0 || (m_state >= 0 && c.states.size() > 1))) DeleteSelection();
            ImGui::EndDisabled(); ImGui::EndPopup();
        }
    }
    void AnimatorPanel::DrawDetails(bool readOnly) {
        auto& data = m_document.Data(); auto& c = data.controller;
        ImGui::BeginDisabled(readOnly);
        ImGui::SeparatorText("Preview model");
        auto& resources = m_core.GetResources();
        if (ImGui::BeginCombo("##PreviewModel", data.previewModel ? resources.GetName(data.previewModel).c_str() : "None (clip indices)")) {
            if (ImGui::Selectable("None", !data.previewModel)) data.previewModel = 0;
            for (const auto& model : resources.List(ResourceKind::Model)) {
                ImGui::PushID(static_cast<int>(model.id)); if (ImGui::Selectable(model.name.c_str(), data.previewModel == model.id)) data.previewModel = model.id; ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (m_state >= 0 && m_state < static_cast<int>(c.states.size())) {
            auto& s = c.states[m_state]; ImGui::SeparatorText("State");
            EditText("Name", s.name);
            auto model = data.previewModel ? resources.GetModel(data.previewModel) : nullptr;
            if (model) {
                const auto clipName = [&](uint32_t index) { return index == InvalidModelIndex ? std::string("Rest pose") : index < model->animations.size() ? std::to_string(index) + ": " + model->animations[index].name : std::string("Invalid clip index"); };
                if (ImGui::BeginCombo("Clip", clipName(s.clip).c_str())) {
                    if (ImGui::Selectable("Rest pose", s.clip == InvalidModelIndex)) s.clip = InvalidModelIndex;
                    for (uint32_t i = 0; i < model->animations.size(); ++i) { ImGui::PushID(static_cast<int>(i)); if (ImGui::Selectable(clipName(i).c_str(), s.clip == i)) s.clip = i; ImGui::PopID(); }
                    ImGui::EndCombo();
                }
            } else { int clip = s.clip == InvalidModelIndex ? -1 : static_cast<int>(s.clip); if (ImGui::InputInt("Clip (-1 rest)", &clip)) s.clip = clip < 0 ? InvalidModelIndex : static_cast<uint32_t>(clip); }
            ImGui::DragFloat("Speed", &s.speed, .01f); ImGui::Checkbox("Loop", &s.loop);
            if (ImGui::Button("Set entry")) c.entryState = static_cast<uint32_t>(m_state);
            if (ImGui::Button("Create transition")) { m_connectFrom = static_cast<uint32_t>(m_state); m_connecting = true; }
            if (ImGui::Button("Duplicate state")) m_state = static_cast<int>(m_document.DuplicateState(static_cast<uint32_t>(m_state)));
            ImGui::BeginDisabled(c.states.size() <= 1); if (ImGui::Button("Delete state and links")) DeleteSelection(); ImGui::EndDisabled();
        } else if (m_transition >= 0 && m_transition < static_cast<int>(c.transitions.size())) {
            auto& t = c.transitions[m_transition]; ImGui::SeparatorText("Transition"); ImGui::Text("Priority #%d (lower first)", m_transition);
            StateCombo("From", t.from, c, true); StateCombo("To", t.to, c, false);
            ImGui::DragFloat("Duration (s)", &t.duration, .01f, 0, 120, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Offset", &t.offset, 0, 1); ImGui::Checkbox("Has exit time", &t.hasExitTime);
            if (t.hasExitTime) ImGui::DragFloat("Exit cycles", &t.exitTime, .01f, 0, 10000, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox("Allow self transition", &t.canTransitionToSelf);
            ImGui::SeparatorText("Conditions (AND)");
            int remove = -1;
            for (int i = 0; i < static_cast<int>(t.conditions.size()); ++i) {
                auto& condition = t.conditions[i]; ImGui::PushID(i);
                const char* name = condition.parameter < c.parameters.size() ? c.parameters[condition.parameter].name.c_str() : "Missing parameter";
                if (ImGui::BeginCombo("Parameter", name)) {
                    for (uint32_t p = 0; p < c.parameters.size(); ++p) { ImGui::PushID(static_cast<int>(p)); if (ImGui::Selectable(c.parameters[p].name.c_str(), p == condition.parameter)) {
                        condition.parameter = p; condition.threshold = {};
                        condition.mode = c.parameters[p].type == AnimatorParameterType::Bool || c.parameters[p].type == AnimatorParameterType::Trigger ? AnimatorConditionMode::If : AnimatorConditionMode::Greater;
                    } ImGui::PopID(); }
                    ImGui::EndCombo();
                }
                if (condition.parameter < c.parameters.size()) {
                    const auto type = c.parameters[condition.parameter].type; const bool boolean = type == AnimatorParameterType::Bool || type == AnimatorParameterType::Trigger;
                    int mode = static_cast<int>(condition.mode) - (boolean ? 0 : 2);
                    if (ImGui::Combo("Mode", &mode, conditionModes + (boolean ? 0 : 2), boolean ? (type == AnimatorParameterType::Trigger ? 1 : 2) : 4)) condition.mode = static_cast<AnimatorConditionMode>(mode + (boolean ? 0 : 2));
                    if (type == AnimatorParameterType::Float) ImGui::DragFloat("Value", &condition.threshold.number, .01f);
                    if (type == AnimatorParameterType::Integer) ImGui::InputInt("Value", &condition.threshold.integer);
                }
                if (ImGui::SmallButton("Remove condition")) remove = i;
                ImGui::Separator(); ImGui::PopID();
            }
            if (remove >= 0) t.conditions.erase(t.conditions.begin() + remove);
            ImGui::BeginDisabled(c.parameters.empty() || t.conditions.size() >= 64);
            if (ImGui::Button("+ Condition")) {
                AnimatorCondition condition; condition.parameter = 0;
                condition.mode = c.parameters[0].type == AnimatorParameterType::Bool || c.parameters[0].type == AnimatorParameterType::Trigger ? AnimatorConditionMode::If : AnimatorConditionMode::Greater;
                t.conditions.push_back(condition);
            }
            ImGui::EndDisabled();
            bool up = false, down = false;
            ImGui::BeginDisabled(m_transition == 0); up = ImGui::Button("Earlier"); ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(m_transition + 1 >= static_cast<int>(c.transitions.size())); down = ImGui::Button("Later"); ImGui::EndDisabled();
            if (up) { std::swap(c.transitions[m_transition], c.transitions[m_transition - 1]); --m_transition; }
            if (down) { std::swap(c.transitions[m_transition], c.transitions[m_transition + 1]); ++m_transition; }
            if (ImGui::Button("Delete transition")) DeleteSelection();
        } else if (m_anySelected) ImGui::TextWrapped("Any State is a source for transitions from the current state. It cannot be an entry or destination.");
        else ImGui::TextWrapped("Select a state or arrow to edit its properties. Arrow numbers are transition priority.");
        ImGui::EndDisabled();
        std::string validation;
        if (!ValidateAnimatorController(c, validation)) { ImGui::Separator(); ImGui::TextWrapped("Cannot save: %s", validation.c_str()); }
    }
    uint32_t AnimatorPanel::LiveCurrent() const {
        if (!m_asset || !m_document.RuntimeMatchesSaved()) return InvalidModelIndex;
        auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
        const auto e = inspector ? inspector->GetSelectedEntity() : kNullEntity;
        const auto* a = m_core.GetWorld().IsAlive(e) ? m_core.GetWorld().Read<AnimatedModel>(e) : nullptr;
        if (!a || a->controllerAsset != m_asset || !a->controller.Initialized() || !a->controllerDefinition ||
            !AnimatorDocument::SameRuntime(m_document.Data().controller, *a->controllerDefinition)) return InvalidModelIndex;
        return a->controller.CurrentState();
    }
    uint32_t AnimatorPanel::LiveNext() const {
        if (LiveCurrent() == InvalidModelIndex) return InvalidModelIndex;
        auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
        const auto* a = m_core.GetWorld().Read<AnimatedModel>(inspector->GetSelectedEntity());
        return a ? a->controller.NextState() : InvalidModelIndex;
    }
    void AnimatorPanel::DrawLive() {
        ImGui::SeparatorText("Live instance");
        auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
        const auto e = inspector ? inspector->GetSelectedEntity() : kNullEntity;
        auto* a = m_core.GetWorld().IsAlive(e) ? m_core.GetWorld().Write<AnimatedModel>(e) : nullptr;
        if (!a || a->controllerAsset != m_asset || !a->controller.Initialized() || !a->controllerDefinition) { ImGui::TextWrapped("Select an entity that uses this saved controller to inspect its live state."); return; }
        const auto& definition = *a->controllerDefinition;
        if (!m_document.RuntimeMatchesSaved()) ImGui::TextWrapped("Draft differs from runtime. Save in Edit Mode to apply it. Live values below use the saved definition.");
        const auto current = a->controller.CurrentState(), next = a->controller.NextState();
        if (current < definition.states.size()) ImGui::Text("Current: %s", definition.states[current].name.c_str());
        if (next < definition.states.size()) { ImGui::Text("Next: %s", definition.states[next].name.c_str()); ImGui::ProgressBar(a->controller.TransitionWeight(), {-1, 0}); }
        ImGui::Text("Time %.3fs | progress %.3f", a->controller.Time(), a->controller.NormalizedProgress());
        if (ImGui::Button(a->playing ? "Pause instance" : "Play instance")) a->playing = !a->playing;
        const bool currentRevision = a->controllerDefinition == m_core.GetResources().GetAnimatorController(m_asset);
        ImGui::BeginDisabled(!currentRevision);
        for (uint32_t i = 0; i < definition.parameters.size(); ++i) {
            const auto& p = definition.parameters[i]; const auto* v = a->controller.Parameter(i); if (!v) continue;
            ImGui::PushID(static_cast<int>(i));
            if (p.type == AnimatorParameterType::Float) { float value = v->number; if (ImGui::DragFloat(p.name.c_str(), &value, .01f)) a->controller.SetFloat(p.name, value); }
            else if (p.type == AnimatorParameterType::Integer) { int value = v->integer; if (ImGui::InputInt(p.name.c_str(), &value)) a->controller.SetInteger(p.name, value); }
            else if (p.type == AnimatorParameterType::Bool) { bool value = v->boolean; if (ImGui::Checkbox(p.name.c_str(), &value)) a->controller.SetBool(p.name, value); }
            else { if (ImGui::Button(p.name.c_str())) a->controller.SetTrigger(p.name); if (v->boolean) { ImGui::SameLine(); ImGui::TextDisabled("pending"); } }
            ImGui::PopID();
        }
        ImGui::EndDisabled(); ImGui::TextWrapped("Live values are temporary and are not saved as parameter defaults.");
    }
    void AnimatorPanel::DrawDialogs(bool playing) {
        if (m_openGuard) { ImGui::OpenPopup("Unsaved Animator"); m_openGuard = false; }
        if (ImGui::BeginPopupModal("Unsaved Animator", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save this controller before continuing?");
            if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
            ImGui::BeginDisabled(playing);
            if (ImGui::Button("Save", {100, 0})) {
                if (!m_asset) { m_continueAfterSave = true; m_openSaveAs = true; ImGui::CloseCurrentPopup(); }
                else if (Save()) { ImGui::CloseCurrentPopup(); ApplyPending(); }
            }
            ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button("Discard", {100, 0})) { ImGui::CloseCurrentPopup(); ApplyPending(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {100, 0})) { m_pending = Pending::None; m_exitContinuation = {}; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        if (m_openSaveAs) { ImGui::OpenPopup("Save Animator As"); m_openSaveAs = false; }
        if (ImGui::BeginPopupModal("Save Animator As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("New asset path relative to Assets:");
            ImGui::SetNextItemWidth(420); ImGui::InputText("##Path", m_savePath, sizeof(m_savePath));
            if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
            ImGui::BeginDisabled(playing);
            if (ImGui::Button("Save", {100, 0}) && SaveAs(m_savePath)) { const bool finish = m_continueAfterSave; ImGui::CloseCurrentPopup(); if (finish) ApplyPending(); }
            ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button("Cancel", {100, 0})) { m_pending = Pending::None; m_continueAfterSave = false; m_exitContinuation = {}; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
}
