#pragma once
#include <cstdint>
namespace Crucible {
    // Opaque handle to a 2D texture living on the GPU (see Renderer::CreateTexture). Cheap to
    // copy. Mirrors MeshHandle.h's pattern exactly, including why it's split into its own
    // zero-dependency header: Material (below) is referenced from ECS/Components/Components.h,
    // which must not transitively pull in Vulkan.
    struct TextureHandle {
        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;
        bool IsValid() const { return id != Invalid; }
        friend bool operator==(TextureHandle a, TextureHandle b) { return a.id == b.id; }
        friend bool operator<(TextureHandle a, TextureHandle b)  { return a.id <  b.id; }
    };
}
