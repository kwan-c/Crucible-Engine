#pragma once
/*************************************************************************
  \file       UITypes.h
  \brief
    What the in-game UI hands the Renderer each frame: vertices in canvas
    pixels, indices, and draw commands (texture, mode, clip rectangle,
    index range). See Renderer::SubmitUI and Renderer::SubmitViewportUI.
  \details
    No Vulkan in here, so UI and font code can include it on its own.

    Conventions:
      - Positions are canvas pixels: origin at the top-left corner, +x to
        the right, +y down. In the Game view and the game window, one
        canvas pixel is one screen pixel (lay out against
        Renderer::GetGameScreenSize()). A viewport can map them differently
        with Renderer::SetViewportUITransform (the UI editor's pan / zoom).
      - Colours are sRGB, as a designer picks them; the shader converts
        them to linear. Alpha is not converted.
      - Blending is ordinary (straight) alpha: colour = src * a + dst * (1 - a).
      - Draw commands are drawn in the order given, and lists in the order
        they were submitted, so later ones draw on top.
      - Data is copied when submitted, and forgotten after the frame
        (like Renderer::Submit), so submit every frame.
*************************************************************************/

#include "Graphics/TextureHandle.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace Crucible {

    /// One UI vertex, 20 bytes. See the file header for the coordinate and colour conventions.
    struct UIVertex {
        glm::vec2 position{ 0.0f };          ///< Canvas pixels.
        glm::vec2 uv{ 0.0f };                ///< Texture coordinates, (0, 0) = the texture's top-left corner.
        uint32_t  colour = 0xFFFFFFFFu;      ///< RGBA8, sRGB, red in the lowest byte (see PackUIColour).
    };
    static_assert(sizeof(UIVertex) == 20, "UIVertex must match the UI pipeline's vertex layout (UIPass.cpp, UI.slang).");

    /// Packs an sRGB colour (0-255 per channel) into UIVertex::colour.
    constexpr uint32_t PackUIColour(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    }

    /// Packs an sRGB colour (0-1 per channel) into UIVertex::colour.
    inline uint32_t PackUIColour(const glm::vec4& srgb) {
        const glm::vec4 c = glm::clamp(srgb, 0.0f, 1.0f) * 255.0f + 0.5f;
        return PackUIColour(static_cast<uint8_t>(c.r), static_cast<uint8_t>(c.g), static_cast<uint8_t>(c.b), static_cast<uint8_t>(c.a));
    }

    /// How a draw command's texture is turned into colour.
    enum class UIDrawMode : uint32_t {
        Image = 0,   ///< texel * vertex colour. Images, solid shapes (no texture), and coverage text from an Alpha8 atlas.
        Sdf   = 1,   ///< Signed distance text: distance in the texel's alpha (an Alpha8 atlas works), edge at 0.5.
        Msdf  = 2,   ///< Multi-channel signed distance text: distance = median of red, green, blue (an RGBA8 atlas), edge at 0.5.
    };

    /// One draw: a run of the list's indices with one texture, mode and clip rectangle.
    struct UIDrawCommand {
        TextureHandle texture{};                                         ///< Invalid = plain white (solid colour).
        UIDrawMode    mode = UIDrawMode::Image;
        glm::vec4     clipRect{ -1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f };      ///< Min x, min y, max x, max y in canvas pixels. Default: no clipping.
        uint32_t      firstIndex = 0;                                    ///< Into UIDrawList::indices.
        uint32_t      indexCount = 0;                                    ///< A multiple of 3 (triangles).
        int32_t       vertexOffset = 0;                                  ///< Added to each index, so meshes can share an index pattern.
    };

    /*************************************************************************
     \brief
        One frame's UI, as views into the caller's arrays (copied on submit,
        so they only need to live for the call).
     \details
        A command whose indices point outside `vertices`, or whose index
        range runs past `indices`, is dropped with a warning.
     *************************************************************************/
    struct UIDrawList {
        std::span<const UIVertex>      vertices;
        std::span<const uint32_t>      indices;
        std::span<const UIDrawCommand> commands;
    };

}   // NAMESPACE CRUCIBLE
