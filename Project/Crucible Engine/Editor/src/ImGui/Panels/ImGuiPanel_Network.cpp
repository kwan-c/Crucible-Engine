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
#include <cstdio>
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

    const char* PhaseName(PeerState::Phase phase) {
        switch (phase) {
        case PeerState::Phase::Connecting:    return "connecting";
        case PeerState::Phase::AwaitingJoin:  return "joining";
        case PeerState::Phase::Synchronizing: return "loading world";
        case PeerState::Phase::InGame:        return "in game";
        default:                              return "free";
        }
    }

    const char* WorldName(ClientWorld world) {
        switch (world) {
        case ClientWorld::Receiving: return "receiving";
        case ClientWorld::Ready:     return "ready";
        default:                     return "waiting";
        }
    }

    // A GNS quality (0..1, or negative for "not measured yet", about the first second) as text.
    void QualityText(char (&out)[16], float quality) {
        if (quality < 0.f) std::snprintf(out, sizeof(out), "--");
        else std::snprintf(out, sizeof(out), "%.0f%%", static_cast<double>(quality) * 100.0);
    }

    // One "label | value" row of a two-column table.
    template <typename... Args>
    void Row(const char* label, const char* format, Args... args) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::Text(format, args...);
    }

    unsigned long long U64(std::uint64_t value) { return static_cast<unsigned long long>(value); }
}

