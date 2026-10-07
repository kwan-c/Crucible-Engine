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

}   // NAMESPACE CRUCIBLE
