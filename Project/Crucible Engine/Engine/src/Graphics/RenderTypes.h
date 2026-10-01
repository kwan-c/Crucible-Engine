#pragma once
/*************************************************************************
* \file:         RenderTypes.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Plain-data types shared across the renderer: viewport
*                handles, mesh/frame/material data, and the per-draw push
*                constant.
*
* \details
*                Declares ViewportHandle, MeshData, RenderItem,
*                FrameUniforms, MaterialFactors, MaterialData, and
*                PushConstants - the CPU-side shapes that mirror what the
*                GPU actually consumes (uniform buffer layout, push
*                constant layout, shader-side MaterialFactors in
*                DefaultShader2.slang). Built-in procedural primitives
*                (Cube, Plane, Disc, Cylinder, Cone, Pyramid, Sphere,
*                Capsule) live in Graphics/Primitives.h to keep this header
*                lean - include Primitives.h (it includes this header
*                itself) wherever Primitives::MakeXxx() is needed.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstddef>   // offsetof
#include <cstdint>
#include <vector>

#include "Math/Math.h"
#include "MeshHandle.h"
#include "TextureHandle.h"
#include "MaterialHandle.h"
#include "CubemapHandle.h"
#include "LightTypes.h"
#include "Vulkan/VulkanPipeline.h"   // for Vertex (adjust if Vertex lives elsewhere)

namespace Crucible {

    // ------------------------------------------------------------------------------------------ //
    //  Viewports (offscreen render targets)
    // ------------------------------------------------------------------------------------------ //

    /*************************************************************************
     \brief
        Opaque handle to an offscreen render target the Renderer draws the
        scene into (Scene view, Game view, asset thumbnails, ...).

     \details
        The Renderer knows nothing about ImGui; it just exposes the
        resulting sampled image + sampler for whoever owns the handle to
        hand to ImGui_ImplVulkan_AddTexture().
     *************************************************************************/
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

    /*************************************************************************
     \brief
        CPU-side mesh description (from a model loader, procedural
        generator, etc.), ready to hand to Renderer::CreateMesh().
     *************************************************************************/
    struct MeshData {
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
    };

    // ------------------------------------------------------------------------------------------ //
    //  Per-frame data
    // ------------------------------------------------------------------------------------------ //

    /*************************************************************************
     \brief
        One draw request.

     \details
        The ECS extraction step builds these every frame, then Update()
        sorts the queue by (material, mesh) before DrawRenderQueue()
        consumes it, so consecutive draws rebind the material/mesh
        descriptor+buffers as rarely as possible.
     *************************************************************************/
    struct RenderItem {
        MeshHandle     mesh;
        MaterialHandle material;
        glm::mat4      model{ 1.0f };
    };

    /*************************************************************************
     \brief
        Uploaded once per frame in flight (descriptor set 0, binding 0).

     \details
        "model" is no longer here; it is sent per draw as a push constant
        so any number of objects can share one UBO. cameraPosWS is needed
        fragment-side for the PBR specular term's view vector; it's a vec4
        (xyz used, w unused) rather than a vec3 purely for std140 UBO
        alignment - a UBO enforces 16-byte alignment per member in a way a
        push constant/HLSL cbuffer does not, so a bare vec3 here (unlike
        MaterialFactors' vec3 fields, which are fine in a push constant)
        would silently misalign whatever member followed it. The same rule
        is why every member added for lighting below is a full vec4/uvec4.

        Lighting/environment members (filled from the frame's
        EnvironmentSettings and light queue - see Renderer::
        FillFrameUniforms()):
          - ambientSky / ambientGround: rgb = hemisphere ambient radiance
            (already multiplied by EnvironmentSettings::ambientIntensity)
            for surfaces facing straight up / straight down; the shader
            blends between them by the normal's world-space Y. w unused.
          - renderParams: x = exposure (multiplier applied before
            tonemapping), y = skybox intensity, zw unused.
          - lightParams:  x = number of valid entries in the light storage
            buffer (set 0, binding 1) this frame, y = index + 1 of the
            shadow-casting light (0 = no shadows), z = cascade count.

        Shadow members (Renderer::ComputeShadowCascades), per view:
          - shadowViewProj[i]: world -> light clip space of cascade i
          - cascadeSplits:     view-space far distance of each cascade
          - cascadeTexelSize:  world size of one shadow texel, per cascade
                               (scales the normal-offset bias)
          - shadowParams:      x = 1/resolution, y = depth bias (NDC),
                               z = normal-offset bias in texels,
                               w = fade-out fraction of the shadow distance

     \note
        DefaultShader2.slang has the full copy. Skybox, DebugLine, Shadow
        and OutlineMask declare only the prefix they read, which is valid
        as long as members are only ever appended, never reordered.
     *************************************************************************/
    constexpr uint32_t kMaxShadowCascades = 4;

    struct FrameUniforms {
        glm::mat4  view{ 1.0f };
        glm::mat4  proj{ 1.0f };
        glm::vec4  cameraPosWS{ 0.0f, 0.0f, 0.0f, 0.0f };
        glm::vec4  ambientSky{ 0.0f, 0.0f, 0.0f, 0.0f };
        glm::vec4  ambientGround{ 0.0f, 0.0f, 0.0f, 0.0f };
        glm::vec4  renderParams{ 1.0f, 1.0f, 0.0f, 0.0f };
        glm::uvec4 lightParams{ 0u, 0u, 0u, 0u };
        glm::mat4  shadowViewProj[kMaxShadowCascades]{ glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f) };
        glm::vec4  cascadeSplits{ 0.0f };
        glm::vec4  cascadeTexelSize{ 0.0f };
        glm::vec4  shadowParams{ 0.0f };
    };
    static_assert(sizeof(FrameUniforms) == 64 + 64 + 16 * 5 + 64 * kMaxShadowCascades + 16 * 3,
        "FrameUniforms must stay tightly packed in 16-byte slots to match the shaders' std140 layout.");
    static_assert(offsetof(FrameUniforms, shadowViewProj) == 208 && offsetof(FrameUniforms, shadowParams) == 496,
        "FrameUniforms offsets must match DefaultShader2.slang (checked against the compiled SPIR-V).");

    /*************************************************************************
     \brief
        One drawn object, as the vertex shaders see it (set 0, binding 2:
        StructuredBuffer<InstanceData>). 64 bytes under std430 and C++.
     \details
        Every pass (main, shadow cascades, selection mask) of every view
        appends its objects to one per-frame array; a draw covers a
        contiguous run, addressed by drawIndexed's firstInstance.
     *************************************************************************/
    struct InstanceData {
        glm::mat4 model{ 1.0f };
    };
    static_assert(sizeof(InstanceData) == 64, "InstanceData must match the shaders' std430 stride.");

    /*************************************************************************
     \brief  Push constant of the selection outline's composite pass
             (OutlineComposite.slang's CompositePush).
     *************************************************************************/
    struct OutlineCompositePush {
        glm::vec4 color{ 1.0f, 0.55f, 0.1f, 1.0f };   // sRGB-authored, a = opacity
        float     thickness = 2.0f;                   // pixels (shader clamps to 3)
        float     pad[3]{};
    };
    static_assert(sizeof(OutlineCompositePush) == 32, "OutlineCompositePush must match OutlineComposite.slang.");

    // ------------------------------------------------------------------------------------------ //
    //  Render proxies, culling, picking
    // ------------------------------------------------------------------------------------------ //

    /*************************************************************************
     \brief  Handle to a persistent render proxy (Renderer::CreateRenderProxy).
     *************************************************************************/
    struct RenderProxyHandle {
        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;
        bool IsValid() const { return id != Invalid; }
    };

    /*************************************************************************
     \brief  One view's culling result for the last frame.
     \details
        - proxies / proxiesVisible: registered proxies in the spatial index,
          and how many of them were in this view's frustum.
        - immediate / immediateVisible: the same for Submit() items.
        - movingProxies: proxies currently in the index's brute-force
          "active" array (see SpatialIndex) rather than its tree.
        - drawCalls: instanced draws of the main pass (one per run of equal
          material + mesh), versus proxiesVisible + immediateVisible objects.
        - shadowCasters / shadowDrawCalls: instances and draws over all of
          this view's shadow cascades.
     *************************************************************************/
    struct RenderCullStats {
        uint32_t proxies = 0;
        uint32_t proxiesVisible = 0;
        uint32_t immediate = 0;
        uint32_t immediateVisible = 0;
        uint32_t movingProxies = 0;
        uint32_t drawCalls = 0;
        uint32_t shadowCasters = 0;
        uint32_t shadowDrawCalls = 0;
    };

    /*************************************************************************
     \brief
        Cascaded shadow map settings for the sun (Renderer::
        GetShadowSettings()). The shadowed light is the first enabled
        directional light with castShadows (Light component), or the
        fallback sun when the scene has no lights.

     \details
        - cascadeCount: 1..kMaxShadowCascades. Each cascade re-renders its
          casters, so this is the main cost knob.
        - distance: shadows end (fading out) this far from the camera,
          clamped to the camera's far plane. Shorter = sharper.
        - splitLambda: 0 = even cascade splits, 1 = logarithmic (most
          resolution near the camera). 0.75 is a common balance.
        - casterExtension: how far toward the light, beyond a cascade's
          bounds, casters are still included (tall things off-screen).
        - depthBias: constant bias in light-space NDC depth.
        - normalBias: offset along the surface normal, in shadow texels.
        - fadeFraction: fraction of `distance` over which shadows fade out.
     *************************************************************************/
    struct ShadowSettings {
        bool     enabled = true;
        uint32_t cascadeCount = 3;
        float    distance = 80.0f;
        float    splitLambda = 0.75f;
        float    casterExtension = 100.0f;
        float    depthBias = 0.0002f;
        float    normalBias = 1.5f;
        float    fadeFraction = 0.1f;
    };

    /*************************************************************************
     \brief  Result of Renderer::Raycast().
     *************************************************************************/
    struct RenderRaycastHit {
        uint64_t   userData = 0;            //!< the proxy's userData (RenderExtractSystem: packed Entity)
        float      distance = 0.0f;         //!< along the normalized ray direction
        glm::vec3  point{ 0.0f };           //!< world-space hit point
        MeshHandle mesh;                    //!< the mesh that was hit
    };

    // ------------------------------------------------------------------------------------------ //
    //  Lights & environment
    // ------------------------------------------------------------------------------------------ //

    /*************************************************************************
     \brief
        Hard cap on lights per frame (the light storage buffer is sized
        for exactly this many GpuLights per frame in flight).

     \details
        Every fragment loops over every light (plain forward shading, no
        clustering/tiling yet), so this is also the practical performance
        ceiling. Renderer::SubmitLight() warns once and drops the rest
        beyond it.
     *************************************************************************/
    inline constexpr uint32_t kMaxLights = 256;

    /*************************************************************************
     \brief
        One light, in world space, as the CPU describes it to
        Renderer::SubmitLight().

     \details
        - direction: the way the light TRAVELS (for a sun pointing down,
                     (0,-1,0)). Used by Directional and Spot; ignored by
                     Point. Need not be normalized.
        - position:  used by Point and Spot; ignored by Directional.
        - colour:    linear RGB; multiplied by intensity.
        - intensity: Directional - radiance (the old hardcoded light was
                     3.0). Point/Spot - radiant intensity: follows inverse-
                     square falloff, so intensity 1 gives radiance 1 at a
                     distance of 1 unit.
        - range:     Point/Spot - distance at which the light is windowed
                     smoothly to exactly zero.
        - innerConeDeg/outerConeDeg: Spot - full-intensity half-angle and
                     the half-angle at which it reaches zero, in degrees.
     *************************************************************************/
    struct LightData {
        LightType  type = LightType::Point;
        glm::vec3  position{ 0.0f, 0.0f, 0.0f };
        glm::vec3  direction{ 0.0f, -1.0f, 0.0f };
        glm::vec3  colour{ 1.0f, 1.0f, 1.0f };
        float      intensity = 1.0f;
        float      range = 10.0f;
        float      innerConeDeg = 20.0f;
        float      outerConeDeg = 30.0f;
        bool       castShadows = false;  // honoured for Directional only (cascaded shadow maps)
    };

    /*************************************************************************
     \brief
        GPU layout of one light: one element of the light storage buffer
        (set 0, binding 1, StructuredBuffer<GpuLight> in
        DefaultShader2.slang).

     \details
        Four vec4s = 64 bytes, which is identical under std430 and plain
        C++ layout, so no padding rules can disagree. Built from LightData
        by Renderer::SubmitLight().
          - positionRange:  xyz = world position, w = range
          - directionType:  xyz = normalized travel direction,
                            w = float(LightType)
          - colourIntensity: rgb = linear colour, w = intensity
          - spotParams:     x = cos(inner half-angle),
                            y = cos(outer half-angle), zw unused
     *************************************************************************/
    struct GpuLight {
        glm::vec4 positionRange{ 0.0f };
        glm::vec4 directionType{ 0.0f };
        glm::vec4 colourIntensity{ 0.0f };
        glm::vec4 spotParams{ 0.0f };
    };
    static_assert(sizeof(GpuLight) == 64, "GpuLight must match the shader's 64-byte std430 GpuLight.");

    /*************************************************************************
     \brief
        Per-frame sky / ambient / exposure settings, handed to
        Renderer::SetEnvironment() (normally by RenderExtractSystem, from
        the scene's Skybox component).

     \details
        Like the render and light queues, this is per-frame state: the
        Renderer resets it to GetDefaultEnvironment() after every frame,
        so a scene with no Skybox entity gets the built-in procedural sky
        and default ambient.
          - skybox:           Invalid -> the Renderer's built-in procedural
                              sky (GetDefaultSkybox()).
          - drawSkybox:       false -> no sky is drawn and the pass's clear
                              colour shows instead.
          - skyboxIntensity:  multiplier on the sampled sky colour.
          - ambientSky/Ground + ambientIntensity: cheap hemisphere ambient,
                              a stand-in for real image-based lighting.
          - exposure:         multiplier applied to the final HDR colour
                              before tonemapping (meshes and sky alike).
     *************************************************************************/
    struct EnvironmentSettings {
        CubemapHandle skybox{};
        bool          drawSkybox = true;
        float         skyboxIntensity = 1.0f;
        glm::vec3     ambientSky{ 0.30f, 0.36f, 0.45f };
        glm::vec3     ambientGround{ 0.20f, 0.18f, 0.16f };
        float         ambientIntensity = 1.0f;
        float         exposure = 1.0f;
    };

    // ------------------------------------------------------------------------------------------ //
    //  Materials
    // ------------------------------------------------------------------------------------------ //

    /*************************************************************************
     \brief
        A material's scalar (non-textured) knobs.

     \details
        Shared verbatim between this C++ push-constant struct and the
        shader's own MaterialFactors (DefaultShader2.slang) - keep both in
        sync by hand, same as the rest of this file's structs (no
        reflection/codegen here). Field order matters: it must pack
        identically under HLSL's cbuffer rules (a float3 followed by a
        float shares one 16-byte slot) as it does under plain C++ struct
        layout, which is true for exactly this order (see PushConstants'
        static_assert below).
     *************************************************************************/
    struct MaterialFactors {
        glm::vec4 albedoTint{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec3 emissiveFactor{ 0.0f, 0.0f, 0.0f };
        float     metallicFactor  = 1.0f;
        float     roughnessFactor = 1.0f;
    };

    /*************************************************************************
     \brief
        What Renderer::CreateMaterial()/UpdateMaterial() take: the five PBR
        texture slots plus the scalar factors above.

     \details
        Any texture slot may be left Invalid - the Renderer substitutes a
        neutral placeholder texture so the shader never has to branch on
        "is this map present". glTF's metallic-roughness model names below
        match the spec directly (metallicRoughness packs roughness in G,
        metalness in B; ambient occlusion reads R of `occlusion`).
     *************************************************************************/
    struct MaterialData {
        TextureHandle    albedo{};
        TextureHandle    normal{};
        TextureHandle    metallicRoughness{};
        TextureHandle    emissive{};
        TextureHandle    occlusion{};
        MaterialFactors  factors{};
    };

    /*************************************************************************
     \brief
        Per-draw push constant: the current material's scalar factors.

     \details
        The model matrix used to live here too; with instancing it moved
        to the per-frame instance buffer (InstanceData), since one draw now
        covers every object sharing a mesh and material. Keeping the
        material's scalar factors here means a designer dragging a tint/roughness
        slider in the Inspector is visible next frame with no GPU
        synchronization at all - unlike the five texture bindings in the
        material's descriptor set (set 1), which live in real GPU-resident
        descriptor state and need a (deliberately heavier-handed, editor-
        only) wait-idle to swap safely; see Renderer::UpdateMaterial()'s
        comment.

     \note
        Must fit in the 128-byte push-constant range every Vulkan
        implementation is required to support
        (VkPhysicalDeviceLimits::maxPushConstantsSize's guaranteed floor) -
        enforced by the static_assert below.
     *************************************************************************/
    struct PushConstants {
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
