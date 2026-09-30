#pragma once

#include <cstdint>

namespace Crucible {

    // Opaque handle to a mesh living on the GPU (see RenderSystem.h's CreateMesh/CreateMeshAsync).
    // Cheap to copy; this is what ECS components (MeshRenderer, Components.h) store.
    //
    // Split out from RenderTypes.h into its own header on purpose: RenderTypes.h pulls in
    // Vulkan/VulkanPipeline.h (and, through it, the Vulkan RAII headers) for the Vertex/MeshData
    // types, and ECS/Components/Components.h needs to reference MeshHandle WITHOUT dragging any
    // of that into every ECS header that includes Components.h. This header has zero renderer
    // dependencies - just an integer - so it's safe for the ECS to depend on directly.
    struct MeshHandle {
        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;
        bool IsValid() const { return id != Invalid; }
        friend bool operator==(MeshHandle a, MeshHandle b) { return a.id == b.id; }
        friend bool operator<(MeshHandle a, MeshHandle b)  { return a.id <  b.id; }
    };

}   // NAMESPACE CRUCIBLE
