#pragma once
#include <cstdint>
namespace Crucible {
    // Opaque handle to a Material living on the GPU (see Renderer::CreateMaterial) - a bundle of
    // texture bindings (albedo/normal/metallic-roughness/emissive/occlusion) plus scalar factors,
    // exposed to the ECS as this one handle. Mirrors MeshHandle.h/TextureHandle.h's pattern; see
    // ECS/Components/Components.h's MeshRenderer::material for the ECS-visible use of this type.
    struct MaterialHandle {
        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;
        bool IsValid() const { return id != Invalid; }
        friend bool operator==(MaterialHandle a, MaterialHandle b) { return a.id == b.id; }
        friend bool operator<(MaterialHandle a, MaterialHandle b)  { return a.id <  b.id; }
    };
}
