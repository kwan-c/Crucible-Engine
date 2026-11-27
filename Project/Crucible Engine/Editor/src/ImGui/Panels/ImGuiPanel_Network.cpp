/*!
* @file			ImGuiPanel_Network.cpp
* @author		Abhishek Akilandeswari Patchamuthu
* @par			a.patchamuthu@digipen.edu
* @date			3 November 2026
*
* @brief		The editor's Network window, defined in ImGuiPanel_Network.h: the Session,
*				Connection Server and Debug tabs.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_Network.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>

#include "Core.h"
#include "Diagnostics/Logger.h"
#include "ImGui/EditorTheme.h"
#include "Network/ConnectionServerControl.h"
#include "Network/LobbyServerClient.h"
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
        case PeerState::Phase::Loading:       return "loading scene";
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

NetworkPanel::NetworkPanel(Core& core) : m_core(core) {}

NetworkPanel::~NetworkPanel() = default;   // the control lets go of the server: it keeps running

void NetworkPanel::Init()
{
    // Hidden until opened (the Network menu, or View > Network), then a floating window in the
    // middle of the editor; dock it anywhere if you like. The "###" id is new, so a layout saved
    // when this was a tab beside the Inspector no longer applies; the title still reads "Network".
    m_panelName = "Network###NetworkWindow";
    m_flags = ImGuiWindowFlags_None;
    m_docked = false;   // not part of the default layout: a floating window opened from View > Network
    LoadFromSession();

    // The connection server on this PC: found next to the Editor or in the build folder, and taken
    // back if an earlier Editor started it and it is still running.
#ifdef CRUCIBLE_BUILD_DIR
    std::filesystem::path const buildDir = CRUCIBLE_BUILD_DIR;
#else
    std::filesystem::path const buildDir;
#endif
    m_server = std::make_unique<ConnectionServerControl>(ConnectionServerControl::FindExecutable(ConnectionServerControl::ThisProgramDir(), buildDir));
    m_serverPort = m_edit.lobbyServerPort != 0 ? m_edit.lobbyServerPort : LobbyServer::kDefaultPort;
    m_serverLifetimeMs = static_cast<int>(LobbyServer::kDefaultLifetime.count());
    if (m_server->HasExecutable() && m_server->Reattach()) {
        m_serverPort = m_server->Port();
        m_serverLifetimeMs = static_cast<int>(m_server->LifetimeMs());
        CRU_INFO("[Net] the connection server started earlier is still running (process {}, port {})", m_server->ProcessId(), m_server->Port());
    }
}

void NetworkPanel::LoadFromSession()
{
    m_edit = m_core.GetNetwork().Settings();   // EngineConfig.json's defaults until this panel changes them
    CopyTo(m_connectAddress, m_edit.connectAddress);
    CopyTo(m_playerName, m_edit.playerName);
    CopyTo(m_lobbyServerAddress, m_edit.lobbyServerAddress);
    m_lobbyServerPort = m_edit.lobbyServerPort;
    m_gamePort = m_edit.hostPort;
    m_validationError.clear();
    Validate(m_edit, m_validationError);   // a bad EngineConfig.json value shows up here at once
}

void NetworkPanel::Apply()
{
    m_edit.connectAddress = m_connectAddress.data();
    m_edit.playerName = m_playerName.data();
    m_edit.lobbyServerAddress = m_lobbyServerAddress.data();
    m_edit.lobbyServerPort = ToPort(m_lobbyServerPort);
    m_edit.hostPort = ToPort(m_gamePort);      // one game port: hosted on, and joined at
    m_edit.connectPort = ToPort(m_gamePort);
    m_validationError.clear();
    Validate(m_edit, m_validationError);
    // Always handed over, valid or not: Play then uses exactly what the panel shows, and an invalid
    // choice fails to start with the same message instead of silently using older values.
    m_core.GetNetwork().SetSettings(m_edit);
}

void NetworkPanel::Update(float)
{
    // The server is watched even while the window is closed, so the tab is current when opened.
    m_server->Update(ConnectionServerControl::Clock::now());
    NetworkSession& network = m_core.GetNetwork();
    if (network.IsActive()) UpdateRates();   // every frame while a session runs, so the window never spans a gap

    if (!m_visible) return;

    // First open (no saved position): a small window in the middle of the editor.
    ImGui::SetNextWindowSize({ 320.0f, 420.0f }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });
    if (m_makeFocus) { ImGui::SetNextWindowFocus(); m_makeFocus = false; }
    bool open = true;
    if (ImGui::Begin(m_panelName.c_str(), &open, m_flags)) {
        // Only in Edit with no session running: a running session is never reconfigured.
        bool const editable = m_core.GetPlayState() == PlayState::Edit && !network.IsActive();
        if (ImGui::BeginTabBar("##network")) {
            auto const flagsFor = [this](Tab tab) {
                return m_hasSelectTab && m_selectTab == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            };
            if (ImGui::BeginTabItem("Session", nullptr, flagsFor(Tab::Session))) {
                DrawSessionTab(editable);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Connection Server", nullptr, flagsFor(Tab::ConnectionServer))) {
                DrawServerTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug", nullptr, flagsFor(Tab::Debug))) {
                DrawDebugTab();
                ImGui::EndTabItem();
            }
            m_hasSelectTab = false;
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (!open) m_visible = false;
}

// ---- Session ----------------------------------------------------------------------------------

void NetworkPanel::DrawSessionTab(bool editable)
{
    bool changed = false;
    ImGui::BeginDisabled(!editable);
    // Sent when joining; shown in lobby lists; what a script's StartHost / Join uses when it gives none.
    changed |= ImGui::InputTextWithHint("Player name", "Player", m_playerName.data(), m_playerName.size());
    changed |= DrawLobbyDiscovery();
    changed |= DrawQuickTest();
    changed |= DrawSimulation();
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

bool NetworkPanel::DrawQuickTest()
{
    bool changed = false;
    bool const open = ImGui::TreeNode("Quick test (start at Play)");
    if (!open) {
        ImGui::SameLine();
        if (m_edit.mode == NetMode::Offline) ImGui::TextDisabled("(off)");
        else ImGui::TextColored(EditorTheme::GetPalette().warning, "(%s at Play)", ToString(m_edit.mode));
        return false;
    }
    int mode = static_cast<int>(m_edit.mode);
    changed |= ImGui::RadioButton("Offline", &mode, static_cast<int>(NetMode::Offline));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play starts no session: the game's own menu or lobby hosts and joins\n"
                          "(Network.StartHost / Join). The normal choice.");
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Host", &mode, static_cast<int>(NetMode::Host));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play hosts a session at once (testing a scene without the lobby).");
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Client", &mode, static_cast<int>(NetMode::Client));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play joins the host below at once (testing a scene without the lobby).");
    m_edit.mode = static_cast<NetMode>(mode);

    if (m_edit.mode == NetMode::Client) {
        changed |= ImGui::InputTextWithHint("Host IP", "127.0.0.1", m_connectAddress.data(), m_connectAddress.size(),
                                            ImGuiInputTextFlags_CharsNoBlank);
    }
    changed |= ImGui::InputInt("Game port", &m_gamePort, 0, 0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The port a host listens on and a client joins (default 27020).");
    ImGui::TreePop();
    return changed;
}

// ---- Connection Server ------------------------------------------------------------------------

bool NetworkPanel::ServerRunning() const { return m_server && m_server->IsRunning(); }

bool NetworkPanel::CanStartServer() const { return m_server && m_server->HasExecutable() && !m_server->IsRunning(); }

void NetworkPanel::StartServer()
{
    m_serverError.clear();
    ConnectionServerControl::Options options;
    options.port = ToPort(m_serverPort);
    options.lifetimeMs = static_cast<std::uint32_t>(std::clamp(m_serverLifetimeMs, 3000, 60000));
    if (!m_server->Start(options, m_serverError)) {
        CRU_ERROR("[Net] connection server not started: {}", m_serverError);
        return;
    }
    CRU_INFO("[Net] connection server started on UDP port {} (process {}); it keeps running when the Editor closes",
             options.port, m_server->ProcessId());
}

void NetworkPanel::StopServer()
{
    if (!m_server->IsRunning()) return;
    const std::uint32_t pid = m_server->ProcessId();
    m_server->Stop();
    m_serverError.clear();
    CRU_INFO("[Net] connection server stopped (process {})", pid);
}

void NetworkPanel::UseLocalServer()
{
    const auto networks = LobbyServerClient::LocalNetworks();
    const std::string address = networks.empty() ? std::string("127.0.0.1") : LobbyServer::AddressText(networks.front().ipv4);
    CopyTo(m_lobbyServerAddress, address);
    m_lobbyServerPort = m_server->IsRunning() ? m_server->Port() : ToPort(m_serverPort);
    if (m_edit.lobbyDiscovery == LobbyDiscovery::Lan) m_edit.lobbyDiscovery = LobbyDiscovery::Auto;
    Apply();
}

void NetworkPanel::DrawServerTab()
{
    auto const& palette = EditorTheme::GetPalette();
    auto const now = ConnectionServerControl::Clock::now();
    ConnectionServerControl& server = *m_server;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Lists the lobbies on your network so every PC finds them. It runs as its own program "
                        "(CrucibleConnectionServer.exe) and keeps running when the Editor closes. Games never depend on it.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    if (!server.HasExecutable()) {
        ImGui::TextColored(palette.error, "CrucibleConnectionServer.exe was not found.");
        ImGui::TextDisabled("Build the CrucibleConnectionServer project, then reopen this tab.");
        if (ImGui::Button("Look again")) {
#ifdef CRUCIBLE_BUILD_DIR
            server.SetExecutable(ConnectionServerControl::FindExecutable(ConnectionServerControl::ThisProgramDir(), CRUCIBLE_BUILD_DIR));
#else
            server.SetExecutable(ConnectionServerControl::FindExecutable(ConnectionServerControl::ThisProgramDir(), {}));
#endif
        }
        return;
    }

    // Settings: only while stopped (the running server keeps its own).
    bool const running = server.IsRunning();
    ImGui::BeginDisabled(running);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("UDP port", &m_serverPort, 0, 0);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Lobby lifetime (ms)", &m_serverLifetimeMs, 0, 0);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How long a lobby stays listed without a heartbeat (3000 to 60000).\n"
                          "Hosts send one every 2 s; a crashed host disappears after this long.");
    ImGui::EndDisabled();

    if (!running) {
        ImGui::BeginDisabled(!CanStartServer());
        if (ImGui::Button("Start server")) StartServer();
        ImGui::EndDisabled();
    }
    else if (ImGui::Button("Stop server")) {
        StopServer();
    }

    // State
    ImGui::Spacing();
    switch (server.GetState()) {
    case ConnectionServerControl::State::Running: {
        LobbyServerStatus const status = server.MonitorState(now);
        if (server.Verified()) {
            auto const& s = server.ServerStatus();
            ImGui::TextColored(palette.success, "Running: up %u:%02u:%02u, %u lobby(s) listed", s.uptimeSeconds / 3600,
                               (s.uptimeSeconds / 60) % 60, s.uptimeSeconds % 60, static_cast<unsigned>(s.lobbies));
        }
        else if (server.HasServerStatus()) {
            ImGui::TextColored(palette.warning, "Another program answers on port %u (process %u, not %u).", server.Port(),
                               server.ServerStatus().processId, server.ProcessId());
        }
        else if (status == LobbyServerStatus::Unreachable) {
            ImGui::TextColored(palette.warning, "Running, but not answering on 127.0.0.1:%u.", server.Port());
        }
        else {
            ImGui::TextUnformatted("Starting...");
        }
        ImGui::TextDisabled("Process %u, UDP port %u, lifetime %u ms%s", server.ProcessId(), server.Port(), server.LifetimeMs(),
                            server.Reattached() ? " - started earlier, still running" : "");
        break;
    }
    case ConnectionServerControl::State::Exited:
        ImGui::TextColored(palette.error, "The server stopped by itself (exit code %u). The log below says why.", server.ExitCode());
        break;
    default:
        ImGui::TextDisabled("Not running.");
        break;
    }
    if (!m_serverError.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(palette.error, "%s", m_serverError.c_str());
        ImGui::PopTextWrapPos();
    }

    // Where other PCs reach it
    ImGui::SeparatorText("Addresses");
    std::uint16_t const port = running ? server.Port() : ToPort(m_serverPort);
    const auto networks = LobbyServerClient::LocalNetworks();
    if (networks.empty()) ImGui::TextDisabled("This PC has no network connection: only this PC can use it.");
    int row = 0;
    for (const auto& n : networks) {
        std::string const text = std::format("{}:{}", LobbyServer::AddressText(n.ipv4), port);
        ImGui::PushID(row++);
        ImGui::BulletText("%s", text.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(LobbyServer::AddressText(n.ipv4).c_str());
        ImGui::PopID();
    }
    ImGui::BulletText("127.0.0.1:%u  (this PC only)", static_cast<unsigned>(port));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("On every other PC: Network > Network Window > Session > Lobby discovery: Server address = one of the "
                        "addresses above (the one on the same network), Server port = %u.", static_cast<unsigned>(port));
    ImGui::PopTextWrapPos();

    bool const editable = m_core.GetPlayState() == PlayState::Edit && !m_core.GetNetwork().IsActive();
    std::string const preferred = networks.empty() ? std::string("127.0.0.1") : LobbyServer::AddressText(networks.front().ipv4);
    bool const usesIt = m_edit.lobbyDiscovery != LobbyDiscovery::Lan && m_edit.lobbyServerPort == port &&
                        (m_edit.lobbyServerAddress == preferred || m_edit.lobbyServerAddress == "127.0.0.1");
    ImGui::BeginDisabled(!editable || usesIt);
    if (ImGui::Button("Use this server")) UseLocalServer();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Sets this Editor's Lobby discovery to %s:%u.", preferred.c_str(), static_cast<unsigned>(port));
    ImGui::SameLine();
    if (usesIt) ImGui::TextColored(palette.success, "This Editor uses it.");
    else if (!editable) ImGui::TextDisabled("(press Stop to change)");

    // The log
    ImGui::SeparatorText("Log");
    ImGui::Checkbox("Follow", &m_followLog);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", server.LogPath().string().c_str());
    if (ImGui::BeginChild("##serverlog", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        if (server.LogTail().empty()) ImGui::TextDisabled(running ? "(nothing yet)" : "(the log of the last run appears here)");
        for (const std::string& line : server.LogTail()) ImGui::TextUnformatted(line.c_str());
        if (m_followLog && ImGui::GetScrollY() < ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

// ---- Debug ------------------------------------------------------------------------------------

void NetworkPanel::DrawDebugTab()
{
    if (!m_core.GetNetwork().IsActive()) {
        ImGui::TextDisabled("No session. The numbers appear here while one runs.");
        return;
    }
    DrawStatistics();
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

bool NetworkPanel::DrawLobbyDiscovery()
{
    bool changed = false;
    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    bool const open = ImGui::TreeNode("Lobby discovery");
    bool const needsAddress = m_edit.lobbyDiscovery == LobbyDiscovery::Server && m_lobbyServerAddress[0] == '\0';
    if (!open) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s%s%s)", ToString(m_edit.lobbyDiscovery), m_edit.lobbyServerAddress.empty() ? "" : ", server ",
                            m_edit.lobbyServerAddress.c_str());
        if (needsAddress) {
            ImGui::SameLine();
            ImGui::TextColored(EditorTheme::GetPalette().warning, "no server address");
        }
        return false;
    }
    int discovery = static_cast<int>(m_edit.lobbyDiscovery);
    changed |= ImGui::RadioButton("Auto", &discovery, static_cast<int>(LobbyDiscovery::Auto));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The connection server first. If it does not answer %d list requests, the lobby browser\n"
                          "says so and the game may offer LAN broadcast instead. No server address: LAN broadcast.\n"
                          "A hosted session is listed both ways.", LobbyServer::kListAttempts);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Server", &discovery, static_cast<int>(LobbyDiscovery::Server));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The connection server only (testing).");
    ImGui::SameLine();
    changed |= ImGui::RadioButton("LAN", &discovery, static_cast<int>(LobbyDiscovery::Lan));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("LAN broadcast only; the connection server is not used (testing).");
    m_edit.lobbyDiscovery = static_cast<LobbyDiscovery>(discovery);

    ImGui::BeginDisabled(m_edit.lobbyDiscovery == LobbyDiscovery::Lan);
    changed |= ImGui::InputTextWithHint("Server address", "none (e.g. 192.168.0.5)", m_lobbyServerAddress.data(),
                                        m_lobbyServerAddress.size(), ImGuiInputTextFlags_CharsNoBlank);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The IPv4 address of the PC running CrucibleConnectionServer.exe\n"
                          "(it prints its addresses when it starts). Empty: no connection server.");
    ImGui::TextDisabled("127.0.0.1 is the connection server on this PC. Using another device? Change it to the\n"
                        "IPv4 address of the PC running the connection server.");
    changed |= ImGui::InputInt("Server port", &m_lobbyServerPort, 0, 0);
    ImGui::EndDisabled();
    if (m_edit.lobbyDiscovery == LobbyDiscovery::Server && m_lobbyServerAddress[0] == '\0') {
        // Not an error that stops Play (Quick test Host / Client need no server), but nothing is
        // listed or found this way: say so here instead of a lobby browser searching forever.
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(EditorTheme::GetPalette().warning,
                           "Server needs a Server address: hosted lobbies are not listed and lobby lists stay empty. "
                           "Set the address, or choose Auto.");
        ImGui::PopTextWrapPos();
    }
    ImGui::TextDisabled("Applies to the next Play / the next lobby browser.");
    ImGui::TreePop();
    return changed;
}

void NetworkPanel::DrawLobbyServerStatus()
{
    NetworkSession const& network = m_core.GetNetwork();
    if (std::string const problem = network.LobbyListingProblem(); !problem.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(EditorTheme::GetPalette().warning, "This session is not in lobby lists: %s", problem.c_str());
        ImGui::PopTextWrapPos();
    }
    LobbyServerStatus const status = network.LobbyServerState();
    if (network.IsServer() && !network.LobbyOpen() && network.ActiveSettings().lobbyDiscovery != LobbyDiscovery::Lan &&
        !network.ActiveSettings().lobbyServerAddress.empty()) {
        // Closed (the match started): unregistered on purpose, so the server line below is gone.
        ImGui::TextDisabled("Connection server: not listed while the lobby is closed (match in progress)");
    }
    if (status == LobbyServerStatus::Off) return;
    std::string const endpoint = network.LobbyServerEndpoint();
    auto const& palette = EditorTheme::GetPalette();
    switch (status) {
    case LobbyServerStatus::Online:
        ImGui::TextColored(palette.success, "Connection server %s: online%s", endpoint.c_str(),
                           network.IsServer() ? (network.LobbyRegistered() ? " (this session is listed)" : "") : "");
        break;
    case LobbyServerStatus::Connecting:
        if (network.IsBrowsing() && network.LobbyListFromServer())
            ImGui::Text("Connection server %s: connecting (attempt %d of %d)...", endpoint.c_str(),
                        std::min(network.LobbyServerFailedAttempts() + 1, LobbyServer::kListAttempts), LobbyServer::kListAttempts);
        else
            ImGui::Text("Connection server %s: connecting...", endpoint.c_str());
        break;
    default:
        ImGui::TextColored(palette.warning, "Connection server %s: unreachable%s", endpoint.c_str(),
                           network.LanDiscoveryChosen() ? " - lobby list from LAN broadcast (chosen)"
                           : network.CanUseLanDiscovery() ? " - the lobby browser can offer LAN broadcast" : "");
        break;
    }
    if (network.IsBrowsing() && network.LobbyListFromServer() && network.LobbyListTruncated()) {
        ImGui::TextDisabled("Lobby list: %zu of %u (truncated)", network.Lobbies().size(), static_cast<unsigned>(network.LobbyListTotal()));
    }
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
        if (network.IsChangingScene())
            ImGui::TextColored(EditorTheme::GetPalette().warning, "Changing scene (%u): waiting for the players to load it", network.SceneTransition());
        for (PeerState const& peer : network.Peers()) {
            if (!peer.IsJoined()) continue;
            ConnectionStatus link;
            if (network.PeerLinkStatus(peer.peer, link))
                ImGui::BulletText("Player %u: %s (%s) - %d ms", static_cast<unsigned>(peer.peer), peer.name.c_str(), peer.remoteAddress.c_str(), link.pingMs);
            else
                ImGui::BulletText("Player %u: %s (%s)", static_cast<unsigned>(peer.peer), peer.name.c_str(), peer.remoteAddress.c_str());
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
            if (network.IsChangingScene())
                ImGui::TextColored(EditorTheme::GetPalette().warning, "Loading the host's scene (change %u)...", network.SceneTransition());
            ImGui::TextColored(EditorTheme::GetPalette().success, "Joined %s:%u as player %u", active.connectAddress.c_str(),
                               static_cast<unsigned>(active.connectPort), static_cast<unsigned>(network.LocalPeer()));
            if (ConnectionStatus link; network.HostLinkStatus(link)) {
                ImGui::SameLine();
                ImGui::TextDisabled("- %d ms", link.pingMs);
            }
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
    DrawLobbyServerStatus();
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
