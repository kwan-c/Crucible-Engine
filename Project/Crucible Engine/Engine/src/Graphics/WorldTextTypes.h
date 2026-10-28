/*************************************************************************
  \file       WorldTextTypes.h
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    What WorldTextSystem hands the Renderer for each WorldText, and what
    WorldTextPass draws it with (WorldText.slang). No Vulkan in here.
  \details
    One piece of text is submitted as:
      - its glyph corners (WorldTextVertex), six per quad, in the text's own
        plane: world units from the anchor, +x right, +y UP. Made once per
        frame on the CPU and shared by every view.
      - the runs of those vertices that share a texture, mode and effects
        (WorldTextRun) - one draw each.
      - where the plane is (WorldTextDesc): the anchor and the plane's axes
        in the world, with the Transform's rotation and scale in them, plus
        how it faces the camera. Facing is done in the vertex shader, so the
        Scene and Game views each see it facing their own camera.
*************************************************************************/
#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "Graphics/TextureHandle.h"
#include "Graphics/UITypes.h"   // UIDrawMode, UITextEffects

namespace Crucible {

    /// One corner of a glyph quad. Must match WorldText.slang's VSInput and the vertex input in WorldTextPass.cpp.
    struct WorldTextVertex {
        glm::vec2 local{ 0.0f };                 ///< In the text's plane: world units from the anchor, +x right, +y up.
        glm::vec2 uv{ 0.0f };                    ///< Atlas coordinates (0,0 = top-left).
        uint32_t  colour = 0xFFFFFFFFu;          ///< RGBA8, sRGB, red in the lowest byte (PackUIColour).
    };
    static_assert(sizeof(WorldTextVertex) == 20, "WorldTextVertex must match the world text pipeline's vertex layout.");

    /// How the text's plane is turned (WorldText::Billboard). Must match WorldText.slang (modes.y).
    enum class WorldTextFacing : uint32_t {
        Transform  = 0,   ///< As the Transform has it.
        FaceCamera = 1,   ///< Square on to each camera.
        Upright    = 2,   ///< Straight up, turned around Y toward each camera.
    };

    /// A run of one text's vertices drawn together.
    struct WorldTextRun {
        TextureHandle texture{};                 ///< Invalid = plain white (underlines).
        UIDrawMode    mode = UIDrawMode::Image;
        float         pixelRange = 0.0f;         ///< Distance-field atlases: the field's range in texels.
        UITextEffects effects{};                 ///< Distance-field modes only.
        uint32_t      firstVertex = 0;           ///< Into the vertices submitted with it.
        uint32_t      vertexCount = 0;           ///< A multiple of 3.
    };

    /// Where one piece of text is and how it is drawn.
    struct WorldTextDesc {
        glm::vec3       anchor{ 0.0f };          ///< The plane's origin in the world (the entity's position).
        glm::vec3       right{ 1.0f, 0.0f, 0.0f };   ///< The plane's +x axis in the world, scale included.
        glm::vec3       up{ 0.0f, 1.0f, 0.0f };      ///< The plane's +y axis in the world, scale included.
        float           radius = 0.0f;           ///< Every vertex is within this of the anchor (culling), any facing.
        WorldTextFacing facing = WorldTextFacing::Transform;
        bool            alwaysOnTop = false;     ///< No depth test.
        bool            doubleSided = true;      ///< false: not drawn from behind (back faces culled).
        float           intensity = 1.0f;        ///< Multiplies the colour (HDR: > 1 blooms).
        float           fadeStart = 0.0f;        ///< Distance fade: fully visible up to fadeStart, gone at fadeEnd.
        float           fadeEnd = 0.0f;          ///< fadeEnd <= fadeStart: no fade.
    };

    /// Push constant of the world text pipeline. Must match WorldText.slang's WorldTextPush, 112 bytes.
    struct WorldTextPush {
        glm::vec4  anchor{ 0.0f };   ///< xyz = anchor, w = opacity (distance fade)
        glm::vec4  right{ 0.0f };    ///< xyz = +x axis, w = intensity
        glm::vec4  up{ 0.0f };       ///< xyz = +y axis, w = pixel range
        glm::uvec4 modes{ 0u };      ///< x = UIDrawMode, y = WorldTextFacing, z = flags (bit 0: shadow pass)
        glm::uvec4 colours{ 0u };    ///< x = outline colour, y = glow colour
        glm::vec4  widths{ 0.0f };   ///< x = outline width, y = glow width, z = glow softness, w = dilate
        glm::vec4  extra{ 0.0f };    ///< x = shadow softness
    };
    static_assert(sizeof(WorldTextPush) == 112, "WorldTextPush must match WorldText.slang.");

    /// One draw in a view's list (WorldTextPass::Prepare), far to near.
    struct WorldTextDraw {
        TextureHandle texture{};
        uint32_t      firstVertex = 0;           ///< Into the frame's world text vertex buffer.
        uint32_t      vertexCount = 0;
        bool          alwaysOnTop = false;
        bool          doubleSided = true;
        WorldTextPush push{};
    };

}   // NAMESPACE CRUCIBLE
