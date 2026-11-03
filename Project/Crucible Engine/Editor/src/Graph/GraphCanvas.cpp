/*************************************************************************
* \file:         GraphCanvas.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The canvas: input, grid, and the drawing helpers.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "GraphCanvas.h"

// STL
#include <algorithm>    // std::clamp, std::max, std::min
#include <cmath>        // std::fmod, std::pow

namespace Crucible {

    namespace {

        constexpr float kMinZoom  = 0.25f;
        constexpr float kMaxZoom  = 2.0f;
        constexpr float kGridStep = 40.0f;    // graph units between grid lines

        constexpr ImU32 kBackground = IM_COL32(26, 29, 35, 255);   // the Animator panel's colours, so the editors match
        constexpr ImU32 kGridLine   = IM_COL32(42, 45, 53, 255);

    }   // ANONYMOUS NAMESPACE

    void GraphCanvas::Begin(const char* id) {

        m_origin = ImGui::GetCursorScreenPos();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        m_size = { std::max(available.x, 1.0f), std::max(available.y, 1.0f) };

        // A pending "frame these nodes", now that the size is known.
        if (m_frameRequested) {
            m_frameRequested = false;
            const Math::vec2 extent = glm::max(m_frameMax - m_frameMin, Math::vec2(1.0f));
            m_zoom = std::clamp(std::min((m_size.x - 60.0f) / extent.x, (m_size.y - 60.0f) / extent.y), kMinZoom, 1.25f);
            m_pan  = { 30.0f - m_frameMin.x * m_zoom, 30.0f - m_frameMin.y * m_zoom };
        }

        // One invisible button over the whole canvas catches every mouse
        // button, so clicks on empty space never fall through to the window.
        ImGui::InvisibleButton(id, m_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        m_hovered      = ImGui::IsItemHovered();
        m_leftClicked  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        m_rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        m_leftHeld     = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        m_leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

        const ImGuiIO& io = ImGui::GetIO();

        // Wheel: zoom around the mouse - the point under it stays put, which is
        // what makes zooming feel like moving towards something.
        if (m_hovered && io.MouseWheel != 0.0f) {
            const float  zoom  = std::clamp(m_zoom * std::pow(1.15f, io.MouseWheel), kMinZoom, kMaxZoom);
            const ImVec2 local{ io.MousePos.x - m_origin.x, io.MousePos.y - m_origin.y };
            m_pan  = { local.x - (local.x - m_pan.x) * (zoom / m_zoom), local.y - (local.y - m_pan.y) * (zoom / m_zoom) };
            m_zoom = zoom;
        }

        // Middle drag: pan.
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            m_pan = { m_pan.x + io.MouseDelta.x, m_pan.y + io.MouseDelta.y };
        }

        // Background and grid, clipped to the canvas.
        m_draw = ImGui::GetWindowDrawList();
        const ImVec2 end{ m_origin.x + m_size.x, m_origin.y + m_size.y };
        m_draw->PushClipRect(m_origin, end, true);
        m_draw->AddRectFilled(m_origin, end, kBackground);

        const float step = kGridStep * m_zoom;
        for (float x = std::fmod(m_pan.x, step); x < m_size.x; x += step) m_draw->AddLine({ m_origin.x + x, m_origin.y }, { m_origin.x + x, end.y }, kGridLine);
        for (float y = std::fmod(m_pan.y, step); y < m_size.y; y += step) m_draw->AddLine({ m_origin.x, m_origin.y + y }, { end.x, m_origin.y + y }, kGridLine);

    }   // GraphCanvas::Begin()

    void GraphCanvas::End() {

        if (m_draw) m_draw->PopClipRect();
        m_draw = nullptr;

    }   // GraphCanvas::End()

    ImVec2 GraphCanvas::ToScreen(const Math::vec2& graph) const {

        return { m_origin.x + m_pan.x + graph.x * m_zoom, m_origin.y + m_pan.y + graph.y * m_zoom };

    }   // GraphCanvas::ToScreen()

    Math::vec2 GraphCanvas::ToGraph(const ImVec2& screen) const {

        return { (screen.x - m_origin.x - m_pan.x) / m_zoom, (screen.y - m_origin.y - m_pan.y) / m_zoom };

    }   // GraphCanvas::ToGraph()

    void GraphCanvas::FrameBox(const Math::vec2& min, const Math::vec2& max) {

        m_frameRequested = true;
        m_frameMin = min;
        m_frameMax = max;

    }   // GraphCanvas::FrameBox()

    ImVec2 GraphCanvas::Mouse() const {

        return ImGui::GetIO().MousePos;

    }   // GraphCanvas::Mouse()

    void GraphCanvas::DrawNode(const ImVec2& topLeft, const ImVec2& size, ImU32 header, const char* title, const char* subtitle,
                               bool selected, bool dim) const {

        const ImVec2 bottomRight{ topLeft.x + size.x, topLeft.y + size.y };
        const float  rounding = 6.0f * m_zoom;
        const ImU32  body     = dim ? IM_COL32(40, 42, 48, 220) : IM_COL32(52, 56, 64, 255);
        const ImU32  border   = selected ? IM_COL32(255, 215, 120, 255) : IM_COL32(126, 141, 162, dim ? 120 : 255);

        m_draw->AddRectFilled(topLeft, bottomRight, body, rounding);

        // The header: a coloured band across the top, its colour saying what
        // kind of node it is (events, flow, navigation...).
        const float headerHeight = 22.0f * m_zoom;
        m_draw->AddRectFilled(topLeft, { bottomRight.x, topLeft.y + headerHeight }, dim ? (header & 0x80FFFFFFu) : header, rounding, ImDrawFlags_RoundCornersTop);
        m_draw->AddRect(topLeft, bottomRight, border, rounding, 0, selected ? 2.5f : 1.0f);

        // Text clipped to the box, so a long summary never spills over.
        const ImVec4 clip{ topLeft.x + 4.0f, topLeft.y, bottomRight.x - 4.0f, bottomRight.y };
        ImFont* font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize() * m_zoom;
        m_draw->AddText(font, fontSize, { topLeft.x + 8.0f * m_zoom, topLeft.y + 4.0f * m_zoom }, IM_COL32(245, 245, 245, dim ? 150 : 255), title, nullptr, 0.0f, &clip);
        if (subtitle && *subtitle) {
            m_draw->AddText(font, fontSize * 0.9f, { topLeft.x + 8.0f * m_zoom, topLeft.y + headerHeight + 6.0f * m_zoom },
                            IM_COL32(200, 205, 215, dim ? 120 : 255), subtitle, nullptr, 0.0f, &clip);
        }

    }   // GraphCanvas::DrawNode()

    void GraphCanvas::DrawPort(const ImVec2& centre, bool hovered) const {

        const float radius = std::max(4.0f, 6.0f * m_zoom);
        m_draw->AddCircleFilled(centre, radius, hovered ? IM_COL32(255, 215, 110, 255) : IM_COL32(170, 180, 195, 255));
        m_draw->AddCircle(centre, radius, IM_COL32(20, 22, 26, 255), 0, 1.5f);

    }   // GraphCanvas::DrawPort()

    void GraphCanvas::DrawLink(const ImVec2& from, const ImVec2& to, ImU32 colour, float thickness) const {

        // A vertical S-curve: leaves the parent straight down, arrives at the
        // child straight down - the tree reads top to bottom.
        const float bend = std::max(30.0f * m_zoom, std::abs(to.y - from.y) * 0.5f);
        const ImVec2 arrive{ to.x, to.y - 8.0f * m_zoom };   // stop short so the arrowhead sits on the port
        m_draw->AddBezierCubic(from, { from.x, from.y + bend }, { arrive.x, arrive.y - bend }, arrive, colour, thickness);

        const float head = 7.0f * m_zoom;
        m_draw->AddTriangleFilled({ to.x, to.y }, { to.x - head, to.y - head * 1.4f }, { to.x + head, to.y - head * 1.4f }, colour);

    }   // GraphCanvas::DrawLink()

    void GraphCanvas::DrawLabel(const ImVec2& position, ImU32 colour, const char* text, float scale) const {

        m_draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * m_zoom * scale, position, colour, text);

    }   // GraphCanvas::DrawLabel()

}   // NAMESPACE CRUCIBLE
