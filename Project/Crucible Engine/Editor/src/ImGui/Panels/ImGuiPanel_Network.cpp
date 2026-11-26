/*!
* @file			ImGuiPanel_Network.cpp
* @author		Abhishek Akilandeswari Patchamuthu
* @par			a.patchamuthu@digipen.edu
* @date			3 November 2026
*
* @brief		The editor's Network panel, defined in ImGuiPanel_Network.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_Network.h"

#include <algorithm>
#include <cstring>

#include "Core.h"
#include "ImGui/EditorTheme.h"
#include "Network/NetworkSession.h"

namespace Crucible {

namespace {
    void CopyTo(std::array<char, 64>& field, const std::string& text) {
        field.fill('\0');
        std::memcpy(field.data(), text.data(), std::min(text.size(), field.size() - 1));
    }

    // An int from an InputInt as a port; 0 (never valid) when out of range, so Validate reports it.
    std::uint16_t ToPort(int value) {
        return value >= 1 && value <= 65535 ? static_cast<std::uint16_t>(value) : 0;
    }
}

void NetworkPanel::Init()
{
    m_panelName = "Network";
    m_flags = ImGuiWindowFlags_None;
    m_docked = false;   // not part of the default layout: a floating window opened from View > Network
    LoadFromSession();
}

void NetworkPanel::LoadFromSession()
{
    m_edit = m_core.GetNetwork().Settings();   // EngineConfig.json's defaults until this panel changes them
    CopyTo(m_listenAddress, m_edit.listenAddress);
    CopyTo(m_connectAddress, m_edit.connectAddress);
    m_hostPort = m_edit.hostPort;
    m_connectPort = m_edit.connectPort;
    m_validationError.clear();
    Validate(m_edit, m_validationError);   // a bad EngineConfig.json value shows up here at once
}

void NetworkPanel::Apply()
{
    m_edit.listenAddress = m_listenAddress.data();
    m_edit.connectAddress = m_connectAddress.data();
    m_edit.hostPort = ToPort(m_hostPort);
    m_edit.connectPort = ToPort(m_connectPort);
    m_validationError.clear();
    Validate(m_edit, m_validationError);
    // Always handed over, valid or not: Play then uses exactly what the panel shows, and an invalid
    // choice fails to start with the same message instead of silently using older values.
    m_core.GetNetwork().SetSettings(m_edit);
}

void NetworkPanel::Update(float)
{
    if (!m_visible) { return; }

    // First open (no saved position): a small window in the middle of the editor.
    ImGui::SetNextWindowSize({ 320.0f, 420.0f }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });
    if (m_makeFocus) { ImGui::SetNextWindowFocus(); m_makeFocus = false; }

    bool open = true;
    if (ImGui::Begin(m_panelName.c_str(), &open, m_flags)) {
        NetworkSession& network = m_core.GetNetwork();
        // Only in Edit with no session running: a running session is never reconfigured.
        bool const editable = m_core.GetPlayState() == PlayState::Edit && !network.IsActive();
        bool changed = false;

        ImGui::BeginDisabled(!editable);

        int mode = static_cast<int>(m_edit.mode);
        ImGui::TextUnformatted("Mode");
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Offline", &mode, static_cast<int>(NetMode::Offline));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Host", &mode, static_cast<int>(NetMode::Host));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Client", &mode, static_cast<int>(NetMode::Client));
        m_edit.mode = static_cast<NetMode>(mode);

        ImGuiInputTextFlags const addressFlags = ImGuiInputTextFlags_CharsNoBlank;
        if (m_edit.mode == NetMode::Host) {
            changed |= ImGui::InputInt("Port##host", &m_hostPort, 0, 0);
            if (ImGui::TreeNode("Advanced")) {
                changed |= ImGui::InputTextWithHint("Listen address", "all interfaces",
                                                    m_listenAddress.data(), m_listenAddress.size(), addressFlags);
                ImGui::TreePop();
            }
        }
        else if (m_edit.mode == NetMode::Client) {
            changed |= ImGui::InputTextWithHint("Host IP", "127.0.0.1",
                                                m_connectAddress.data(), m_connectAddress.size(), addressFlags);
            changed |= ImGui::InputInt("Host port", &m_connectPort, 0, 0);
        }
        else {
            ImGui::TextDisabled("Networking is off: Play runs single-player.");
        }

        ImGui::EndDisabled();
        if (changed && editable) Apply();

        if (!m_validationError.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(EditorTheme::GetPalette().error, "%s", m_validationError.c_str());
            ImGui::PopTextWrapPos();
        }
        if (!editable && m_core.GetPlayState() != PlayState::Edit) {
            ImGui::TextDisabled("Locked while playing - press Stop to change.");
        }

        ImGui::Separator();
        DrawStatus();
    }
    ImGui::End();
    if (!open) { m_visible = false; }
}

void NetworkPanel::DrawStatus()
{
    NetworkSession const& network = m_core.GetNetwork();
    NetworkSettings const& active = network.ActiveSettings();

    if (network.IsServer()) {
        ImGui::TextColored(EditorTheme::GetPalette().success, "Hosting on port %u", static_cast<unsigned>(active.hostPort));
        ImGui::Text("Players joined: %zu of %u", network.JoinedPeers(), active.maxClients);
        for (PeerState const& peer : network.Peers()) {
            if (peer.IsJoined()) ImGui::BulletText("Player %u: %s (%s)", static_cast<unsigned>(peer.peer), peer.name.c_str(), peer.remoteAddress.c_str());
        }
    }
    else if (network.IsClient()) {
        switch (network.ClientState()) {
        case ClientPhase::Connecting:
            ImGui::Text("Connecting to %s:%u...", active.connectAddress.c_str(), static_cast<unsigned>(active.connectPort));
            break;
        case ClientPhase::Joining:
            ImGui::Text("Joining %s:%u...", active.connectAddress.c_str(), static_cast<unsigned>(active.connectPort));
            break;
        default:
            ImGui::TextColored(EditorTheme::GetPalette().success, "Joined %s:%u as player %u", active.connectAddress.c_str(),
                               static_cast<unsigned>(active.connectPort), static_cast<unsigned>(network.LocalPeer()));
            break;
        }
    }
    else {
        ImGui::TextDisabled("No session.");
    }

    ImGui::PushTextWrapPos(0.0f);
    if (!network.LastStartError().empty()) {
        ImGui::TextColored(EditorTheme::GetPalette().error, "Could not start: %s", network.LastStartError().c_str());
    }
    else if (!network.IsActive() && !network.LastEndReason().empty()) {
        ImGui::TextDisabled("Last session ended: %s", network.LastEndReason().c_str());
    }
    ImGui::PopTextWrapPos();
}

}
