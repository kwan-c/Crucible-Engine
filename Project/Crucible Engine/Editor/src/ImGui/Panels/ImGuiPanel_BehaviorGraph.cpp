/*************************************************************************
* \file:         ImGuiPanel_BehaviorGraph.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The Behavior Graph window: layout, toolbar, variables,
*                open / save, and when undo steps are recorded.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "ImGuiPanel_BehaviorGraph.h"

// STL
#include <string_view>
#include <vector>

// Crucible engine
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include "Resources/ResourceManager.h"
#include "Scene/SceneManager.h"

// Crucible editor
#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"

namespace Crucible {

    void BehaviorGraphPanel::Init() {

        m_panelName     = "Behavior Graph";
        m_windowPadding = { 8.0f, 8.0f };
        m_docked        = false;    // its own floating window, as Hugo wanted (a real OS window needs ImGui multi-viewport)
        m_flags         = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    }   // BehaviorGraphPanel::Init()

    void BehaviorGraphPanel::Update(float) {

        if (!m_visible) return;

        ImGui::SetNextWindowSize({ 1150.0f, 650.0f }, ImGuiCond_FirstUseEver);
        if (m_makeFocus) { ImGui::SetNextWindowFocus(); m_makeFocus = false; }

        // Read-only during Play: enemies already took their copies when Play
        // started, so edits now would only mislead.
        const bool readOnly = m_core.GetScenes().IsPlaySessionActive();
        const bool unsaved  = m_hasDocument && m_document.Dirty();

        bool open = true;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, m_windowPadding);
        if (ImGui::Begin(m_panelName.c_str(), &open, m_flags | (unsaved ? ImGuiWindowFlags_UnsavedDocument : 0))) {

            // Shortcuts, while this window has focus and no popup or text box is in the way.
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (focused && !readOnly && m_hasDocument && !ImGui::GetIO().WantTextInput
                && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) Save();
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z) && m_document.Undo()) m_view.ClearSelection();
                if ((ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) && m_document.Redo()) m_view.ClearSelection();
            }

            DrawToolbar(readOnly);

            const std::string& status = !m_view.Status().empty() ? m_view.Status() : m_message;
            const bool isError = !m_view.Status().empty() || m_messageIsError;
            if (!status.empty()) ImGui::TextColored(isError ? ImVec4(1.0f, 0.55f, 0.43f, 1.0f) : ImVec4(0.6f, 0.85f, 0.6f, 1.0f), "%s", status.c_str());

            if (m_hasDocument) {

                // Variables | canvas | selected node, like the Animator panel's three columns.
                const float width = ImGui::GetContentRegionAvail().x;
                const float left  = 210.0f;
                const float right = 280.0f;
                const float gap   = ImGui::GetStyle().ItemSpacing.x;

                ImGui::BeginChild("Variables", { left, 0.0f }, ImGuiChildFlags_Borders);
                if (DrawVariables(readOnly)) m_pendingCommit = true;
                ImGui::EndChild();

                ImGui::SameLine();
                ImGui::BeginChild("Canvas", { std::max(100.0f, width - left - right - gap * 2.0f), 0.0f }, ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImGui::TextDisabled("Right-click to add nodes. Drag from a bottom dot onto a node to connect. Shift+click selects several; Alt-drag brings children along. Ctrl+C / Ctrl+V copy and paste. Middle-drag pans; wheel zooms.");
                if (m_view.Draw(m_document.Graph(), readOnly)) m_pendingCommit = true;
                ImGui::EndChild();

                ImGui::SameLine();
                ImGui::BeginChild("Node", { 0.0f, 0.0f }, ImGuiChildFlags_Borders);
                if (m_view.DrawInspector(m_document.Graph(), m_document.Graph().Variables(), readOnly)) m_pendingCommit = true;
                ImGui::EndChild();

                // One undo step per finished gesture: a drag or typing into a
                // field spans many frames, so wait until nothing is held.
                if (m_pendingCommit && !ImGui::IsAnyItemActive() && !m_view.IsBusy()) {
                    m_document.Commit();
                    m_pendingCommit = false;
                }

            } else {
                ImGui::TextWrapped("Click New to start a graph, Open to pick one, or double-click a .behaviorgraph in the Asset Browser.");
            }

            DrawDialogs();

        }
        ImGui::End();
        ImGui::PopStyleVar();

        if (!open) m_visible = false;   // hidden, not closed: the graph and its edits stay

    }   // BehaviorGraphPanel::Update()

    void BehaviorGraphPanel::DrawToolbar(bool readOnly) {

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
        if (ImGui::Button("Undo") && m_document.Undo()) m_view.ClearSelection();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_document.CanRedo());
        if (ImGui::Button("Redo") && m_document.Redo()) m_view.ClearSelection();
        ImGui::EndDisabled();
        ImGui::EndDisabled();   // !m_hasDocument
        ImGui::EndDisabled();   // readOnly

        ImGui::SameLine();
        ImGui::BeginDisabled(!m_hasDocument);
        if (ImGui::Button("Frame All")) m_view.FrameAll(m_document.Graph());
        ImGui::EndDisabled();

        // Which file this is.
        if (m_hasDocument) {
            ImGui::SameLine();
            const AssetEntry* entry = m_asset ? m_core.GetAssets().FindByGuid(xresource::instance_guid{ m_asset }) : nullptr;
            ImGui::TextDisabled("%s%s", entry ? entry->virtualPath.c_str() : "(not saved yet)", m_document.Dirty() ? " *" : "");
        }
        if (readOnly) { ImGui::SameLine(); ImGui::TextDisabled("- read-only during Play"); }

    }   // BehaviorGraphPanel::DrawToolbar()

    bool BehaviorGraphPanel::DrawVariables(bool readOnly) {

        // The list itself is shared with the State Graph window (Graph/VariablesEditor.h).
        std::string message;
        const bool changed = m_variablesEditor.Draw(m_document.Graph().Variables(), readOnly, message);
        if (!message.empty()) SetMessage(message, true);
        return changed;

    }   // BehaviorGraphPanel::DrawVariables()

    void BehaviorGraphPanel::DrawDialogs() {

        // Open: every .behaviorgraph in Assets.
        if (m_openPicker) { ImGui::OpenPopup("Open Behavior Graph"); m_openPicker = false; }
        if (ImGui::BeginPopup("Open Behavior Graph")) {
            bool any = false;
            for (const AssetEntry& entry : m_core.GetAssets().GetAll()) {
                if (entry.type != AssetType::BehaviorGraph) continue;
                any = true;
                if (ImGui::Selectable(entry.virtualPath.c_str())) { Guard(Pending::Open, entry.guid.m_Value); ImGui::CloseCurrentPopup(); }
            }
            if (!any) ImGui::TextDisabled("No .behaviorgraph files in Assets yet - click New, then Save As.");
            ImGui::EndPopup();
        }

        // Save As: a path inside Assets.
        if (m_openSaveAs) { ImGui::OpenPopup("Save Behavior Graph As"); m_openSaveAs = false; }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
        if (ImGui::BeginPopupModal("Save Behavior Graph As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Path inside Assets:");
            ImGui::SetNextItemWidth(420.0f);
            ImGui::InputText("##path", m_savePath, sizeof m_savePath);
            if (ImGui::Button("Save") && SaveAs(m_savePath)) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Unsaved changes: ask before New or Open throws them away.
        if (m_openGuard) { ImGui::OpenPopup("Unsaved Behavior Graph"); m_openGuard = false; }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
        if (ImGui::BeginPopupModal("Unsaved Behavior Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("This graph has unsaved changes. Discard them?");
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

    }   // BehaviorGraphPanel::DrawDialogs()

    void BehaviorGraphPanel::Guard(Pending action, std::uint64_t asset) {

        if (m_hasDocument && m_document.Dirty()) {
            m_pending      = action;
            m_pendingAsset = asset;
            m_openGuard    = true;
            return;
        }
        if (action == Pending::New) NewGraph();
        else if (action == Pending::Open) Load(asset);

    }   // BehaviorGraphPanel::Guard()

    void BehaviorGraphPanel::RequestOpen(std::uint64_t asset) {

        Show();
        if (m_hasDocument && asset == m_asset) return;   // already open
        Guard(Pending::Open, asset);

    }   // BehaviorGraphPanel::RequestOpen()

    void BehaviorGraphPanel::NewGraph() {

        m_document.New();
        m_asset         = 0;
        m_hasDocument   = true;
        m_pendingCommit = false;
        m_view.ClearSelection();
        m_view.FrameAll(m_document.Graph());
        SetMessage("New graph. Right-click the canvas to add nodes under On Start.", false);

    }   // BehaviorGraphPanel::NewGraph()

    bool BehaviorGraphPanel::Load(std::uint64_t asset) {

        const AssetEntry* entry = m_core.GetAssets().FindByGuid(xresource::instance_guid{ asset });
        std::vector<unsigned char> bytes;
        if (!entry || entry->type != AssetType::BehaviorGraph || !m_core.GetAssets().ReadBytes(entry->virtualPath, bytes)) {
            SetMessage("Could not read that behaviour graph", true);
            return false;
        }

        std::string error;
        if (!m_document.Load(bytes, error)) {
            SetMessage(entry->virtualPath + ": " + error, true);
            return false;
        }

        m_asset         = asset;
        m_hasDocument   = true;
        m_pendingCommit = false;
        m_view.ClearSelection();
        m_view.FrameAll(m_document.Graph());
        SetMessage("Opened " + entry->virtualPath, false);
        return true;

    }   // BehaviorGraphPanel::Load()

    bool BehaviorGraphPanel::Save() {

        if (!m_hasDocument) return false;
        if (!m_asset) { m_openSaveAs = true; return false; }    // never saved: ask where

        m_document.Commit();    // whatever was mid-edit belongs in this save
        m_pendingCommit = false;

        const AssetEntry* entry = m_core.GetAssets().FindByGuid(xresource::instance_guid{ m_asset });
        if (!entry) { SetMessage("The file is gone from Assets - use Save As", true); return false; }
        const std::string path = entry->virtualPath;   // copy: saving refreshes the index, which invalidates `entry`

        std::string error;
        if (!m_core.GetResources().SaveTextAsset(path, m_document.ToJson(), false, &error)) { SetMessage(error, true); return false; }

        m_document.MarkSaved();
        SetMessage("Saved " + path, false);
        if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) browser->Refresh();
        return true;

    }   // BehaviorGraphPanel::Save()

    bool BehaviorGraphPanel::SaveAs(std::string path) {

        if (!m_hasDocument) return false;
        constexpr std::string_view kExtension = ".behaviorgraph";
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

    }   // BehaviorGraphPanel::SaveAs()

}   // NAMESPACE CRUCIBLE