void NetworkPanel::Init()
{
    m_panelName = "Network";
    m_flags = ImGuiWindowFlags_None;
    m_docked = true;
    m_dockArea = DockArea::Right;   // a tab beside the Inspector
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
    if (ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {
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
        if (m_edit.mode != NetMode::Offline) changed |= DrawSimulation();

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
        if (network.IsActive()) DrawStatistics();
    }
    ImGui::End();
}

bool NetworkPanel::DrawSimulation()
{
    bool changed = false;
    if (!ImGui::TreeNode("Network simulation (testing)")) {
        if (m_edit.simulation.IsActive()) {
            ImGui::SameLine();
            ImGui::TextColored(EditorTheme::GetPalette().warning, "(on)");
        }
        return false;
    }

    int latency = static_cast<int>(m_edit.simulation.extraRoundTripMs);
    if (ImGui::SliderInt("Extra latency", &latency, 0, static_cast<int>(NetworkSimulation::kMaxExtraRoundTripMs), "%d ms",
                         ImGuiSliderFlags_AlwaysClamp)) {
        m_edit.simulation.extraRoundTripMs = static_cast<std::uint32_t>(std::max(latency, 0));
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Added to the round trip of every connection of this editor (half each way).\n"
                          "The delay is applied in full, but the Ping shown under Statistics can read\n"
                          "lower when this editor is a Client: set it on the Host for an exact reading.");
    }

    float loss = m_edit.simulation.packetLossPercent;
    if (ImGui::SliderFloat("Packet loss", &loss, 0.f, NetworkSimulation::kMaxPacketLossPercent, "%.1f %%",
                           ImGuiSliderFlags_AlwaysClamp)) {
        m_edit.simulation.packetLossPercent = loss;
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Share of packets dropped, separately for sending and for receiving.");

    ImGui::TextDisabled("Applies to this whole editor while Play runs. Set it on one editor only.");
    if (m_edit.simulation.IsActive() && ImGui::SmallButton("Back to a normal network")) {
        m_edit.simulation = NetworkSimulation{};
        changed = true;
    }
    ImGui::TreePop();
    return changed;
}

void NetworkPanel::UpdateRates()
{
    NetworkSession const& network = m_core.GetNetwork();
    NetStats const& stats = network.Stats();
    Rates now;
    now.snapshots = network.IsServer() ? stats.snapshotsSent : stats.snapshotsApplied;
    now.inputs = network.IsServer() ? stats.inputsReceived : stats.inputsSent;
    now.messages = stats.messagesReceived;
    now.bytes = stats.bytesReceived;

    double const time = ImGui::GetTime();
    if (network.Generation() != m_rateGeneration || m_rateWindowStart < 0.0) {
        // A different session: its counters started from zero.
        m_rateGeneration = network.Generation();
        m_rateWindowStart = time;
        m_rateSample = now;
        m_hasRates = false;
        return;
    }
    double const elapsed = time - m_rateWindowStart;
    if (elapsed < 1.0) return;

    auto const perSecond = [elapsed](std::uint64_t to, std::uint64_t from) {
        return to >= from ? static_cast<std::uint64_t>(static_cast<double>(to - from) / elapsed + 0.5) : 0;
    };
    m_ratePerSecond.snapshots = perSecond(now.snapshots, m_rateSample.snapshots);
    m_ratePerSecond.inputs = perSecond(now.inputs, m_rateSample.inputs);
    m_ratePerSecond.messages = perSecond(now.messages, m_rateSample.messages);
    m_ratePerSecond.bytes = perSecond(now.bytes, m_rateSample.bytes);
    m_rateSample = now;
    m_rateWindowStart = time;
    m_hasRates = true;
}

void NetworkPanel::DrawStatistics()
{
    UpdateRates();   // every frame while a session runs, so the window never spans a gap
    if (!ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) return;

    NetworkSession const& network = m_core.GetNetwork();
    NetStats const& stats = network.Stats();
    ImGuiTableFlags const tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    char qualityIn[16];
    char qualityOut[16];

    // Connections: GNS's live numbers.
    if (network.IsServer()) {
        if (network.OccupiedSlots() == 0) {
            ImGui::TextDisabled("No clients connected.");
        }
        else if (ImGui::BeginTable("##netpeers", 6, tableFlags)) {
            ImGui::TableSetupColumn("Player");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("Ping");
            ImGui::TableSetupColumn("Quality in/out");
            ImGui::TableSetupColumn("KB/s in/out");
            ImGui::TableSetupColumn("Lifecycle sent/applied");
            ImGui::TableHeadersRow();
            for (PeerState const& peer : network.Peers()) {
                if (peer.phase == PeerState::Phase::Free) continue;
                ConnectionStatus link;
                bool const hasLink = network.PeerLinkStatus(peer.peer, link);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u %s", static_cast<unsigned>(peer.peer), peer.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(PhaseName(peer.phase));
                if (!hasLink) continue;
                QualityText(qualityIn, link.qualityLocal);
                QualityText(qualityOut, link.qualityRemote);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d ms", link.pingMs);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s / %s", qualityIn, qualityOut);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.1f / %.1f", static_cast<double>(link.inBytesPerSec) / 1024.0, static_cast<double>(link.outBytesPerSec) / 1024.0);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%u / %u", peer.nextLifecycleSeq > 0 ? peer.nextLifecycleSeq - 1 : 0u, peer.appliedLifecycle);
            }
            ImGui::EndTable();
        }
    }
    else if (network.IsClient()) {
        ConnectionStatus link;
        if (network.HostLinkStatus(link)) {
            QualityText(qualityIn, link.qualityLocal);
            QualityText(qualityOut, link.qualityRemote);
            ImGui::Text("Host link: ping %d ms, quality in %s / out %s", link.pingMs, qualityIn, qualityOut);
            ImGui::Text("Host link: %.1f KB/s in, %.1f KB/s out", static_cast<double>(link.inBytesPerSec) / 1024.0,
                        static_cast<double>(link.outBytesPerSec) / 1024.0);
        }
        ImGui::Text("World: %s, lifecycle message %u applied", WorldName(network.WorldState()), network.AppliedLifecycle());
    }

    // Rates: our own counters over the last second.
    if (!m_hasRates) {
        ImGui::TextDisabled("Per second: measuring...");
    }
    else if (network.IsServer()) {
        ImGui::Text("Per second: %llu snapshots sent, %llu inputs received, %llu messages (%.1f KB) received",
                    U64(m_ratePerSecond.snapshots), U64(m_ratePerSecond.inputs), U64(m_ratePerSecond.messages),
                    static_cast<double>(m_ratePerSecond.bytes) / 1024.0);
    }
    else {
        ImGui::Text("Per second: %llu snapshots applied, %llu inputs sent, %llu messages (%.1f KB) received",
                    U64(m_ratePerSecond.snapshots), U64(m_ratePerSecond.inputs), U64(m_ratePerSecond.messages),
                    static_cast<double>(m_ratePerSecond.bytes) / 1024.0);
    }

    // The session's own counters (NetStats): a steady rise in a rejection counter means a bug
    // or a misbehaving peer.
    if (!ImGui::TreeNode("Counters")) return;
    if (ImGui::BeginTable("##netcounters", 2, tableFlags)) {
        Row("Messages received", "%llu (%.1f KB)", U64(stats.messagesReceived), static_cast<double>(stats.bytesReceived) / 1024.0);
        Row("Malformed", "%llu", U64(stats.malformed));
        Row("Unexpected", "%llu", U64(stats.unexpected));
        Row("Over quota", "%llu", U64(stats.quotaDropped));
        Row("Oversized", "%llu", U64(stats.oversized));
        Row("Send failed", "%u", stats.sendFailed);
        if (network.IsServer()) {
            Row("Joined", "%u", stats.joined);
            Row("Refused (full / content)", "%u / %u", stats.refusedFull, stats.refusedProtocol);
            Row("Timeouts (join / world)", "%u / %u", stats.joinTimeouts, stats.syncTimeouts);
            Row("Closed for bad traffic", "%u", stats.closedMalformed);
            Row("Peers left", "%u", stats.peersLeft);
            Row("Snapshots sent / acked", "%llu / %llu", U64(stats.snapshotsSent), U64(stats.snapshotsAcked));
            Row("Snapshot send failed", "%u", stats.snapshotSendFailed);
            Row("Snapshot bytes (last / max)", "%u / %u", stats.lastSnapshotBytes, stats.maxSnapshotBytes);
            Row("Records deferred by budget", "%llu", U64(stats.recordsDeferred));
            Row("Spawns (done / failed)", "%u / %u", stats.spawnsPerformed, stats.spawnsFailed);
            Row("Despawns queued", "%u", stats.despawnsQueued);
            Row("Gameplay events (sent / refused)", "%llu / %u", U64(stats.gameplayEventsSent), stats.gameplayEventsRefused);
            Row("Rosters sent", "%u", stats.rostersSent);
            Row("Commands (from clients / own)", "%llu / %llu", U64(stats.commandMessagesReceived), U64(stats.commandsLocal));
            Row("Commands (refused / missed / no target)", "%u / %llu / %u", stats.commandsRefused, U64(stats.commandsMissed),
                stats.commandsNoTarget);
        }
        else {
            Row("Snapshots (applied / rejected)", "%llu / %u", U64(stats.snapshotsApplied), stats.snapshotsRejected);
            Row("Stale records", "%llu", U64(stats.staleRecords));
            Row("Records for unknown entities", "%llu", U64(stats.unknownEntity));
            Row("Lifecycle messages applied", "%u", stats.lifecycleApplied);
            Row("Proxies (created / destroyed)", "%u / %u", stats.proxiesCreated, stats.proxiesDestroyed);
            Row("Interpolation teleports", "%u", stats.interpolationTeleports);
            Row("Rosters applied", "%u", stats.rostersApplied);
            Row("Commands (sent / not sent / refused)", "%llu / %u / %u", U64(stats.commandMessagesSent), stats.commandSendFailed,
                stats.commandsRefused);
            if (stats.inputLatencySamples > 0) {
                Row("Key-to-motion (last / avg / min / max)", "%u / %llu / %u / %u ms", stats.lastInputLatencyMs,
                    U64(stats.totalInputLatencyMs / stats.inputLatencySamples), stats.minInputLatencyMs, stats.maxInputLatencyMs);
            }
            else {
                Row("Key-to-motion", "not measured yet");
            }
        }
        ImGui::EndTable();
    }
    ImGui::TreePop();
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
            // Everyone in the session, as the host told this client (its roster).
            ImGui::Text("Players in the session: %zu", network.PlayerCount());
            for (NetPeerInfo const& player : network.Roster()) {
                if (!player.Present()) continue;
                ImGui::BulletText("Player %u: %s%s%s", static_cast<unsigned>(player.peer), player.name.c_str(),
                                  player.peer == network.LocalPeer() ? " (you)" : player.peer == Net::kServerPeer ? " (host)" : "",
                                  player.inGame ? "" : " - loading");
            }
            break;
        }
    }
    else {
        ImGui::TextDisabled("No session.");
    }
    if (network.IsSimulating()) {
        ImGui::TextColored(EditorTheme::GetPalette().warning, "Simulating a bad network: +%u ms round trip, %.1f%% packet loss",
                           active.simulation.extraRoundTripMs, static_cast<double>(active.simulation.packetLossPercent));
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
