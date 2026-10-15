/*************************************************************************
  \file       UITestOverlay.h
  \brief
    A built-in check of the screen-space UI path (Renderer::
    SetUITestOverlay): a panel of shapes, colours, clipped stripes,
    distance-field circles and placeholder text, submitted every frame as
    the game's UI, so it shows in the Game view and the game window.
  \details
    Also a small worked example of building a UIDrawList: quads, indices,
    commands that are merged while texture, mode and clip stay the same,
    text from stb_easy_font (no font file needed), and a texture made
    with CreateTextureFromPixels.
*************************************************************************/
#pragma once

#include "Graphics/UITypes.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Crucible {

    class Renderer;

    class UITestOverlay {
    public:
        /*********************************************************************
         \brief  Builds this frame's overlay at the game screen's size and
                 submits it (Renderer::SubmitUI). Makes its textures the
                 first time. Does nothing while the size is 0 (no Game view).
         \param[in] renderer  The renderer to submit to.
         \param[in] time      Seconds, for the moving stripes.
         *********************************************************************/
        void Submit(Renderer& renderer, float time);

    private:
        void MakeTextures(Renderer& renderer);

        /// One quad from (x0, y0) to (x1, y1) with uvs (u0, v0)-(u1, v1).
        void Quad(float x0, float y0, float x1, float y1, uint32_t colour,
                  TextureHandle texture = {}, UIDrawMode mode = UIDrawMode::Image,
                  const glm::vec4& uv = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
        /// A solid rectangle outline, `thickness` pixels wide, inside (x0, y0)-(x1, y1).
        void Frame(float x0, float y0, float x1, float y1, float thickness, uint32_t colour);
        /// Placeholder text (stb_easy_font: 12 units per line, scaled).
        void Text(float x, float y, float scale, uint32_t colour, const std::string& text);
        /// Width of `text` at `scale`, in pixels.
        static float TextWidth(const std::string& text, float scale);
        /// Appends six indices for the quad starting at `base`, extending the last command when
        /// the texture, mode and clip rectangle are unchanged.
        void AddQuadIndices(uint32_t base, TextureHandle texture, UIDrawMode mode);

        std::vector<UIVertex>      m_vertices;
        std::vector<uint32_t>      m_indices;
        std::vector<UIDrawCommand> m_commands;
        std::vector<char>          m_fontScratch;
        glm::vec4                  m_clip{ -1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f };   // applied to quads added while set

        bool          m_texturesMade = false;
        TextureHandle m_sdfCircle;       // Alpha8 signed distance field of a circle
        TextureHandle m_sdfCircleRgba;   // the same distances in all four channels (Msdf mode reads the median)
    };

}   // NAMESPACE CRUCIBLE
