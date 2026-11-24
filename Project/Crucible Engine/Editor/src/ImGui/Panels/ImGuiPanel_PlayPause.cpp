/*!
* @file			ImGuiPanel_PlayPause.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's Play / Pause / Stop toolbar, defined in ImGuiPanel_PlayPause.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_PlayPause.h"

#include <imgui_internal.h> // BeginViewportSideBar reserves space above the dockspace.
#include <algorithm>
#include <functional>

#include "Core.h"
#include "Assets/BuildProblems.h"   // why Play is refused, for the button's tooltip
#include "Prefabs/PrefabStage.h"    // ...and Prefab Mode refuses it too
#include "ImGui/EditorTheme.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"

namespace Crucible {

namespace {
    enum class TransportIcon { Play, Pause, Stop };

    // One colour per state, used everywhere the state shows: the button, the status label
    // and the toolbar's tint/underline. Green = playing, amber = paused, red = stop.
    // Taken from the editor theme (darker shades on light themes).
    ImVec4 PlayColor()  { return EditorTheme::GetPalette().success; }
    ImVec4 PauseColor() { return EditorTheme::GetPalette().warning; }
    ImVec4 StopColor()  { return EditorTheme::GetPalette().error; }

    ImVec4 Scale(ImVec4 c, float k) { return ImVec4(c.x * k, c.y * k, c.z * k, c.w); }

    // A name still being typed in the Inspector is applied (and recorded) before a Play or Stop
    // request: the request runs at the start of next frame, before the Inspector sees its field lose
    // focus, so Play would otherwise capture the old name.
    void CommitInspectorEdit() {
        if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) {
            inspector->CommitPendingEdit();
        }
    }
    ImVec4 Mix(ImVec4 a, ImVec4 b, float t) {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    // selected = this button IS the current state (Play while playing, Pause while paused):
    //   filled with its accent colour, dark icon - unmistakable at a glance.
    // not selected = neutral button with the icon in its accent colour, so the three are still
    //   told apart by colour as well as shape. Disabled buttons fade (BeginDisabled's alpha also
    //   applies to the icon, since its colour goes through GetColorU32).
    bool TransportButton(const char* id, const char* tooltip, TransportIcon icon, ImVec4 accent,
                         bool selected, bool enabled, ImVec2 size)
    {
        ImGui::BeginDisabled(!enabled);

        int pushed = 0;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button,        Scale(accent, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Scale(accent, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_Border,        accent);
            pushed = 4;
        }

        bool const clicked = ImGui::Button(id, size);
        ImGui::PopStyleColor(pushed);

        ImVec2 const min = ImGui::GetItemRectMin();
        ImVec2 const max = ImGui::GetItemRectMax();
        ImVec2 const center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        float const radius = size.y * 0.22f;
        ImU32 const color = ImGui::GetColorU32(selected ? ImVec4(0.07f, 0.07f, 0.08f, 1.0f) : accent);
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

    // The engine's state, not a copy: buttons only queue requests, and the UI follows
    // once Core has applied them (next frame). While one is waiting, the buttons rest.
    PlayState const state = m_core.GetPlayState();
    bool const waiting = m_core.HasPendingPlayRequest();
    bool const running = state != PlayState::Edit;
    bool const paused = state == PlayState::Paused;

    // The state's colour: none in Edit, green while playing, amber while paused.
    ImVec4 const stateColor = paused ? PauseColor() : running ? PlayColor() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

    // Tint the whole toolbar strip while in play mode, so the state reads from across the
    // screen (Unity tints its editor for the same reason) - easy to forget you're still playing.
    ImVec4 const barBg = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, running ? Mix(barBg, stateColor, 0.18f) : barBg);

    if (ImGui::BeginViewportSideBar(m_panelName.c_str(), ImGui::GetMainViewport(),
                                  ImGuiDir_Up, buttonHeight + padding * 2.0f, flags)) {
        float const controlsX = std::max(8.0f, (ImGui::GetWindowWidth() - controlsWidth) * 0.5f);
        ImGui::SetCursorPos(ImVec2(controlsX, padding));

        // Unity's shortcuts: Ctrl+P is the Play button (Play, or Exit Play Mode), Ctrl+Shift+P
        // the Pause button. Global, so they work from any panel - and mid-game, since a locked
        // cursor only blocks ImGui's mouse, not its keyboard. Read before the buttons, then
        // put AFTER them in each || so the button is always drawn; same rules as the button
        // (nothing while a request waits, no pause unless running). ImGui matches modifiers
        // exactly, so Ctrl+Shift+P never also counts as Ctrl+P.
        bool const playKey  = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, ImGuiInputFlags_RouteGlobal);
        bool const pauseKey = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, ImGuiInputFlags_RouteGlobal);

        // Still clickable while blocked: the click logs the reason to the Console and shows it
        // in the status here (PlayMode's guard). The tooltip says it up front.
        std::string blocked;
        if (!running) {
            blocked = PrefabStage::PlayBlockReason();
            if (blocked.empty()) blocked = BuildProblems::PlayBlockReason();
        }
        std::string const playTip = running ? "Exit Play Mode (Ctrl+P)"
                                  : blocked.empty() ? "Play (Ctrl+P)" : "Play is blocked: " + blocked;
        if (TransportButton("##Play", playTip.c_str(),
                            TransportIcon::Play, PlayColor(), running && !paused, !waiting, buttonSize)
            || (playKey && !waiting)) {
            CommitInspectorEdit();
            if (running) m_core.RequestStop();
            else         m_core.RequestPlay();
        }

        ImGui::SameLine();
        if (TransportButton("##Pause", paused ? "Resume (Ctrl+Shift+P)" : "Pause (Ctrl+Shift+P)",
                            TransportIcon::Pause, PauseColor(), paused, running && !waiting, buttonSize)
            || (pauseKey && running && !waiting)) {
            if (paused) m_core.RequestResume();
            else        m_core.RequestPause();
        }

        ImGui::SameLine();
        if (TransportButton("##Stop", "Stop", TransportIcon::Stop, StopColor(), false,
                            running && !waiting, buttonSize)) {
            CommitInspectorEdit();
            m_core.RequestStop();
        }

        // Only show status when it fits without crowding the centered controls. A failed
        // request is shown in place of the status until the next transition succeeds.
        const char* status = ToString(state);
        if (controlsX > ImGui::CalcTextSize("Playing").x + 40.0f) {   // + room for the status dot
            ImGui::SetCursorPos(ImVec2(8.0f, padding + ImGui::GetStyle().FramePadding.y));
            if (m_error.empty()) {
                // A filled dot in the state's colour, then the state name in the same colour.
                float const dot = ImGui::GetTextLineHeight() * 0.28f;
                ImVec2 const at = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(at.x + dot, at.y + ImGui::GetTextLineHeight() * 0.5f), dot, ImGui::GetColorU32(stateColor));
                ImGui::Dummy(ImVec2(dot * 2.0f + 6.0f, 0.0f));
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored(stateColor, "%s", status);
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", status);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", m_error.c_str());
            }
        }

        ImVec2 const pos = ImGui::GetWindowPos();
        ImVec2 const size = ImGui::GetWindowSize();
        // Underline: the normal border in Edit, a 2px line in the state's colour in play mode.
        if (running) {
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + size.y - 2.0f),
                ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(stateColor));
        } else {
            ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + size.y - 1.0f),
                ImVec2(pos.x + size.x, pos.y + size.y - 1.0f), ImGui::GetColorU32(ImGuiCol_Border));
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

} // namespace Crucible
