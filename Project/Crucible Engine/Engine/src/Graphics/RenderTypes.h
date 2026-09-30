#pragma once

#include <cstdint>
#include <vector>

#include "Math/Math.h"
#include "MeshHandle.h"
#include "TextureHandle.h"
#include "MaterialHandle.h"
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

    // MeshHandle now lives in Graphics/MeshHandle.h (no Vulkan dependency, so the ECS can include
    // it directly - see ECS/Components/Components.h's MeshRenderer). Still declared/used here
    // exactly as before.

    // CPU-side mesh description (from a model loader, procedural generator, etc.).
    struct MeshData {
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
    };

    // ------------------------------------------------------------------------------------------ //
    //  Per-frame data
    // ------------------------------------------------------------------------------------------ //

    // One draw request. The ECS extraction step builds these every frame, then Update() sorts
    // the queue by (material, mesh) before DrawRenderQueue() consumes it, so consecutive draws
    // rebind the material/mesh descriptor+buffers as rarely as possible.
    struct RenderItem {
        MeshHandle     mesh;
        MaterialHandle material;
        glm::mat4      model{ 1.0f };
    };

    // Uploaded once per frame in flight (descriptor set 0, binding 0). "model" is no longer here;
    // it is sent per draw as a push constant so any number of objects can share one UBO.
    // cameraPosWS is needed fragment-side for the PBR specular term's view vector; it's a vec4
    // (xyz used, w unused) rather than a vec3 purely for std140 UBO alignment - a UBO enforces
    // 16-byte alignment per member in a way a push constant/HLSL cbuffer does not, so a bare vec3
    // here (unlike MaterialFactors' vec3 fields, which are fine in a push constant) would silently
    // misalign whatever member followed it.
    struct FrameUniforms {
        glm::mat4 view{ 1.0f };
        glm::mat4 proj{ 1.0f };
        glm::vec4 cameraPosWS{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    // ------------------------------------------------------------------------------------------ //
    //  Materials
    // ------------------------------------------------------------------------------------------ //

    // A material's scalar (non-textured) knobs. Shared verbatim between the C++ push-constant
    // struct below and the shader's own MaterialFactors (DefaultShader2.slang) - keep both in
    // sync by hand, same as the rest of this file's structs (no reflection/codegen here).
    // Field order matters: it must pack identically under HLSL's cbuffer rules (a float3 followed
    // by a float shares one 16-byte slot) as it does under plain C++ struct layout, which is true
    // for exactly this order (see PushConstants' static_assert below).
    struct MaterialFactors {
        glm::vec4 albedoTint{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec3 emissiveFactor{ 0.0f, 0.0f, 0.0f };
        float     metallicFactor  = 1.0f;
        float     roughnessFactor = 1.0f;
    };

    // What Renderer::CreateMaterial()/UpdateMaterial() take: the five PBR texture slots (any of
    // which may be left Invalid - the Renderer substitutes a neutral placeholder texture so the
    // shader never has to branch on "is this map present") plus the scalar factors above. glTF's
    // metallic-roughness model names below match the spec directly (metallicRoughness packs
    // roughness in G, metalness in B; ambient occlusion reads R of `occlusion`).
    struct MaterialData {
        TextureHandle    albedo{};
        TextureHandle    normal{};
        TextureHandle    metallicRoughness{};
        TextureHandle    emissive{};
        TextureHandle    occlusion{};
        MaterialFactors  factors{};
    };

    // Per-draw push constant: the model matrix (set 0's camera UBO already carries view/proj) plus
    // the current material's scalar factors, so a designer dragging a tint/roughness slider in the
    // Inspector is visible next frame with no GPU synchronization at all - unlike the five texture
    // bindings in the material's descriptor set (set 1), which live in real GPU-resident descriptor
    // state and need a (deliberately heavier-handed, editor-only) wait-idle to swap safely; see
    // Renderer::UpdateMaterial()'s comment.
    struct PushConstants {
        glm::mat4        model{ 1.0f };
        MaterialFactors  factors{};
    };
    static_assert(sizeof(PushConstants) <= 128,
        "PushConstants must fit in the 128-byte push-constant range every Vulkan implementation "
        "is required to support (VkPhysicalDeviceLimits::maxPushConstantsSize's guaranteed floor).");

    // ------------------------------------------------------------------------------------------ //
    //  Built-in primitives (Cube, Plane, Disc, Cylinder, Cone, Pyramid, Sphere, Capsule) have moved
    //  to Graphics/Primitives.h to keep this header lean - see that file for the Primitives
    //  namespace. Include "Primitives.h" (it includes this header itself) wherever you
    //  need Primitives::MakeXxx().


}   // NAMESPACE CRUCIBLE
