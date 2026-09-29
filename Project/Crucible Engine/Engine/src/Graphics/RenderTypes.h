#pragma once

#include <cstdint>
#include <vector>

#include "Math/Math.h"
#include "Vulkan/VulkanPipeline.h"   // for Vertex (adjust if Vertex lives elsewhere)

namespace Crucible {

    // ------------------------------------------------------------------------------------------ //
    //  Viewports (offscreen render targets)
    // ------------------------------------------------------------------------------------------ //

    // Opaque handle to an offscreen render target the Renderer draws the scene into, so it can be
    // displayed inside an ImGui panel (Scene view, Game view, asset thumbnails, ...) instead of
    // going straight to the window. The Renderer knows nothing about ImGui; it just exposes the
    // resulting sampled image + sampler for whoever owns the handle to hand to
    // ImGui_ImplVulkan_AddTexture().
    struct ViewportHandle {
        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;
        bool IsValid() const { return id != Invalid; }
        friend bool operator==(ViewportHandle a, ViewportHandle b) { return a.id == b.id; }
    };

    // ------------------------------------------------------------------------------------------ //
    //  Mesh resources
    // ------------------------------------------------------------------------------------------ //

    // Opaque handle to a mesh living on the GPU. Cheap to copy; this is what ECS components store.
    struct MeshHandle {
        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;
        bool IsValid() const { return id != Invalid; }
        friend bool operator==(MeshHandle a, MeshHandle b) { return a.id == b.id; }
        friend bool operator<(MeshHandle a, MeshHandle b)  { return a.id <  b.id; }
    };

    // CPU-side mesh description (from a model loader, procedural generator, etc.).
    struct MeshData {
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
    };

    // ------------------------------------------------------------------------------------------ //
    //  Per-frame data
    // ------------------------------------------------------------------------------------------ //

    // One draw request. The ECS extraction step builds these every frame.
    // Later: add a MaterialHandle here and sort by (material, mesh).
    struct RenderItem {
        MeshHandle mesh;
        glm::mat4  model{ 1.0f };
    };

    // Uploaded once per frame in flight (descriptor set 0, binding 0). "model" is no longer here;
    // it is sent per draw as a push constant so any number of objects can share one UBO.
    struct FrameUniforms {
        glm::mat4 view{ 1.0f };
        glm::mat4 proj{ 1.0f };
    };

    // Per-draw push constant (64 bytes, well under the 128-byte guaranteed minimum).
    struct PushConstants {
        glm::mat4 model{ 1.0f };
    };

    // ------------------------------------------------------------------------------------------ //
    //  Built-in primitives (replaces the hard-coded cube that used to live in Renderer)
    // ------------------------------------------------------------------------------------------ //
    namespace Primitives {

        inline MeshData MakeCube() {
            // NOTE: reuses one vertex per corner, so normals are approximated as the corner's
            // direction from the centre rather than true flat face normals (which need each
            // corner duplicated per face). Fine for a debug/test primitive; a real cube primitive
            // for shading should duplicate verts per face like a glTF exporter would.
            auto v = [](glm::vec3 pos, glm::vec3 color) {
                return Vertex{ pos, glm::normalize(pos), glm::vec2(0.0f), color };
            };
            return MeshData{
                {
                    v({-0.5f, -0.5f, -0.5f}, {0.1f, 0.5f, 0.5f}),
                    v({ 0.5f, -0.5f, -0.5f}, {0.1f, 0.5f, 0.5f}),
                    v({ 0.5f,  0.5f, -0.5f}, {0.1f, 0.5f, 0.5f}),
                    v({-0.5f,  0.5f, -0.5f}, {0.1f, 0.5f, 0.5f}),
                    v({-0.5f, -0.5f,  0.5f}, {0.1f, 0.5f, 0.5f}),
                    v({ 0.5f, -0.5f,  0.5f}, {0.1f, 0.5f, 0.5f}),
                    v({ 0.5f,  0.5f,  0.5f}, {0.1f, 0.5f, 0.5f}),
                    v({-0.5f,  0.5f,  0.5f}, {0.1f, 0.5f, 0.5f})
                },
                {
                    0, 3, 2,  0, 2, 1,   // back   (z = -0.5)
                    4, 5, 6,  4, 6, 7,   // front  (z = +0.5)
                    0, 4, 7,  0, 7, 3,   // left   (x = -0.5)
                    1, 2, 6,  1, 6, 5,   // right  (x = +0.5)
                    0, 1, 5,  0, 5, 4,   // bottom (y = -0.5)
                    3, 7, 6,  3, 6, 2    // top    (y = +0.5)
                }
            };
        }

    }   // NAMESPACE Primitives

}   // NAMESPACE CRUCIBLE
