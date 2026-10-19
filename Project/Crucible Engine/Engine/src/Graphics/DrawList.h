/*************************************************************************
  \file       DrawList.h
  \brief
    The units of drawing the Renderer hands to its render passes.
  \details
    Moved out of the Renderer class so passes (Graphics/Passes/) can draw
    meshes without knowing about the Renderer.
*************************************************************************/
#pragma once

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
    #include <vulkan/vulkan_raii.hpp>
#else
    import vulkan_hpp;
#endif

#include "MeshHandle.h"
#include "MaterialHandle.h"
#include "TextureHandle.h"
#include "RenderTypes.h"

#include <cstdint>
#include <functional>
#include <array>
#include <vector>

namespace Crucible {

    // One instanced draw: `instanceCount` copies of a mesh, whose per-object data starts at
    // `firstInstance` in the frame's instance buffer.
    struct DrawBatch {
        MeshHandle     mesh;
        MaterialHandle material;
        uint32_t       firstInstance = 0;
        uint32_t       instanceCount = 0;
    };

    // One particle draw: `count` particles from `first` on in this frame's particle buffer,
    // all with the same texture and blend mode (one emitter).
    struct ParticleBatch {
        TextureHandle texture;
        uint32_t      first = 0;
        uint32_t      count = 0;
        ParticlePush  push;
    };

    // One water surface (WaterPass): its mesh, drawn with its own settings and transform.
    struct WaterDraw {
        MeshHandle mesh;
        WaterPush  push;
    };

    // Everything one view draws this frame, built by PrepareView() before recording starts
    // (so the instance buffer's final size is known before any command references it).
    struct ViewDrawList {
        std::vector<DrawBatch>                                main;
        std::array<std::vector<DrawBatch>, kMaxShadowCascades> shadow;
        uint32_t                                              cascadeCount = 0;   // 0 = no shadow pass
        std::vector<DrawBatch>                                outline;            // selected object (mask pass)
        std::vector<ParticleBatch>                            particles;          // far to near
        std::vector<WaterDraw>                                water;              // water surfaces in view (drawn after the opaque scene)
        glm::vec4                                             projInfo{ 1.0f };   // proj[0][0], [1][1], [2][2], [3][2] (ambient occlusion)
        void Clear() { main.clear(); for (auto& c : shadow) { c.clear(); } cascadeCount = 0; outline.clear(); particles.clear(); water.clear(); }
    };

}   // NAMESPACE CRUCIBLE
