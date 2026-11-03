/*************************************************************************
* \file:         GraphCanvas.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                GraphCanvas: the shared background of the node editors -
*                grid, pan, zoom, and drawing node boxes, ports and links.
*
* \details
*                Grown from the Animator panel's hand-drawn canvas. It only
*                draws and moves the view; what the nodes ARE and what a
*                click means is up to the view using it (BehaviorTreeView
*                now, the state graph view next), so both editors look and
*                feel the same.
*
*                Positions come in two spaces: GRAPH space (what is saved,
*                in canvas units) and SCREEN space (pixels). ToScreen and
*                ToGraph convert; zoom and pan only change the mapping.
*
*                Middle-drag pans, the wheel zooms around the mouse.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// ImGui
#include <imgui.h>

// Crucible engine
#include "Math/Math.h"

namespace Crucible {

    class GraphCanvas {

    public:

        /*!
            \brief
            Starts the canvas over the rest of the current window: background,
            grid, and this frame's pan and zoom. Draw nodes between Begin and
            End. The canvas is one invisible ImGui item, so the clicks the
            view reads (Clicked, Released...) are clicks on empty canvas or on
            whatever the view draws there.
        */
        void Begin(const char* id);
        void End();

        // --- Space conversion -------------------------------------------- //

        ImVec2     ToScreen(const Math::vec2& graph) const;
        Math::vec2 ToGraph(const ImVec2& screen) const;
        float      Zoom() const { return m_zoom; }
        ImVec2     Scaled(const Math::vec2& graphSize) const { return { graphSize.x * m_zoom, graphSize.y * m_zoom }; }

        // Pans and zooms so the graph-space box [min, max] fills the canvas
        // (with a margin). Takes effect next frame (it needs the canvas size).
        void FrameBox(const Math::vec2& min, const Math::vec2& max);

        // --- This frame's mouse, on the canvas ---------------------------- //

        bool       Hovered() const { return m_hovered; }
        ImVec2     Mouse() const;
        Math::vec2 MouseGraph() const { return ToGraph(Mouse()); }
        bool       LeftClicked() const { return m_leftClicked; }
        bool       RightClicked() const { return m_rightClicked; }
        bool       LeftHeld() const { return m_leftHeld; }
        bool       LeftReleased() const { return m_leftReleased; }

        // --- Drawing ------------------------------------------------------- //

        ImDrawList* DrawList() const { return m_draw; }

        /*!
            \brief
            A node: a rounded box with a coloured header line, a title and a
            smaller second line, scaled with the zoom. `dim` draws it faded
            (not connected to the tree).
        */
        void DrawNode(const ImVec2& topLeft, const ImVec2& size, ImU32 header, const char* title, const char* subtitle,
                      bool selected, bool dim) const;

        // A port: the dot links start from and end at.
        void DrawPort(const ImVec2& centre, bool hovered) const;

        // A curved link from `from` down to `to`, with an arrowhead at `to`.
        void DrawLink(const ImVec2& from, const ImVec2& to, ImU32 colour, float thickness) const;

        // Text at a screen position, scaled with the zoom. (Not "DrawText":
        // windows.h turns that name into DrawTextA in any file that includes it.)
        void DrawLabel(const ImVec2& position, ImU32 colour, const char* text, float scale = 1.0f) const;

    private:

        ImVec2      m_pan{ 60.0f, 60.0f };   // where graph (0, 0) is, in pixels from the canvas's corner
        float       m_zoom = 1.0f;
        ImVec2      m_origin{};              // the canvas's top-left corner on screen
        ImVec2      m_size{};
        ImDrawList* m_draw = nullptr;

        bool        m_hovered      = false;
        bool        m_leftClicked  = false;
        bool        m_rightClicked = false;
        bool        m_leftHeld     = false;
        bool        m_leftReleased = false;

        bool        m_frameRequested = false;
        Math::vec2  m_frameMin{ 0.0f };
        Math::vec2  m_frameMax{ 0.0f };

    };  // GRAPHCANVAS CLASS

}   // NAMESPACE CRUCIBLE
