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
*                DefaultShader.slang). Built-in procedural primitives
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
#include "MaterialShaderHandle.h"
#include "CubemapHandle.h"
#include "LightTypes.h"
#include "Vulkan/RenderLayouts.h"   // for Vertex (adjust if Vertex lives elsewhere)

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

    /*************************************************************************
     \brief
        What a viewport is for (Renderer::CreateViewport). Decides which
        camera it looks through and whose UI it shows.
     \details
        Every role also draws UI submitted for that one viewport
        (Renderer::SubmitViewportUI).
     *************************************************************************/
    enum class ViewportRole : uint8_t {
        Scene,   ///< The level through the viewport's own camera (the editor's Scene view). Never shows the game's UI.
        Game,    ///< What the player sees: the scene through the main camera, with the game's UI (Renderer::SubmitUI) on top. With no main camera: the clear colour and the UI.
        UI,      ///< No scene at all: the clear colour and only this viewport's own UI (the UI editor tab).
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
        glm::vec4      params{ 1.0f, 0.0f, 0.0f, 0.0f };   // per-object material parameters, see InstanceData
        glm::uvec4     skin{0u}; // palette offset/count in this frame's instance buffer
        bool           sceneHidden = false;                // editor: left out of Scene-role views (Renderer::Submit)
    };

    inline bool IsMirroredTransform(const glm::mat4& model) {
        // Double precision avoids determinant underflow at small model scales.
        return glm::determinant(glm::dmat3(model)) < 0.0;
    }

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
        DefaultShader.slang has the full copy. Skybox, DebugLine, Shadow
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
        // Clustered lights (Renderer::BuildLightClusters), per view:
        glm::uvec4 clusterDims{ 1u, 1u, 1u, 0u };   // xyz = grid size, w = this view's first cell in the grid buffer
        glm::vec4  clusterZ{ 0.1f, 1.0f, 1.0f, 1.0f };   // x = near, y = depth clusters / log(far/near), zw = target size in pixels
        glm::vec4  time{ 0.0f };                         // x = seconds since the Renderer started, y = length of the last frame (seconds), z = 1: shadow cascade debug tint
        // Fog (EnvironmentSettings):
        glm::vec4  fogColour{ 0.0f };                    // rgb = colour, a = density at the base height (0 = off)
        glm::vec4  fogParams{ 0.0f, 0.0f, 0.0f, 1.0f };  // x = height falloff, y = base height, z = start distance, w = max opacity
        // Light from the sky (Renderer::BuildEnvironmentLighting):
        glm::vec4  ambientParams{ 0.0f };                // x = 1: ambient from the sky, y = last mip of the environment map, z = strength
        glm::vec4  skySH[9]{};                           // the sky's diffuse light (spherical harmonics, ready for the shader's polynomial)
        // Volumetric fog (VolumetricFogPass), set for views that have a fog volume this frame:
        glm::vec4  volumetricParams{ 0.0f };             // x = 1: read the fog volume (0 = analytic fog), y = near depth (m), z = far depth (m), w = log(far / near)
        glm::vec4  volumetricLight{ 0.0f };              // x = light scattering strength, y = anisotropy g, z = depth slices, w unused
        glm::vec4  volumetricAmbient{ 0.0f };            // x = strength of the fog's light in shade, y = 1: that light is the sky's (0 = fog colour)
        // Toon shading (EnvironmentSettings::toonShading), see DefaultShader.slang's ToonLight:
        glm::vec4  toonParams{ 0.0f };                   // x = 0: materials set to "Scene" are lit realistically, 1: toon, 2: toon switched off everywhere (Rendering menu); y = where the light/shade edge is (0..1 of the light), z = its softness, w = brightness of the sky's light on the shaded side
        glm::vec4  toonShade{ 1.0f };                    // rgb = tint of the shaded side (linear), w = how flat the sky's ambient light is (0..1)
        glm::vec4  toonEdge{ 1.0f, 1.0f, 1.0f, 0.1f };   // rgb = tint of the band at the edge (linear, luminance 1), w = the band's width
        glm::vec4  toonExtra{ 0.0f };                    // x = the band's strength, y = hard highlight strength, z = share of the sky reflection kept, w = shading by angle kept on the lit side
        // Toon outlines: what the scene shader writes for ToonOutlinePass (EnvironmentSettings::toonLine*):
        glm::vec4  toonLines{ 0.0f };                    // x = 1: write line strengths (0 = no lines anywhere), y = breakup amount (0..1), z = 1 / breakup size (per metre), w unused
    };
    static_assert(sizeof(FrameUniforms) == 64 + 64 + 16 * 5 + 64 * kMaxShadowCascades + 16 * 6 + 16 * 20,
        "FrameUniforms must stay tightly packed in 16-byte slots to match the shaders' std140 layout.");
    static_assert(offsetof(FrameUniforms, volumetricParams) == 752 && offsetof(FrameUniforms, volumetricLight) == 768 && offsetof(FrameUniforms, volumetricAmbient) == 784,
        "FrameUniforms offsets must match Common.slang (checked against the compiled SPIR-V).");
    static_assert(offsetof(FrameUniforms, toonParams) == 800 && offsetof(FrameUniforms, toonShade) == 816 && offsetof(FrameUniforms, toonEdge) == 832 && offsetof(FrameUniforms, toonExtra) == 848
                  && offsetof(FrameUniforms, toonLines) == 864,
        "FrameUniforms offsets must match Common.slang (checked against the compiled SPIR-V).");
    static_assert(offsetof(FrameUniforms, clusterDims) == 512 && offsetof(FrameUniforms, clusterZ) == 528 && offsetof(FrameUniforms, time) == 544 && offsetof(FrameUniforms, fogColour) == 560 && offsetof(FrameUniforms, skySH) == 608,
        "FrameUniforms offsets must match DefaultShader.slang (checked against the compiled SPIR-V).");
    static_assert(offsetof(FrameUniforms, shadowViewProj) == 208 && offsetof(FrameUniforms, shadowParams) == 496,
        "FrameUniforms offsets must match DefaultShader.slang (checked against the compiled SPIR-V).");

    /*************************************************************************
     \brief
        One drawn object, as the vertex shaders see it (set 0, binding 2:
        StructuredBuffer<InstanceData>). 96 bytes under std430 and C++.
     \details
        Every pass (main, shadow cascades, selection mask) of every view
        appends its objects to one per-frame array; a draw covers a
        contiguous run, addressed by drawIndexed's firstInstance.
     *************************************************************************/
    struct InstanceData {
        glm::mat4 model{ 1.0f };
        // Per-object material parameters (the MaterialParams component): x = multiplier on the
        // material's emissive (glow), yzw = free for effects to come. Objects that share a
        // material can differ here and still be drawn in one instanced draw.
        glm::vec4 params{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::uvec4 skin{0u}; // x = first palette entry, y = number of joints; zero = rigid
    };
    static_assert(sizeof(InstanceData) == 96, "InstanceData must match the shaders' std430 stride.");
    static_assert(offsetof(InstanceData, params) == 64 && offsetof(InstanceData, skin) == 80,
                  "InstanceData offsets must match ModelVertex.slang.");

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
    //  Clustered lighting
    // ------------------------------------------------------------------------------------------ //

    /*************************************************************************
     \brief
        Size of the light-cluster grid every view is cut into: across, down
        and in depth (depth slices are spaced logarithmically).
     \details
        Each cell lists the point/spot lights that can reach it, so a pixel
        shades only with the lights near it. Directional lights are not
        clustered - they reach everything. Built on the CPU once per view
        per frame (Renderer::BuildLightClusters); the two buffers behind it
        are set 0, bindings 6 (ClusterCell per cell) and 7 (light indices).
     *************************************************************************/
    inline constexpr uint32_t kClusterX = 16;
    inline constexpr uint32_t kClusterY = 9;
    inline constexpr uint32_t kClusterZ = 24;
    inline constexpr uint32_t kClustersPerView = kClusterX * kClusterY * kClusterZ;

    /// One cell of the grid: where its light list starts in the index buffer, and its length.
    struct ClusterCell {
        uint32_t offset = 0;
        uint32_t count = 0;
    };
    static_assert(sizeof(ClusterCell) == 8, "ClusterCell must match DefaultShader.slang.");

    // ------------------------------------------------------------------------------------------ //
    //  Particles
    // ------------------------------------------------------------------------------------------ //

    /*************************************************************************
     \brief
        One particle as the particle shader reads it (set 0, binding 5:
        StructuredBuffer<GpuParticle> in Particle.slang). 64 bytes.
     \details
          - posSize: xyz = world position, w = width in metres
          - color:   rgb = LINEAR colour, may exceed 1 (glows through
                     bloom); a = opacity
          - axisRot: xyz = stretch vector (zero = camera-facing square;
                     otherwise the quad is stretched along it - sparks),
                     w = rotation in radians (billboards only)
          - misc:    x = flipbook frame index
     *************************************************************************/
    struct GpuParticle {
        glm::vec4 posSize{ 0.0f, 0.0f, 0.0f, 0.1f };
        glm::vec4 color{ 1.0f };
        glm::vec4 axisRot{ 0.0f };
        glm::vec4 misc{ 0.0f };
    };
    static_assert(sizeof(GpuParticle) == 64, "GpuParticle must match Particle.slang's 64-byte std430 GpuParticle.");

    /*************************************************************************
     \brief
        How one batch of particles (normally: one emitter) is drawn - see
        Renderer::SubmitParticles().
     \details
          - texture:  Invalid = a procedural soft round dot.
          - additive: true = colours add up (sparks, fire: order doesn't
                      matter); false = alpha blending (smoke: the Renderer
                      sorts these back to front per view).
          - flipbookCols/Rows: the texture is an atlas of this many frames.
          - boundsMin/Max: world box around the batch, for frustum culling
                      and for ordering batches by distance.
          - sceneHidden: editor scene visibility (the SceneHidden tag on the
                      emitter): left out of Scene-role views only.
     *************************************************************************/
    struct ParticleDrawDesc {
        TextureHandle texture{};
        bool          additive = true;
        uint32_t      flipbookCols = 1;
        uint32_t      flipbookRows = 1;
        glm::vec3     boundsMin{ 0.0f };
        glm::vec3     boundsMax{ 0.0f };
        bool          sceneHidden = false;
    };

    /// Push constant of the particle pipeline (Particle.slang's ParticlePush).
    struct ParticlePush {
        glm::vec4 params{ 1.0f, 1.0f, 1.0f, 0.0f };   // x = cols, y = rows, z = additive, w = textured
    };

    // ------------------------------------------------------------------------------------------ //
    //  Water (WaterPass, Water.slang)
    // ------------------------------------------------------------------------------------------ //

    /*************************************************************************
     \brief
        How one water surface looks - see Renderer::SubmitWater() and the
        Water component, whose fields these are.
     \details
          - shallowColour / deepColour: the water's colour where it is
            shallow and where it is deep (sRGB, as picked). deepDepth: water
            depth (m) at which it is fully the deep colour.
          - clarity: how far (m) through the water the bottom still shows.
          - refraction: how much the ripples bend the view of the bottom (0..1).
          - foamColour (sRGB); foamWidth: the shore foam band, in metres of
            water depth (0 = no foam); foamBreakup: 0 = an even band,
            1 = ragged and broken.
          - rippleSize (m), rippleSpeed (m/s), rippleStrength (how far they
            tilt the surface); rippleLines: thin drawn strokes on open water;
            rippleLineColour: their colour (sRGB).
          - glints: strength of the hard sparkles of the sun and lamps;
            glintColour tints them (sRGB, white = the light's own colour).
          - reflection: how much sky the surface reflects at grazing angles;
            reflectionColour tints the reflected sky (sRGB, white = as it is).
     *************************************************************************/
    struct WaterSettings {
        glm::vec3 shallowColour{ 0.45f, 0.85f, 0.72f };
        glm::vec3 deepColour{ 0.05f, 0.35f, 0.55f };
        float     deepDepth = 2.5f;
        float     clarity = 3.0f;
        float     refraction = 0.4f;
        glm::vec3 foamColour{ 1.0f, 1.0f, 1.0f };
        float     foamWidth = 0.2f;
        float     foamBreakup = 0.5f;
        float     rippleSize = 2.0f;
        float     rippleSpeed = 0.25f;
        float     rippleStrength = 0.25f;
        float     rippleLines = 0.3f;
        glm::vec3 rippleLineColour{ 1.0f, 1.0f, 1.0f };
        float     glints = 1.0f;
        glm::vec3 glintColour{ 1.0f, 1.0f, 1.0f };
        float     reflection = 0.5f;
        glm::vec3 reflectionColour{ 1.0f, 1.0f, 1.0f };
    };

    /// Push constant of the water pipeline (Water.slang's WaterPush): one water surface. 128
    /// bytes, the most every GPU guarantees, so the six colours travel as half floats, two to a
    /// uint (glm::packHalf2x16: low half first), linear RGB, in this order: shallow, deep, foam,
    /// reflection tint, ripple line, glint (already multiplied by the glint strength).
    struct WaterPush {
        glm::vec4  model0{ 1.0f, 0.0f, 0.0f, 0.0f };   // the surface's object -> world transform, as three rows
        glm::vec4  model1{ 0.0f, 1.0f, 0.0f, 0.0f };
        glm::vec4  model2{ 0.0f, 0.0f, 1.0f, 0.0f };
        glm::uvec4 coloursA{ 0u };   // shallow r g | shallow b, deep r | deep g b | foam r g
        glm::uvec4 coloursB{ 0u };   // foam b, reflection r | reflection g b | line r g | line b, glint r
        glm::vec4  water{ 0.0f };    // x = deepDepth (m), y = clarity (m), z = refraction (0..1), w = foamWidth (m)
        glm::vec4  ripple{ 0.0f };   // x = rippleSize (m), y = rippleSpeed (m/s), z = rippleStrength, w = foamBreakup (0..1)
        uint32_t   glintGB = 0;      // glint g | glint b
        float      reflection = 0.0f;
        float      rippleLines = 0.0f;
        float      unused = 0.0f;
    };
    static_assert(sizeof(WaterPush) == 128 && offsetof(WaterPush, coloursA) == 48 && offsetof(WaterPush, water) == 80
                  && offsetof(WaterPush, glintGB) == 112 && offsetof(WaterPush, rippleLines) == 120,
        "WaterPush must match Water.slang's WaterPush (checked against the compiled SPIR-V).");

    // ------------------------------------------------------------------------------------------ //
    //  Post-processing (HDR -> bloom -> tonemap)
    // ------------------------------------------------------------------------------------------ //

    /// Number of half-size steps in the bloom chain (fewer for very small render targets).
    inline constexpr uint32_t kBloomMips = 5;

    /*************************************************************************
     \brief
        Bloom and tonemap settings (Renderer::GetPostSettings()). Exposure
        itself comes from the scene's Skybox component / EnvironmentSettings.
     \details
          - bloom:          false skips the bloom passes entirely.
          - bloomThreshold: only (exposed) brightness above this glows. 1.0
                            = only things brighter than "white".
          - bloomKnee:      softness of that cut-off.
          - bloomIntensity: strength of the glow added back.
          - fxaa:           smooths jagged edges in the final image (fast
                            approximate anti-aliasing, part of the tonemap
                            pass).
          - ao:             ambient occlusion: ambient light is dimmed in
                            creases, corners and under objects. Costs one
                            extra half-size depth-only draw of the scene.
          - aoRadius:       how far (metres) geometry can be and still
                            shade a point.
          - aoIntensity:    strength of the darkening.
          - msaaSamples:    1, 2 or 4 samples per pixel for the scene pass
                            (MSAA: smooths the edges of geometry properly,
                            at a memory and fill cost). Clamped to what the
                            GPU supports; a change takes effect next frame.
          - volumetricFog:  allows the lit froxel fog (light shafts, glow
                            around lights) wherever the scene's Skybox
                            turns it on. Off: always the cheaper analytic
                            fog. Costs two compute passes per view.
     *************************************************************************/
    struct PostSettings {
        bool  bloom = true;
        float bloomThreshold = 1.0f;
        float bloomKnee = 0.5f;
        float bloomIntensity = 0.35f;
        bool  fxaa = true;
        uint32_t msaaSamples = 4;
        bool  ao = true;
        float aoRadius = 0.6f;
        float aoIntensity = 1.2f;
        bool  volumetricFog = true;   // lit froxel fog where the scene's Skybox asks for it (off = always the analytic fog)
        bool  toonShading = true;     // toon shading where the scene's Skybox asks for it (off = always the PBR lighting, to compare)
    };

    /// Push constant shared by every post pass (Post.slang's PostPush). c and d are read only by
    /// the tonemap pass (the scene's look: contrast, saturation, grain, white balance, fringing).
    struct PostPush {
        glm::vec4 a{ 0.0f };
        glm::vec4 b{ 0.0f };
        glm::vec4 c{ 1.0f, 1.0f, 0.0f, 0.0f };
        glm::vec4 d{ 1.0f, 1.0f, 1.0f, 0.0f };
    };
    static_assert(sizeof(PostPush) == 64, "PostPush must match Post.slang.");

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
        uint32_t particles = 0;          // particles drawn in this view
        uint32_t particleDrawCalls = 0;  // one per visible emitter batch
        uint32_t clusteredLights = 0;    // point/spot lights that reach this view's frustum
    };

    /// The parts of a frame the GPU's time is split into (RenderFrameStats::gpuMs).
    enum class GpuSection : uint8_t {
        Uploads,            ///< Mesh and texture copies queued since the last frame.
        Shadows,            ///< Sun shadow cascades.
        Outline,            ///< Selection outline mask (editor).
        AmbientOcclusion,   ///< Depth pre-pass, occlusion, blur.
        Volumetrics,        ///< Froxel fog: light the volume, then integrate it (compute).
        Scene,              ///< Meshes, sky and particles (including the MSAA resolve).
        Bloom,
        Final,              ///< Tonemap + FXAA, debug lines, outline composite.
        Interface,          ///< The window pass: editor UI (and the tonemap in a standalone build).
        End,                ///< Marks the end of the frame; never holds time itself.
        Count
    };

    /*************************************************************************
     \brief
        Where the last frames' time went (Renderer::GetFrameStats()), each
        value smoothed over roughly ten frames, in milliseconds.
     \details
        CPU values are measured around the stages of Renderer::DrawFrame:
          - cpuWaitMs:    waiting for the GPU to finish an earlier frame and
                          for the next window image (grows when the GPU, or
                          vsync, is the limit).
          - cpuPrepareMs: lights, culling, batching, shadow cascades, light
                          clusters, particles - for every view.
          - cpuUploadMs:  copying instances, particles and clusters to the GPU.
          - cpuRecordMs:  recording the frame's commands (includes the editor
                          UI's own draw commands).
          - cpuSubmitMs:  submitting and presenting.
        GPU values come from timestamps the GPU writes between passes, summed
        over all views. gpuValid is false when the GPU cannot provide them.
     *************************************************************************/
    struct RenderFrameStats {
        float frameMs = 0.0f;        // start of one DrawFrame to the start of the next
        float cpuWaitMs = 0.0f;
        float cpuPrepareMs = 0.0f;
        float cpuUploadMs = 0.0f;
        float cpuRecordMs = 0.0f;
        float cpuSubmitMs = 0.0f;
        float gpuMs[static_cast<size_t>(GpuSection::Count)]{};
        float gpuTotalMs = 0.0f;
        bool  gpuValid = false;
    };

    /*************************************************************************
     \brief
        A built-in load test (Renderer::GetStressSettings()): `count` extra
        objects in a grid, drawn through the same path as scene entities
        (render proxies, culling, instancing, shadows) but with no entities
        behind them, so the renderer can be measured on its own.
     \details
        - count:   0 = off. Changing it rebuilds the grid.
        - moving:  every object bobs and turns each frame, as a horde would;
                   off = they stand still (the cheap case for culling).
        - spacing: distance between neighbours, in metres.
     *************************************************************************/
    struct StressSettings {
        uint32_t count = 0;
        bool     moving = true;
        float    spacing = 2.5f;
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
        float    depthBias = 0.0001f;
        float    normalBias = 1.5f;
        float    fadeFraction = 0.1f;
        bool     debugCascades = false;   // tint every surface by the cascade it uses (see DefaultShader.slang)
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
        DefaultShader.slang).

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
          - ambientFromSky:   true -> ambient light and reflections come
                              from the skybox itself (image-based lighting),
                              scaled by ambientIntensity * skyboxIntensity;
                              false -> the ambientSky/Ground colours.
          - fog*:             height fog. fogDensity 0 = off; a typical
                              value is 0.01-0.05. fogHeightFalloff = how
                              quickly it thins above fogBaseHeight (0 = same
                              at every height); fogStart = clear distance in
                              front of the camera; fogMaxOpacity caps how
                              much it can hide.
          - volumetric*:      the same fog lit by the lights (froxel volume,
                              VolumetricFogPass), when PostSettings::
                              volumetricFog also allows it. volumetricIntensity
                              adds the sun (with its shadows: light shafts)
                              and point/spot lights; volumetricAnisotropy (0 =
                              even, toward 1 = glow gathers around lights seen
                              ahead); volumetricDistance = how far the volume
                              and its shadows reach (beyond, the fog is lit
                              but unshadowed). volumetricAmbient = how bright
                              the fog is in shade (1 = fogColour, the analytic
                              fog's look; lower = darker shade, so shafts stand
                              out). volumetricSkyAmbient: the fog's light in
                              shade is instead the sky's average light (the
                              same ambient as surfaces) tinted by fogColour -
                              follows the sky, but a dark sky gives dark fog.
          - The look, applied in the tonemap pass. Each has a neutral value
            at which it does nothing:
              contrast (1), saturation (1; 0 = black and white),
              temperature (0; -1 cool/blue .. +1 warm/orange),
              colourFilter (white; multiplies the picture),
              vignetteIntensity (0; darkening toward the corners) and
              vignetteSoftness (how far in from the corners it reaches),
              filmGrain (0), chromaticAberration (0; colour fringing
              toward the edges, 1 = strong).
     *************************************************************************/
    struct EnvironmentSettings {
        CubemapHandle skybox{};
        bool          drawSkybox = true;
        float         skyboxIntensity = 1.0f;
        glm::vec3     ambientSky{ 0.30f, 0.36f, 0.45f };
        glm::vec3     ambientGround{ 0.20f, 0.18f, 0.16f };
        float         ambientIntensity = 1.0f;
        float         exposure = 1.0f;
        bool          ambientFromSky = true;
        float         fogDensity = 0.0f;
        glm::vec3     fogColour{ 0.55f, 0.63f, 0.72f };
        float         fogHeightFalloff = 0.08f;
        float         fogBaseHeight = 0.0f;
        float         fogStart = 0.0f;
        float         fogMaxOpacity = 1.0f;
        bool          volumetricFog = true;          // light the fog (sun shafts, glow around lights) with a froxel volume
        float         volumetricIntensity = 1.0f;    // how strongly lights scatter in the fog (0 = the fog's light in shade only)
        float         volumetricAnisotropy = 0.5f;   // -1..1: 0 scatters evenly, toward 1 the glow gathers around lights you look toward
        float         volumetricDistance = 100.0f;   // metres the volume (and its shadows) reaches; beyond it the fog is lit but unshadowed
        float         volumetricAmbient = 1.0f;      // brightness of the fog in shade (1 = fogColour; lower = stronger shafts)
        bool          volumetricSkyAmbient = false;  // fog in shade lit by the sky's average light, tinted by fogColour (false = fogColour)
        float         contrast = 1.0f;
        float         saturation = 1.0f;
        float         temperature = 0.0f;
        glm::vec3     colourFilter{ 1.0f, 1.0f, 1.0f };
        float         vignetteIntensity = 0.0f;
        float         vignetteSoftness = 0.5f;
        float         filmGrain = 0.0f;
        float         chromaticAberration = 0.0f;
        // Toon shading (see the Skybox component for what each one does). Colours are sRGB, as picked.
        bool          toonShading = false;
        float         toonThreshold = 0.1f;
        float         toonSoftness = 0.03f;
        glm::vec3     toonShadeColour{ 0.85f, 0.87f, 0.95f };
        float         toonShadeBrightness = 1.5f;
        float         toonLitShading = 0.4f;
        glm::vec3     toonEdgeColour{ 1.0f, 0.62f, 0.42f };
        float         toonEdgeWidth = 0.15f;
        float         toonEdgeStrength = 0.6f;
        float         toonAmbientFlatten = 0.7f;
        float         toonHighlight = 1.0f;
        float         toonReflections = 0.0f;
        // Toon outlines (ToonOutlinePass; see the Skybox component).
        bool          toonLines = true;
        float         toonLineDarkness = 0.6f;
        float         toonLineWidth = 1.0f;
        float         toonLineCreaseAngle = 75.0f;
        float         toonLineCreaseStrength = 0.5f;
        float         toonLineDepthGap = 0.03f;
        float         toonLineBreakup = 0.35f;
        float         toonLineBreakupSize = 0.25f;
        float         toonLineFadeDistance = 60.0f;
    };

    // ------------------------------------------------------------------------------------------ //
    //  Materials
    // ------------------------------------------------------------------------------------------ //

    /*************************************************************************
     \brief
        A material's scalar (non-textured) knobs.

     \details
        Shared verbatim between this C++ push-constant struct and the
        shader's own MaterialFactors (DefaultShader.slang) - keep both in
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
    /// How a material is lit (MaterialData::shading).
    enum class MaterialShading : uint8_t {
        Scene = 0,       ///< whatever the scene's Skybox says (toonShading on: toon, off: realistic)
        Toon = 1,        ///< always toon
        Realistic = 2    ///< always the physically based lighting
    };

    /*************************************************************************
     \brief  Per-material changes to the scene's toon look (MaterialData::toon).
     \details
        For things that should not share the scene's hard edge - foliage
        that shades as one soft gradient, say. Colours are sRGB, as picked.
          - ownEdge: use threshold/softness here instead of the scene's.
          - ownShadeColour: use shadeColour here instead of the scene's.
          - litColour: multiplies the light on the lit side (white = none);
            a warm colour with a cool shadeColour gives a yellow-to-teal
            gradient across a canopy.
     *************************************************************************/
    struct MaterialToon {
        bool      ownEdge = false;
        float     threshold = 0.1f;
        float     softness = 0.03f;
        bool      ownShadeColour = false;
        glm::vec3 shadeColour{ 0.85f, 0.87f, 0.95f };
        glm::vec3 litColour{ 1.0f, 1.0f, 1.0f };
        float     outline = 1.0f;   ///< toon outlines on this material: 0 = none, 1 = the scene's full strength
    };

    struct MaterialData {
        TextureHandle    albedo{};
        TextureHandle    normal{};
        TextureHandle    metallicRoughness{};
        TextureHandle    emissive{};
        TextureHandle    occlusion{};
        MaterialFactors  factors{};
        MaterialShading  shading = MaterialShading::Scene;
        // Cut-out: pixels whose alpha (albedo texture alpha x albedoTint alpha) is below this are
        // not drawn - in the shadows and ambient occlusion too, so leaves cast leafy shadows.
        // 0 = off (solid). glTF "MASK" materials import with their alphaCutoff.
        float            alphaCutoff = 0.0f;
        // Both sides drawn (leaf cards, thin sheets); the back side is lit as its own front.
        bool             doubleSided = false;
        MaterialToon     toon{};
        // The mesh shader that draws it (Renderer::LoadMaterialShader). Invalid, the default, is
        // the built-in shader (DefaultShader.slang). Many materials share one shader.
        MaterialShaderHandle shader{};
        // Its values for that shader's own parameters and textures (the shader's list:
        // MaterialShaderParams), by slot. Shorter than the list - empty, usually, for a material
        // that sets none - means the list's defaults for the rest. Unused by the built-in shader.
        std::vector<glm::vec4>     params;
        std::vector<TextureHandle> shaderTextures;
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
        float            alphaCutoff = 0.0f;   // 0 = solid
        uint32_t         flags = 0;            // bits 0-1: MaterialShading, bit 2: toonEdge applies, bit 3: toonShade applies
        float            outline = 1.0f;       // strength of toon outlines on this material (0..1), MaterialToon::outline
        glm::vec4        toonEdge{ 0.1f, 0.03f, 0.0f, 0.0f };   // x = edge position, y = softness (flag bit 2)
        glm::vec4        toonShade{ 1.0f };                     // rgb = shade tint, linear (flag bit 3)
        glm::vec4        toonLit{ 1.0f };                       // rgb = lit tint, linear
        uint32_t         paramOffset = 0;      // where the material's own parameters start in the parameter buffer (MaterialParamBuffer)
    };
    inline constexpr uint32_t kMaterialFlagOwnEdge  = 1u << 2;
    inline constexpr uint32_t kMaterialFlagOwnShade = 1u << 3;
    static_assert(sizeof(MaterialFactors) == 36 && offsetof(PushConstants, alphaCutoff) == 36 && offsetof(PushConstants, flags) == 40
                  && offsetof(PushConstants, outline) == 44 && offsetof(PushConstants, toonEdge) == 48 && offsetof(PushConstants, toonShade) == 64 && offsetof(PushConstants, toonLit) == 80
                  && offsetof(PushConstants, paramOffset) == 96 && sizeof(PushConstants) == 100,
        "PushConstants must match Surface.slang's (checked against the compiled SPIR-V).");

    /// Push constant of the depth-only passes' cut-out pipelines (shadow, ambient occlusion,
    /// selection mask): what decides which pixels of a cut-out material are drawn.
    struct CutoutPush {
        float alphaCutoff = 0.0f;
        float alphaTint = 1.0f;   // MaterialFactors::albedoTint.a
    };
    static_assert(sizeof(CutoutPush) == 8, "CutoutPush must match the depth shaders' cutout fields.");
    static_assert(sizeof(PushConstants) <= 128,
        "PushConstants must fit in the 128-byte push-constant range every Vulkan implementation "
        "is required to support (VkPhysicalDeviceLimits::maxPushConstantsSize's guaranteed floor).");

    // ------------------------------------------------------------------------------------------ //
    //  Built-in primitives (Cube, Plane, Disc, Cylinder, Cone, Pyramid, Sphere, Capsule) have moved
    //  to Graphics/Primitives.h to keep this header lean - see that file for the Primitives
    //  namespace. Include "Primitives.h" (it includes this header itself) wherever you
    //  need Primitives::MakeXxx().


}   // NAMESPACE CRUCIBLE
