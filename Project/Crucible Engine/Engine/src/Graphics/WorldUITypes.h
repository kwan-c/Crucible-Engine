/*************************************************************************
  \file       WorldUITypes.h
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    World-space UI as the Renderer takes it (Renderer::SubmitWorldUI) and
    WorldUIPass draws it (WorldUI.slang). No Vulkan in here.
  \details
    The unit is a PANEL: something flat placed in the world - one WorldText
    (WorldTextSystem), or one world-space UICanvas with everything on it
    (its images, sliders and text). A panel is submitted as:
      - its quad corners (WorldUIVertex), six per quad, in the panel's own
        plane: world units from the anchor, +x right, +y UP. Made once per
        frame on the CPU and shared by every view.
      - the runs of those vertices that share a texture, mode and effects
        (WorldUIRun) - one draw each, in the order given (so the panel's own
        layering is kept: what comes later is drawn over what came before).
      - where the plane is (WorldUIDesc): the anchor and the plane's axes
        in the world, with the Transform's rotation and scale in them, plus
        how it faces the camera. Facing is done in the vertex shader, so the
        Scene and Game views each see it facing their own camera.
    Panels are sorted against each other far to near, as wholes.
*************************************************************************/
#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "Graphics/TextureHandle.h"
#include "Graphics/UITypes.h"   // UIDrawMode, UITextEffects

namespace Crucible {

    /// One corner of a glyph quad. Must match WorldUI.slang's VSInput and the vertex input in WorldUIPass.cpp.
    struct WorldUIVertex {
        glm::vec2 local{ 0.0f };                 ///< In the panel's plane: world units from the anchor, +x right, +y up.
        glm::vec2 uv{ 0.0f };                    ///< Atlas coordinates (0,0 = top-left).
        uint32_t  colour = 0xFFFFFFFFu;          ///< RGBA8, sRGB, red in the lowest byte (PackUIColour).
    };
    static_assert(sizeof(WorldUIVertex) == 20, "WorldUIVertex must match the world UI pipeline's vertex layout.");

    /// How the panel's plane is turned (WorldText::Billboard, UICanvas's). Must match WorldUI.slang (modes.y).
    enum class WorldUIFacing : uint32_t {
        Transform  = 0,   ///< As the Transform has it.
        FaceCamera = 1,   ///< Square on to each camera.
        Upright    = 2,   ///< Straight up, turned around Y toward each camera.
    };

    /// A run of one panel's vertices drawn together.
    struct WorldUIRun {
        TextureHandle texture{};                 ///< Invalid = plain white (underlines, untextured images).
        UIDrawMode    mode = UIDrawMode::Image;
        float         pixelRange = 0.0f;         ///< Distance-field atlases: the field's range in texels.
        UITextEffects effects{};                 ///< Distance-field modes only.
        uint32_t      firstVertex = 0;           ///< Into the vertices submitted with it.
        uint32_t      vertexCount = 0;           ///< A multiple of 3.
    };

    /// Where one panel is and how it is drawn.
    struct WorldUIDesc {
        glm::vec3     anchor{ 0.0f };                ///< The plane's origin in the world (the entity's position).
        glm::vec3     right{ 1.0f, 0.0f, 0.0f };     ///< The plane's +x axis in the world, scale included.
        glm::vec3     up{ 0.0f, 1.0f, 0.0f };        ///< The plane's +y axis in the world, scale included.
        float         radius = 0.0f;                 ///< Every vertex is within this of the anchor (culling), any facing.
        WorldUIFacing facing = WorldUIFacing::Transform;
        bool          alwaysOnTop = false;           ///< No depth test.
        bool          doubleSided = true;            ///< false: not drawn from behind (back faces culled).
        float         intensity = 1.0f;              ///< Multiplies the colour (HDR: > 1 blooms).
        float         fadeStart = 0.0f;              ///< Distance fade: fully visible up to fadeStart, gone at fadeEnd.
        float         fadeEnd = 0.0f;                ///< fadeEnd <= fadeStart: no fade.
    };

    /// Push constant of the world UI pipeline. Must match WorldUI.slang's WorldUIPush, 112 bytes.
    struct WorldUIPush {
        glm::vec4  anchor{ 0.0f };   ///< xyz = anchor, w = opacity (distance fade)
        glm::vec4  right{ 0.0f };    ///< xyz = +x axis, w = intensity
        glm::vec4  up{ 0.0f };       ///< xyz = +y axis, w = pixel range
        glm::uvec4 modes{ 0u };      ///< x = UIDrawMode, y = WorldUIFacing, z = flags (bit 0: shadow pass)
        glm::uvec4 colours{ 0u };    ///< x = outline colour, y = glow colour
        glm::vec4  widths{ 0.0f };   ///< x = outline width, y = glow width, z = glow softness, w = dilate
        glm::vec4  extra{ 0.0f };    ///< x = shadow softness
    };
    static_assert(sizeof(WorldUIPush) == 112, "WorldUIPush must match WorldUI.slang.");

    /// One draw in a view's list (WorldUIPass::Prepare), far to near.
    struct WorldUIDraw {
        TextureHandle texture{};
        uint32_t      firstVertex = 0;           ///< Into the frame's world UI vertex buffer.
        uint32_t      vertexCount = 0;
        bool          alwaysOnTop = false;
        bool          doubleSided = true;
        WorldUIPush push{};
    };

}   // NAMESPACE CRUCIBLE
