#include "ImGui/Panels/ImGuiPanel_PlayPause.h"

#include <imgui_internal.h> // BeginViewportSideBar reserves space above the dockspace.
#include <algorithm>
#include <functional>

#include "Core.h"

namespace Crucible {

namespace {
    enum class TransportIcon { Play, Pause, Stop };

    bool TransportButton(const char* id, const char* tooltip, TransportIcon icon,
                         bool selected, bool enabled, ImVec2 size)
    {
        ImGui::BeginDisabled(!enabled);


        bool const clicked = ImGui::Button(id, size);
        ImVec2 const min = ImGui::GetItemRectMin();
        ImVec2 const max = ImGui::GetItemRectMax();
        ImVec2 const center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        float const radius = size.y * 0.22f;
        ImU32 const color = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        switch (icon) {
        case TransportIcon::Play:
            draw->AddTriangleFilled(ImVec2(center.x - radius * 0.65f, center.y - radius),
                                    ImVec2(center.x - radius * 0.65f, center.y + radius),
                                    ImVec2(center.x + radius, center.y), color);
            break;
        case TransportIcon::Pause:
            draw->AddRectFilled(ImVec2(center.x - radius, center.y - radius),
                                ImVec2(center.x - radius * 0.3f, center.y + radius), color);
            draw->AddRectFilled(ImVec2(center.x + radius * 0.3f, center.y - radius),
                                ImVec2(center.x + radius, center.y + radius), color);
            break;
        case TransportIcon::Stop:
            draw->AddRectFilled(ImVec2(center.x - radius, center.y - radius),
                                ImVec2(center.x + radius, center.y + radius), color);
            break;
        }

        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    }
}

void PlayPause::Init()
{
    m_panelName = "##PlayPauseToolbar";

    // Core reports every applied transition, and every failed request, on the engine bus.
    EventBus& engine = m_core.Events().engine;
    m_stateChanged = engine.Subscribe<EngineEvents::PlayStateChanged>(
        [this](const EngineEvents::PlayStateChanged& change) { OnPlayStateChanged(change); });
    m_requestFailed = engine.Subscribe<EngineEvents::PlayRequestFailed>(
        [this](const EngineEvents::PlayRequestFailed& failed) {
            m_error = std::string(ToString(failed.request)) + " failed: " + failed.reason;
        });
}

void PlayPause::OnPlayStateChanged(const EngineEvents::PlayStateChanged& change)
{
    m_error.clear();

    // Play, Pause and Resume show the Game view; Stop goes back to the Scene view.
    SelectViewport(change.to != PlayState::Edit);
}

void PlayPause::SelectViewport(bool game)
{
    // Let the panel manager select the tab when it next draws that viewport.
    // Clear both requests so a pending Scene focus cannot override Play.
    auto& panels = EditorManager::GetInstance().m_panels;
    for (ImguiPanels viewport : { ImguiPanels::EDITORSCENE, ImguiPanels::GAMESCENE }) {
        auto const panel = panels.find(viewport);
        if (panel != panels.end() && panel->second) {
            panel->second->m_makeFocus = game ? viewport == ImguiPanels::GAMESCENE
                                              : viewport == ImguiPanels::EDITORSCENE;
        }
    }
}

void PlayPause::Update(float)
{
    float const buttonHeight = ImGui::GetFrameHeight();
    float const padding = ImGui::GetStyle().FramePadding.y + 2.0f;
    ImVec2 const buttonSize(buttonHeight * 1.35f, buttonHeight);
    float const gap = 2.0f;
    float const controlsWidth = buttonSize.x * 3.0f + gap * 2.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGuiWindowFlags const flags = ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

    if (ImGui::BeginViewportSideBar(m_panelName.c_str(), ImGui::GetMainViewport(),
                                  ImGuiDir_Up, buttonHeight + padding * 2.0f, flags)) {
        float const controlsX = std::max(8.0f, (ImGui::GetWindowWidth() - controlsWidth) * 0.5f);
        ImGui::SetCursorPos(ImVec2(controlsX, padding));

        // The engine's state, not a copy: buttons only queue requests, and the UI follows
        // once Core has applied them (next frame). While one is waiting, the buttons rest.
        PlayState const state = m_core.GetPlayState();
        bool const waiting = m_core.HasPendingPlayRequest();
        bool const running = state != PlayState::Edit;
        bool const paused = state == PlayState::Paused;

        if (TransportButton("##Play", running ? "Exit Play Mode" : "Play",
                            TransportIcon::Play, running, !waiting, buttonSize)) {
            if (running) m_core.RequestStop();
            else         m_core.RequestPlay();
        }

        ImGui::SameLine();
        if (TransportButton("##Pause", paused ? "Resume" : "Pause",
                            TransportIcon::Pause, paused, running && !waiting, buttonSize)) {
            if (paused) m_core.RequestResume();
            else        m_core.RequestPause();
        }

        ImGui::SameLine();
        if (TransportButton("##Stop", "Stop", TransportIcon::Stop, false,
                            running && !waiting, buttonSize))
            m_core.RequestStop();

        // Only show status when it fits without crowding the centered controls. A failed
        // request is shown in place of the status until the next transition succeeds.
        const char* status = ToString(state);
        if (controlsX > ImGui::CalcTextSize("Playing").x + 24.0f) {
            ImGui::SetCursorPos(ImVec2(8.0f, padding + ImGui::GetStyle().FramePadding.y));
            if (m_error.empty()) {
                ImGui::TextDisabled("%s", status);
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", status);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", m_error.c_str());
            }
        }

        ImVec2 const pos = ImGui::GetWindowPos();
        ImVec2 const size = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + size.y - 1.0f),
            ImVec2(pos.x + size.x, pos.y + size.y - 1.0f), ImGui::GetColorU32(ImGuiCol_Border));
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

} // namespace Crucible
