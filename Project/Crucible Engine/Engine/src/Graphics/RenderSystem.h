#pragma once
/*************************************************************************
* \file:         RenderSystem.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                The engine's Renderer: owns the Vulkan context/swapchain/
*                pipeline/frame manager and every GPU-side mesh, texture,
*                material, and offscreen viewport resource.
*
* \details
*                Declares Renderer, the single class the rest of the
*                engine (and the Editor) talks to for anything GPU-related:
*                uploading meshes/textures, creating/updating materials,
*                creating offscreen viewports for Editor panels, submitting
*                per-frame draw calls, and driving the frame loop itself.
*                Renderer stays entirely unaware the ECS exists - see
*                RenderExtractSystem.h for the one place that bridges the
*                two.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
    #include <vulkan/vulkan_raii.hpp>
#else
    import vulkan_hpp;
#endif
#include <GLFW/glfw3.h>

// STL
#include <vector>
#include <array>
#include <deque>
#include <unordered_map>
#include <functional>

// Crucible Engine
#include "Systems/WindowManager.h"
#include "Math/Math.h"
 
#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanSwapchain.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanFrameManager.h"
#include "Vulkan/VulkanResource.h"
#include "Vulkan/VulkanUploadQueue.h"

#include "Camera.h"
#include "RenderTypes.h"
#include "DebugDraw.h"
#include "Spatial/SpatialIndex.h"

// Imgui
#include "Vulkan/ImGuiContext.h"
#include "EngineExport.h"

namespace Crucible {

    /*************************************************************************
     \brief
        The engine's Renderer: owns the Vulkan context/swapchain/pipeline/
        frame manager and every GPU-side mesh, texture, material, and
        offscreen viewport resource.

     \details
        See the file header for Renderer's role in the engine. Internally
        organized into Meshes, Textures, Materials, Viewports, and Draw
        submission - see the section banners below and in the .cpp.
     *************************************************************************/
    class Renderer {

        // GPU-side mesh. One vertex + one index buffer per mesh for now (simple; suballocate from
        // big shared buffers later if mesh counts get large).
        struct GpuMesh {
            VulkanBuffer vertexBuffer;
            VulkanBuffer indexBuffer;
            uint32_t     indexCount = 0;

            // Human-readable label, purely for tools (Editor UI, RenderDoc/Nsight captures via a
            // future debug-object-name call, log messages) - never used to look a mesh up, and
            // never required to be unique. See GetMeshName()'s doc comment for who's meant to
            // read this and why it lives here rather than in the Editor.
            std::string  debugName;

            // Object-space bounding box of the vertex positions (computed at upload), for
            // selection boxes and future culling - see GetMeshBounds().
            glm::vec3    boundsMin{ 0.0f };
            glm::vec3    boundsMax{ 0.0f };

            // CPU copy of the geometry, for exact ray picking (Raycast). Positions + indices only:
            // 12 bytes per vertex + 4 per index, per MESH (instances share it).
            std::vector<glm::vec3> cpuPositions;
            std::vector<uint32_t>  cpuIndices;
        };

        // GPU-side texture: an image + its view, indexed by TextureHandle. No per-texture
        // sampler - every material texture is sampled through the one shared m_materialSampler
        // (a repeat-wrap trilinear-ish sampler is fine for every texture kind we load today).
        struct GpuTexture {
            VulkanImage         image;
            vk::raii::ImageView view = nullptr;
            uint32_t            width = 0;
            uint32_t            height = 0;
        };

        // GPU-side material: which texture each of the five PBR slots currently points at (kept
        // around so UpdateMaterial can tell whether a texture binding actually changed, and so
        // the Inspector can read back what a material is set to), the scalar factors (pushed per
        // draw - see PushConstants), and the one descriptor set (layout set = 1) those five
        // textures are bound into.
        struct GpuMaterial {
            MaterialData             data;
            vk::raii::DescriptorSet   descriptorSet = nullptr;
        };

        // GPU-side cubemap (skybox): a 6-layer cube-compatible image, its cube view, and the one
        // descriptor set (skybox pipeline's set = 1) it's bound into - allocated once at creation,
        // same as a material's, since a cubemap's contents never change after upload.
        struct GpuCubemap {
            VulkanImage              image;
            vk::raii::ImageView      view = nullptr;
            vk::raii::DescriptorSet  descriptorSet = nullptr;
            uint32_t                 faceSize = 0;
            std::string              debugName;
            // The same sky prepared as a light source (BuildEnvironmentLighting): a small cube
            // map whose every mip level is blurrier than the last, for reflections by roughness,
            // and nine spherical-harmonic coefficients for diffuse light.
            VulkanImage              envImage;
            vk::raii::ImageView      envView = nullptr;
            uint32_t                 envMipCount = 1;
            std::array<glm::vec4, 9> sh{};
        };

        // An offscreen render target: its own color+depth image, camera, and per-frame-in-flight
        // uniform buffer/descriptor set, so several can be drawn per frame with different cameras
        // (Scene view vs. Game view) without stepping on each other's data.
        // One instanced draw: `instanceCount` objects sharing a mesh (and, for the main pass, a
        // material), whose matrices sit contiguously in this frame's instance buffer from
        // `firstInstance` on.
        // One particle draw: `count` particles from `first` on in this frame's particle buffer,
        // all with the same texture and blend mode (one emitter).
        struct ParticleBatch {
            TextureHandle texture;
            uint32_t      first = 0;
            uint32_t      count = 0;
            ParticlePush  push;
        };

        struct DrawBatch {
            MeshHandle     mesh;
            MaterialHandle material;
            uint32_t       firstInstance = 0;
            uint32_t       instanceCount = 0;
        };

        // Everything one view draws this frame, built by PrepareView() before recording starts
        // (so the instance buffer's final size is known before any command references it).
        struct ViewDrawList {
            std::vector<DrawBatch>                                main;
            std::array<std::vector<DrawBatch>, kMaxShadowCascades> shadow;
            uint32_t                                              cascadeCount = 0;   // 0 = no shadow pass
            std::vector<DrawBatch>                                outline;            // selected object (mask pass)
            std::vector<ParticleBatch>                            particles;          // far to near
            glm::vec4                                             projInfo{ 1.0f };   // proj[0][0], [1][1], [2][2], [3][2] (ambient occlusion)
            void Clear() { main.clear(); for (auto& c : shadow) { c.clear(); } cascadeCount = 0; outline.clear(); particles.clear(); }
        };

        // Where one view's scene is drawn before it reaches the screen: a floating-point (HDR)
        // colour image, plus the chain of ever smaller images the bloom is built in. One per
        // viewport, and one for the window in a standalone build. See DrawSceneHdr/DrawBloom/
        // DrawTonemap.
        struct PostTargets {
            vk::Extent2D                                     extent{};
            VulkanImage                                      hdrImage;
            vk::raii::ImageView                              hdrView = nullptr;
            uint32_t                                         bloomCount = 0;   // steps in use (small targets get fewer)
            std::array<VulkanImage, kBloomMips>              bloomImages;
            std::array<vk::raii::ImageView, kBloomMips>      bloomViews{ nullptr, nullptr, nullptr, nullptr, nullptr };
            std::array<vk::Extent2D, kBloomMips>             bloomExtents{};
            vk::raii::DescriptorSet                          hdrSet = nullptr;       // the HDR image, as a bloom source
            std::array<vk::raii::DescriptorSet, kBloomMips>  bloomSets{ nullptr, nullptr, nullptr, nullptr, nullptr };
            vk::raii::DescriptorSet                          tonemapSet = nullptr;   // HDR image + finished bloom
            // Ambient occlusion, all half size: a depth-only image of the scene, the raw (noisy)
            // occlusion, and the smoothed result the scene shader samples (set 0, binding 9).
            vk::Extent2D                                     aoExtent{};
            VulkanImage                                      aoDepth;
            vk::raii::ImageView                              aoDepthView = nullptr;
            VulkanImage                                      aoRaw;
            vk::raii::ImageView                              aoRawView = nullptr;
            VulkanImage                                      aoBlur;
            vk::raii::ImageView                              aoBlurView = nullptr;
            vk::raii::DescriptorSet                          aoDepthSet = nullptr;   // depth, for the occlusion pass
            vk::raii::DescriptorSet                          aoBlurSet = nullptr;    // raw occlusion + depth, for the blur pass
            // MSAA (samples > 1): the scene is drawn into these multisampled images, which are
            // then resolved (averaged) into hdrImage and the view's ordinary depth image.
            uint32_t                                         samples = 1;
            VulkanImage                                      msaaColor;
            vk::raii::ImageView                              msaaColorView = nullptr;
            VulkanImage                                      msaaDepth;
            vk::raii::ImageView                              msaaDepthView = nullptr;
        };

        struct Viewport {
            vk::Extent2D        extent{};

            VulkanImage         colorImage;
            vk::raii::ImageView colorView = nullptr;
            VulkanImage         depthImage;
            vk::raii::ImageView depthView = nullptr;

            Camera              camera;   // independent from the main m_camera and from other viewports

            std::vector<VulkanBuffer>            uniformBuffers;
            std::vector<void*>                   uniformBuffersMapped;
            std::vector<vk::raii::DescriptorSet> descriptorSets;

            bool                debugDraw = false;   // draw DebugDraw lines into this view (SetViewportDebugDraw)
            bool                outline = false;     // draw the selection outline (SetViewportOutline)
            bool                active = true;       // false: not culled, not rendered (SetViewportActive)
            RenderCullStats     cullStats;           // last frame's culling result for this view

            // Selection outline: R8 mask of the selected object (same size as colorImage), and
            // the composite pass's descriptor set pointing at it.
            VulkanImage             maskImage;
            vk::raii::ImageView     maskView = nullptr;
            vk::raii::DescriptorSet outlineSet = nullptr;

            ViewDrawList        draws;               // this frame's batches (PrepareView)
            PostTargets         post;                // HDR scene image + bloom chain, same size as colorImage
        };

    public:
        /*********************************************************************
         \brief  Constructs the Vulkan context/swapchain/pipeline/frame
                 manager, uploads the default textures/material/primitive
                 meshes, and loads the demo scene's sample model.
         \param[in] window The window to render into.
         *********************************************************************/
        Renderer(WindowManager& window);

        /*********************************************************************
         \brief  Waits for the device to go idle before teardown.
         *********************************************************************/
        void ShutDown();

        /*********************************************************************
         \brief  Drains any pending async mesh
                 uploads, populates/sorts the render queue, and draws the
                 frame.
         \param[in] dt Frame delta time, in seconds.
         *********************************************************************/
        void Update(float dt);

        /*********************************************************************
         \brief  Acquires a swapchain image, records the command buffer for
                 this frame, and submits/presents it.
         *********************************************************************/
        void DrawFrame();


        /*********************************************************************
         \brief
            The main (game) camera: what the Game view shows, and what is
            drawn straight to the window in a standalone build.
         \details
            It belongs to the scene: RenderExtractSystem sets it every frame
            from the active CameraComponent entity (SetMainCamera), or
            clears it when the scene has none (ClearMainCamera). While
            HasMainCamera() is false the window pass draws no scene and the
            Game view shows "No cameras rendering". GetCamera() then returns
            the last camera that was set. Aspect ratio is overwritten from
            the render target each frame.
         *********************************************************************/
        void          SetMainCamera(const Camera& camera) { m_camera = camera; m_hasMainCamera = true; }
        void          ClearMainCamera()                   { m_hasMainCamera = false; }
        bool          HasMainCamera() const               { return m_hasMainCamera; }
        const Camera& GetCamera() const                   { return m_camera; }

        /*********************************************************************
         \brief
            Creates the mesh's GPU buffers right away and returns a handle
            for use in an entity's MeshRenderer component. The copy of the
            vertex/index data into those buffers is queued (see
            VulkanUploadQueue) and runs at the start of the next frame's
            command buffer, ahead of every draw - so the mesh is drawable
            from that frame on, and this call never waits on the GPU.

         \param[in] data      The CPU-side mesh to upload.
         \param[in] debugName Optional, purely descriptive name (see
                               GetMeshName()) - pass the source asset's own
                               name when you have one (a glTF mesh's
                               "name", say), otherwise leave it empty and a
                               generic "Mesh <id>" is used.
         \return A handle for use in an entity's MeshRenderer component
                 (ECS/Components/Components.h; extracted into a Submit()
                 call every frame by Graphics/RenderExtractSystem.h).

         \details
            Use for meshes needed right away: built-in primitives, or a
            handful of assets loaded at startup.
         *********************************************************************/
        ENGINE_API MeshHandle CreateMesh(const MeshData& data, const std::string& debugName = {});

        /*********************************************************************
         \brief
            Returns a handle immediately; the mesh's buffers are created
            and filled a few meshes per frame (see kMaxMeshUploadsPerFrame),
            so the CPU-side work of a burst of meshes is spread out.

         \param[in] data      The CPU-side mesh to upload.
         \param[in] debugName Optional, purely descriptive name - see
                               CreateMesh().
         \return A handle, usable immediately (see \\details).

         \details
            Use this for meshes streamed in at runtime (e.g. the Resource
            Manager's .glb loader) so a burst of loaded models doesn't
            stall a single frame. The mesh simply isn't drawn until its
            upload completes.
         *********************************************************************/
        ENGINE_API MeshHandle CreateMeshAsync(MeshData data, const std::string& debugName = {});

        /*********************************************************************
         \brief
            Returns a mesh's human-readable debug name.

         \param[in] handle The mesh to look up.
         \return Reference to the mesh's debug name (or a shared
                 "(unknown mesh)" string for an invalid/out-of-range
                 handle).

         \details
            The Renderer is the source of truth for "which mesh is id 3" -
            not any one Editor panel. A name is descriptive metadata about
            a GPU resource, so it lives right next to the resource
            (GpuMesh::debugName) instead of a separate map some panel
            keeps in sync by hand: there's no single place in the Editor
            where "a mesh was just created" happens (a primitive in
            Renderer's own constructor, a .glb load, gameplay code later),
            so any panel-owned map would inevitably drift, and every panel
            wanting to show a name (today just Inspector, maybe Hierarchy
            or a future asset browser tomorrow) would otherwise need its
            own copy kept in sync independently. Centralizing it here also
            means it's available to anything that isn't a panel at all - a
            debug log line, a future vkSetDebugUtilsObjectNameEXT call for
            RenderDoc/Nsight captures, and so on.

         \note
            Returns a reference into m_meshes - same lifetime caveat as
            GetMaterialData(): valid until the next structural change to
            the mesh list, so don't hold it across frames.
         *********************************************************************/
        ENGINE_API const std::string& GetMeshName(MeshHandle handle) const;

        /*********************************************************************
         \brief  Object-space bounding box of a mesh's vertices.
         \param[in]  handle The mesh.
         \param[out] outMin Minimum corner.
         \param[out] outMax Maximum corner.
         \return false (outputs untouched) if the handle is invalid or the
                 mesh hasn't finished uploading.
         *********************************************************************/
        ENGINE_API bool GetMeshBounds(MeshHandle handle, glm::vec3& outMin, glm::vec3& outMax) const;

        /*********************************************************************
         \brief
            Loads an image file from disk and uploads it to the GPU,
            returning a handle for use in a MaterialData.

         \param[in] path   Path to the image file on disk.
         \param[in] isSRGB Should be true for color data (albedo,
                            emissive) and false for data textures (normal
                            maps, metallic-roughness, occlusion) - getting
                            this wrong silently double-gamma-corrects those
                            maps.
         \return A texture handle.

         \details
            Cached by resolved path - calling this twice with the same
            path returns the same handle instead of decoding/uploading
            twice.
         *********************************************************************/
        ENGINE_API TextureHandle CreateTexture(const std::string& path, bool isSRGB = true);

        /*********************************************************************
         \brief
            Same as CreateTexture() but from an already-loaded image
            file's bytes in memory (still a file format stb_image
            understands - PNG/JPG/etc, not raw pixels).

         \param[in] fileBytes Pointer to the encoded image file's bytes.
         \param[in] byteCount Number of bytes at fileBytes.
         \param[in] isSRGB    See CreateTexture().
         \param[in] cacheKey  Optional; pass one (e.g. the glTF image
                               index) to dedupe repeated calls the same way
                               the path-based overload does.
         \return A texture handle.

         \details
            e.g. a glTF texture whose image lives in a binary buffer view
            rather than as a standalone file.
         *********************************************************************/
        ENGINE_API TextureHandle CreateTextureFromMemory(const unsigned char* fileBytes, size_t byteCount,
            bool isSRGB = true, const std::string& cacheKey = std::string());

        /*********************************************************************
         \brief
            Creates a new material from the given texture bindings +
            scalar factors.

         \param[in] data The material's texture bindings and scalar
                          factors.
         \return A handle for an entity's MeshRenderer::material.

         \details
            Any texture left Invalid falls back to a neutral placeholder -
            see CreateDefaultTextures().
         *********************************************************************/
        ENGINE_API MaterialHandle CreateMaterial(const MaterialData& data);

        /*********************************************************************
         \brief
            Rewrites an existing material in place.

         \param[in] handle The material to update.
         \param[in] data   The material's new texture bindings and scalar
                            factors.

         \details
            This is what lets a designer change a material's texture or
            tint while the Editor is running without every entity using
            that material needing a new handle. Cheap (no Vulkan sync) if
            only `factors` changed; if any texture handle differs from
            what this material currently has, does one graphics-queue
            wait-idle before rewriting the descriptor set (see the .cpp) -
            fine for an occasional designer edit, not something to call
            every frame.
         *********************************************************************/
        ENGINE_API void UpdateMaterial(MaterialHandle handle, const MaterialData& data);

        /*********************************************************************
         \brief  Reads back a material's current texture bindings/scalar
                 factors.
         \param[in] handle The material to look up.
         \return Const reference to the material's data.
         *********************************************************************/
        ENGINE_API const MaterialData& GetMaterialData(MaterialHandle handle) const;

        /*********************************************************************
         \brief  The default material handle.
         \return Handle to the neutral white-albedo/flat-normal/no-effect
                 material.
         \note  Substituted by RenderExtractSystem for any entity whose
                MeshRenderer::material is Invalid so every draw always has
                a material descriptor set bound (the shader never has to
                branch on "no material").
         *********************************************************************/
        MaterialHandle GetDefaultMaterial() const { return m_defaultMaterial; }

        // ---- Lights & environment ------------------------------------------------------------

        /*********************************************************************
         \brief
            Queues one light for this frame.

         \param[in] light The light, in world space (see LightData in
                           RenderTypes.h for the field conventions).

         \details
            Same per-frame contract as Submit(): call once per light per
            frame (RenderExtractSystem does this for every enabled ECS
            Light component); the queue is consumed and cleared at the end
            of Update(). At most kMaxLights (RenderTypes.h) are used per
            frame - extras are dropped with a one-time warning.

            If NOTHING is submitted in a frame, the Renderer lights the
            scene with one built-in directional "sun" (the direction and
            strength of the old hardcoded shader light) so a scene with no
            Light entities is never black.
         *********************************************************************/
        ENGINE_API void SubmitLight(const LightData& light);

        /*********************************************************************
         \brief
            Sets this frame's sky, ambient and exposure.

         \param[in] settings The environment to render this frame with.

         \details
            Per-frame like SubmitLight(): reset to GetDefaultEnvironment()
            at the end of every Update(). RenderExtractSystem calls this
            from the scene's Skybox component; with no such entity the
            defaults apply (built-in procedural sky, default ambient,
            exposure 1). An Invalid settings.skybox also means "the
            built-in procedural sky".
         *********************************************************************/
        ENGINE_API void SetEnvironment(const EnvironmentSettings& settings);

        /*********************************************************************
         \brief  The environment used whenever SetEnvironment() wasn't
                 called this frame.
         \return The default EnvironmentSettings.
         *********************************************************************/
        const EnvironmentSettings& GetDefaultEnvironment() const { return m_defaultEnvironment; }

        /*********************************************************************
         \brief
            Creates a cubemap from six square face images of equal size.

         \param[in] facePaths Image file paths in Vulkan/OpenGL cube face
                                order: +X, -X, +Y, -Y, +Z, -Z (right, left,
                                top, bottom, front, back).
         \param[in] isSRGB    true for ordinary colour images (the usual
                                case for LDR skybox sets).

         \return A handle for Skybox::cubemap / EnvironmentSettings::skybox.

         \details
            Cached by the joined path list, like CreateTexture(). Throws if
            a face fails to load, isn't square, or doesn't match face 0's
            size.

         \note
            Face sets exported for other engines sometimes use a different
            handedness; if the sky looks mirrored or a face is rotated,
            swap +X/-X (or +Z/-Z) in facePaths.
         *********************************************************************/
        ENGINE_API CubemapHandle CreateCubemap(const std::array<std::string, 6>& facePaths, bool isSRGB = true);

        /*********************************************************************
         \brief
            Creates a cubemap from one equirectangular (latitude/longitude,
            2:1) panorama - the format HDRI sites such as Poly Haven ship.

         \param[in] path     Image path. .hdr files are loaded as true HDR
                               (values above 1.0 kept); LDR formats
                               (.png/.jpg/...) are converted to linear.
         \param[in] faceSize Resolution of each generated cube face.

         \return A handle for Skybox::cubemap / EnvironmentSettings::skybox.

         \details
            Resampled on the CPU into six faces (bilinear), stored as
            RGBA16F so HDR range survives. Cached by path + faceSize.
            Throws if the file can't be loaded.
         *********************************************************************/
        ENGINE_API CubemapHandle CreateCubemapFromEquirect(const std::string& path, uint32_t faceSize = 512);

        /*********************************************************************
         \brief  The built-in procedural sky (a blue gradient with a soft
                 sun glow matching the fallback sun light), generated at
                 startup. Used whenever no valid skybox is set.
         \return Handle to the built-in sky cubemap.
         *********************************************************************/
        CubemapHandle GetDefaultSkybox() const { return m_defaultSkybox; }

        /*********************************************************************
         \brief  The built-in primitive meshes (Cube, Capsule, Cone,
                 Cylinder, Disc, Plane, Pyramid, Sphere), created at
                 startup. Names via GetMeshName(). ResourceManager
                 registers each under a fixed built-in asset id.
         \return The handles, in creation order.
         *********************************************************************/
        const std::vector<MeshHandle>& GetPrimitiveMeshes() const { return m_primitiveMeshes; }

        /*********************************************************************
         \brief
            Creates an offscreen render target (color+depth image, own
            camera, own per-frame-in-flight uniform buffer/descriptor
            set).

         \param[in] width  Viewport width, in pixels.
         \param[in] height Viewport height, in pixels.
         \return A handle to the new viewport.

         \details
            The Engine/Game build never calls these, so m_viewports stays
            empty and RecordCommandBuffer takes the direct-to-window path
            with zero extra branching cost beyond a single empty-container
            check.

            Typical Editor-panel usage:
                if (!m_viewport.IsValid()) m_viewport = renderer.CreateViewport(w, h);
                if (panelResized) renderer.ResizeViewport(m_viewport, newW, newH);
                ImGui::Image((ImTextureID)myCachedDescriptorSet, size);   // see note on AddTexture below
                // on panel close / shutdown:
                renderer.DestroyViewport(m_viewport);
         *********************************************************************/
        ENGINE_API ViewportHandle CreateViewport(uint32_t width, uint32_t height);

        /*********************************************************************
         \brief  Resizes an existing viewport's color/depth images.
         \param[in] handle The viewport to resize.
         \param[in] width  New width, in pixels.
         \param[in] height New height, in pixels.
         *********************************************************************/
        ENGINE_API void           ResizeViewport(ViewportHandle handle, uint32_t width, uint32_t height);

        /*********************************************************************
         \brief  Destroys a viewport's GPU resources.
         \param[in] handle The viewport to destroy.
         *********************************************************************/
        ENGINE_API void           DestroyViewport(ViewportHandle handle);

        /*********************************************************************
         \brief  Shows or hides DebugDraw lines in one viewport (off by
                 default: the Editor turns it on for the Scene view only).
         \param[in] handle  The viewport.
         \param[in] enabled true to draw debug lines into it.
         *********************************************************************/
        ENGINE_API void           SetViewportDebugDraw(ViewportHandle handle, bool enabled);

        /*********************************************************************
         \brief  Shows or hides the selection outline (SetOutlinedObject) in
                 one viewport. Off by default; the Editor turns it on for the
                 Scene view only.
         *********************************************************************/
        ENGINE_API void           SetViewportOutline(ViewportHandle handle, bool enabled);

        /*********************************************************************
         \brief  Pauses or resumes rendering into one viewport. An inactive
                 viewport costs nothing per frame and keeps its last image
                 (the Game view uses this while the scene has no camera).
         *********************************************************************/
        ENGINE_API void           SetViewportActive(ViewportHandle handle, bool active);

        /*********************************************************************
         \brief
            Which object gets the orange selection outline, by the userData
            its render proxy was created with (RenderExtractSystem uses the
            packed Entity). 0 = none.
         \details
            Drawn only in viewports with SetViewportOutline(true): the
            object is rendered into a mask with no depth test (so it shows
            through walls), then the mask's edge is blended on top.
         *********************************************************************/
        ENGINE_API void           SetOutlinedObject(std::uint64_t userData);

        /*********************************************************************
         \brief  Outline colour (sRGB, a = opacity) and thickness in pixels
                 (1..3).
         *********************************************************************/
        ENGINE_API void           SetOutlineStyle(const glm::vec4& color, float thicknessPixels);

        /*********************************************************************
         \brief  The sun's cascaded shadow settings (see ShadowSettings).
                 Changes apply next frame.
         *********************************************************************/
        ShadowSettings&           GetShadowSettings() { return m_shadowSettings; }

        /*********************************************************************
         \brief  Bloom settings (see PostSettings). Changes apply next frame.
         *********************************************************************/
        PostSettings&             GetPostSettings() { return m_postSettings; }

        /*********************************************************************
         \brief
            Queues one batch of particles for this frame (normally one
            emitter - ParticleSystem does this). Like Submit(), forgotten
            after the frame.

         \param[in] desc      Texture, blend mode, flipbook layout and the
                              batch's world bounds.
         \param[in] particles The particles (copied).
         \param[in] count     How many.

         \details
            Per view, batches outside the frustum are skipped, the rest are
            drawn far to near, and alpha-blended batches have their
            particles sorted back to front. Particles are unlit; to light
            the scene, also SubmitLight() a point light for the emitter.
         *********************************************************************/
        ENGINE_API void SubmitParticles(const ParticleDrawDesc& desc, const GpuParticle* particles, uint32_t count);

        /*********************************************************************
         \brief  Shows or hides DebugDraw lines when the scene is drawn
                 straight to the window (the Game build, where there are no
                 viewports). Off by default; turn on for gameplay debugging.
         \param[in] enabled true to draw debug lines into the window.
         *********************************************************************/
        void                      SetDebugDrawInWindow(bool enabled) { m_debugInWindow = enabled; }

        /*********************************************************************
         \brief  Accessor for a viewport's independent camera.
         \param[in] handle The viewport to look up.
         \return Mutable reference to that viewport's Camera.
         *********************************************************************/
        ENGINE_API Camera&        GetViewportCamera(ViewportHandle handle);

        /*********************************************************************
         \brief  Accessor for the shared viewport sampler.
         \return The sampler handle.
         \note  One sampler is shared by every viewport image (they're
                just being displayed, not filtered differently), so there
                is exactly one to hand to ImGui_ImplVulkan_AddTexture().
         *********************************************************************/
        ENGINE_API vk::Sampler    GetViewportSampler() const { return *m_viewportSampler; }

        /*********************************************************************
         \brief  Accessor for a viewport's current color image view.
         \param[in] handle The viewport to look up.
         \return The color image view.
         *********************************************************************/
        ENGINE_API vk::ImageView  GetViewportColorView(ViewportHandle handle) const;

        /*********************************************************************
         \brief
            Queues one draw for this frame.

         \param[in] mesh     The mesh to draw.
         \param[in] material The material to draw it with.
         \param[in] model    The object's world-space model matrix.

         \details
            Call once per visible object per frame (the ECS extraction
            step). The queue is consumed and cleared at the end of
            Update(). Pass GetDefaultMaterial() (or let the caller's own
            fallback logic do so - see RenderExtractSystem) rather than an
            Invalid MaterialHandle; DrawRenderQueue always binds a
            material descriptor set per draw.
         *********************************************************************/
        ENGINE_API void Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model,
                               const glm::vec4& params = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

        // ---- Render proxies (persistent, culled objects) -------------------------------------

        /*********************************************************************
         \brief
            Registers a persistent renderable object (normally one per ECS
            entity with a MeshRenderer - RenderExtractSystem does this).

         \param[in] userData Returned by Raycast() hits (RenderExtractSystem
                             stores the packed Entity).
         \return A handle for SetRenderProxy/DestroyRenderProxy.

         \details
            Unlike Submit() (re-sent every frame, culled brute force), a
            proxy stays registered and lives in the Renderer's SpatialIndex,
            so each view's frustum culling and editor ray picking are tree/
            array queries instead of loops over everything. It starts hidden
            until SetRenderProxy() gives it a mesh.
         *********************************************************************/
        ENGINE_API RenderProxyHandle CreateRenderProxy(std::uint64_t userData);

        /*********************************************************************
         \brief
            Sets what a proxy draws and where. Cheap to call every frame:
            identical values are a no-op, and a move only touches the
            spatial index (see SpatialIndex for how moving objects are
            handled).

         \param[in] handle   The proxy.
         \param[in] mesh     Mesh to draw (Invalid = nothing).
         \param[in] material Material (Invalid = the default material).
         \param[in] model    World matrix.
         \param[in] visible  false hides it (removed from culling/picking).
         \param[in] params   Per-object material parameters (InstanceData::
                             params): x = emissive multiplier, yzw spare.
         *********************************************************************/
        ENGINE_API void SetRenderProxy(RenderProxyHandle handle, MeshHandle mesh, MaterialHandle material,
                                       const glm::mat4& model, bool visible,
                                       const glm::vec4& params = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

        /*********************************************************************
         \brief  Unregisters a proxy; its handle may be reused.
         *********************************************************************/
        ENGINE_API void DestroyRenderProxy(RenderProxyHandle handle);

        /*********************************************************************
         \brief
            Exact ray pick against every visible proxy: candidates come from
            the spatial index (box tests), then the ray is tested against the
            mesh's actual triangles.

         \param[in]  origin      Ray start (world space).
         \param[in]  direction   Ray direction (any length).
         \param[in]  maxDistance Ignore hits farther than this.
         \param[out] outHit      Nearest hit (userData, distance, point).
         \return true if anything was hit.
         *********************************************************************/
        ENGINE_API bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RenderRaycastHit& outHit) const;

        /*********************************************************************
         \brief  Last frame's culling numbers for an editor viewport.
         *********************************************************************/
        ENGINE_API RenderCullStats GetViewportCullStats(ViewportHandle handle) const;

        /*********************************************************************
         \brief  Last frame's culling numbers for the window pass (Game build).
         *********************************************************************/
        const RenderCullStats& GetWindowCullStats() const { return m_windowCullStats; }

        /*********************************************************************
         \brief  The spatial index behind culling/picking (read-only).
         *********************************************************************/
        const SpatialIndex& GetSpatialIndex() const { return m_spatial; }

        /*********************************************************************
         \brief  Registers a callback invoked while the frame's render
                 pass is still open (after the scene draw, before
                 endRendering).
         \param[in] cb The callback to invoke.
         \note  Lets Core forward to layers (e.g. the Editor's ImGui).
         *********************************************************************/
        void SetOverlayCallback(std::function<void()> cb) { m_overlayCallback = std::move(cb); }

        /*********************************************************************
         \brief  Builds the plain-data bundle ImGui's Vulkan backend needs
                 to initialize itself.
         \return A filled-in ImGuiVulkanContext.
         *********************************************************************/
        ENGINE_API ImGuiVulkanContext   GetImguiContext()       const;

        /*********************************************************************
         \brief  Accessor for the current frame's command buffer.
         \return The current command buffer.
         *********************************************************************/
        ENGINE_API vk::CommandBuffer    GetCurrentCommandBuff() const;

    private:
        WindowManager&              m_window;
        std::function<void()>       m_overlayCallback;

        VulkanContext               m_context;
        VulkanSwapchain             m_swapchain;
        VulkanPipeline              m_pipeline;
        VulkanFrameManager          m_frameManager;

        // Pending mesh/texture/cubemap copies: recorded into each frame's command buffer ahead of
        // its passes (RecordCommandBuffer), their staging buffers freed once that frame slot's
        // fence has signalled (DrawFrame). Declared after m_context (its staging buffers are
        // suballocated from the context's allocator, so they must be destroyed first).
        VulkanUploadQueue           m_uploads;

        // Scene
        Camera                      m_camera;            // main (game) camera - see SetMainCamera
        bool                        m_hasMainCamera = false;
        std::vector<GpuMesh>        m_meshes;
        std::vector<RenderItem>     m_renderQueue;      // immediate Submit() items, this frame only

        // Persistent render proxies + the spatial index they live in (see CreateRenderProxy).
        struct RenderProxy {
            MeshHandle           mesh;
            MaterialHandle       material;
            glm::mat4            model{ 1.0f };
            glm::vec4            params{ 1.0f, 0.0f, 0.0f, 0.0f };   // InstanceData::params
            std::uint64_t        userData = 0;
            SpatialIndex::Handle spatial = SpatialIndex::kInvalid;   // kInvalid = not in the index (hidden / no bounds yet)
            bool                 visible = false;
            bool                 alive = false;
            bool                 awaitingBounds = false;              // mesh still uploading (CreateMeshAsync)
        };
        std::vector<RenderProxy>    m_proxies;
        std::vector<uint32_t>       m_freeProxies;
        std::vector<uint32_t>       m_proxiesAwaitingBounds;
        SpatialIndex                m_spatial{ 0.25f };
        std::vector<RenderItem>     m_visibleItems;     // scratch: one view's culled + sorted draw list
        std::vector<RenderItem>     m_casterItems;      // scratch: one shadow cascade's casters
        ViewDrawList                m_windowDraws;      // the window pass's batches (no viewports)
        RenderCullStats             m_windowCullStats;
        std::vector<MeshHandle>     m_primitiveMeshes;
        void CreatePrimitiveMeshes();

        // Textures + Materials (see the "---- Textures ----"/"---- Materials ----" public API).
        std::vector<GpuTexture>                     m_textures;
        std::unordered_map<std::string, uint32_t>   m_textureCacheByKey;   // path or explicit cacheKey -> TextureHandle::id
        vk::raii::Sampler                           m_materialSampler = nullptr;

        // Pool(s) BEFORE the sets they allocate (matches m_descriptorPool/m_descriptorSets
        // below): members are destroyed in REVERSE declaration order, so declaring the pools
        // first means they are destroyed LAST - after every GpuMaterial's vk::raii::DescriptorSet
        // has already run its own destructor (which frees itself against whichever pool allocated
        // it). The opposite order compiles fine but destroys a pool while sets allocated from it
        // still exist, which is exactly the "Couldn't find VkDescriptorPool"/"Invalid
        // VkDescriptorSet" validation errors this fixed once already - see the fix's own commit
        // for the exact symptom if this ever regresses.
        //
        // A VkDescriptorPool can't be resized once created, so unbounded material counts mean
        // unbounded POOLS, not one pool sized "big enough": m_materials.size() used to be capped
        // hard at a single pool's fixed maxSets (kMaterialsPerDescriptorPool), and CreateMaterial
        // had no check for that at all - the (kMaterialsPerDescriptorPool + 1)-th material would
        // throw a raw vk::OutOfPoolMemoryError with no indication of what actually went wrong.
        // CreateMaterial now allocates a fresh pool block on demand instead (see its own comment),
        // so every pool after the first is a plain vector growth, same as any other resource here.
        std::vector<vk::raii::DescriptorPool>       m_materialDescriptorPools;
        uint32_t                                    m_materialsInCurrentPool = 0;   // toward the current (last) pool's cap
        std::vector<GpuMaterial>                    m_materials;
        static constexpr uint32_t                   kMaterialsPerDescriptorPool = 256;

        TextureHandle                               m_defaultWhiteTexture;    // (255,255,255,255)
        TextureHandle                               m_defaultNormalTexture;   // flat tangent-space normal (128,128,255,255)
        MaterialHandle                              m_defaultMaterial;

        // Cubemaps (skyboxes). Pool BEFORE the cubemaps whose descriptor sets it allocates -
        // same destruction-order rule as m_materialDescriptorPools/m_materials above.
        static constexpr uint32_t                   kMaxCubemaps = 64;
        vk::raii::DescriptorPool                    m_cubemapDescriptorPool = nullptr;
        vk::raii::Sampler                           m_skyboxSampler = nullptr;
        vk::raii::Sampler                           m_envSampler = nullptr;     // environment maps: blends between mip levels
        std::vector<GpuCubemap>                     m_cubemaps;
        std::unordered_map<std::string, uint32_t>   m_cubemapCacheByKey;   // joined paths / path+size -> CubemapHandle::id
        CubemapHandle                               m_defaultSkybox;

        // Lights & environment (per frame, like m_renderQueue): filled by SubmitLight()/
        // SetEnvironment(), consumed by DrawFrame(), reset at the end of Update().
        std::vector<GpuLight>                       m_lightQueue;
        bool                                        m_warnedTooManyLights = false;
        uint32_t                                    m_frameLightCount = 0;   // lights actually uploaded this frame (after fallback/clamp)
        EnvironmentSettings                         m_defaultEnvironment;
        EnvironmentSettings                         m_environment;

        std::vector<VulkanBuffer>                   m_uniformBuffers;
        std::vector<void*>                          m_uniformBuffersMapped;

        // One light storage buffer per frame in flight (kMaxLights GpuLights each), persistently
        // mapped like the uniform buffers. Shared by the window's set 0 AND every viewport's set
        // 0 for that frame index - lights are world state, not per-camera.
        std::vector<VulkanBuffer>                   m_lightBuffers;
        std::vector<void*>                          m_lightBuffersMapped;

        // Debug lines: one host-visible vertex buffer per frame in flight, grown on demand (see
        // UpdateDebugBuffer). Depth-tested vertices first, then the on-top ones.
        static constexpr uint32_t                   kMaxDebugVertices = 2'000'000;
        std::vector<VulkanBuffer>                   m_debugBuffers;
        std::vector<void*>                          m_debugBuffersMapped;
        std::vector<uint32_t>                       m_debugBufferCapacity;   // in vertices
        std::vector<DebugVertex>                    m_debugTested;           // scratch, reused every frame
        std::vector<DebugVertex>                    m_debugOnTop;
        uint32_t                                    m_debugTestedCount = 0;
        uint32_t                                    m_debugOnTopCount = 0;
        bool                                        m_debugInWindow = false;
        bool                                        m_warnedDebugOverflow = false;

        // Instances: every object drawn this frame by every pass of every view, one model matrix
        // each (set 0, binding 2). Built on the CPU by PrepareView(), copied in one go by
        // UploadInstances() into this frame's host-visible buffer, which grows (power of two) as
        // needed. Shared by the window's and every viewport's set 0 for that frame index.
        std::vector<InstanceData>                   m_instanceScratch;
        std::vector<VulkanBuffer>                   m_instanceBuffers;
        std::vector<void*>                          m_instanceBuffersMapped;
        std::vector<uint32_t>                       m_instanceCapacity;     // in instances
        static constexpr uint32_t                   kInitialInstanceCapacity = 16384;

        // Cascaded sun shadows: one D32 array image (a layer per cascade) shared by every view -
        // each view renders its own cascades into it right before its main pass. It rests in
        // SHADER_READ_ONLY whenever it isn't being rendered, so it is always valid to sample.
        static constexpr vk::Format                 kShadowFormat = vk::Format::eD32Sfloat;
        static constexpr uint32_t                   kShadowMapSize = 2048;
        ShadowSettings                              m_shadowSettings;
        VulkanImage                                 m_shadowImage;
        vk::raii::ImageView                         m_shadowArrayView = nullptr;
        std::array<vk::raii::ImageView, kMaxShadowCascades> m_shadowLayerViews{ nullptr, nullptr, nullptr, nullptr };
        vk::raii::Sampler                           m_shadowSampler = nullptr;
        int32_t                                     m_shadowLightIndex = -1;          // into this frame's light buffer
        glm::vec3                                   m_shadowLightDir{ 0.0f, -1.0f, 0.0f };

        // Selection outline (SetOutlinedObject / SetViewportOutline).
        std::uint64_t                               m_outlineUserData = 0;
        uint32_t                                    m_outlineProxy = 0xFFFFFFFFu;     // cached lookup of m_outlineUserData
        OutlineCompositePush                        m_outlineStyle;

        // Clustered lights. m_frameLights is this frame's light buffer as uploaded (directional
        // lights first). Every prepared view appends its kClustersPerView cells and their light
        // indices to the two scratch arrays, which UploadClusters() copies into this frame's
        // buffers (set 0, bindings 6 and 7 - shared by every set like the light buffer).
        std::vector<GpuLight>                       m_frameLights;
        uint32_t                                    m_frameDirLightCount = 0;
        uint32_t                                    m_preparedViews = 0;      // views prepared so far this frame
        std::vector<ClusterCell>                    m_clusterGridScratch;
        std::vector<uint32_t>                       m_clusterIndexScratch;
        std::vector<uint32_t>                       m_clusterCounts;          // scratch: per cell, one view
        std::vector<std::array<uint16_t, 6>>        m_clusterRanges;          // scratch: per light, cell range
        std::vector<uint32_t>                       m_clusterRangeLights;     // scratch: which light each range is
        std::vector<VulkanBuffer>                   m_clusterGridBuffers;
        std::vector<void*>                          m_clusterGridMapped;
        std::vector<VulkanBuffer>                   m_clusterIndexBuffers;
        std::vector<void*>                          m_clusterIndexMapped;
        static constexpr uint32_t                   kMaxClusterViews = 9;          // kMaxViewports + the window
        static constexpr uint32_t                   kMaxClusterIndices = 1u << 19; // light references per frame, all views

        // Particles (SubmitParticles): this frame's submitted batches, the per-view sorted copy
        // that is uploaded (set 0, binding 5; grows like the instance buffer), and one small
        // descriptor set per texture used by particles. Pool BEFORE the sets it allocates.
        struct ParticleSubmit { ParticleDrawDesc desc; uint32_t first = 0; uint32_t count = 0; };
        std::vector<GpuParticle>                    m_particleQueue;
        std::vector<ParticleSubmit>                 m_particleSubmits;
        std::vector<GpuParticle>                    m_particleScratch;
        std::vector<std::pair<float, uint32_t>>     m_particleOrder;        // scratch: (distance, submit index)
        std::vector<VulkanBuffer>                   m_particleBuffers;
        std::vector<void*>                          m_particleBuffersMapped;
        std::vector<uint32_t>                       m_particleCapacity;     // in particles
        static constexpr uint32_t                   kInitialParticleCapacity = 16384;
        static constexpr uint32_t                   kMaxParticlesPerFrame = 1u << 20;
        static constexpr uint32_t                   kMaxParticleTextures = 128;
        bool                                        m_warnedParticleOverflow = false;
        vk::raii::DescriptorPool                    m_particleTexturePool = nullptr;
        std::unordered_map<uint32_t, vk::raii::DescriptorSet> m_particleTextureSets;   // TextureHandle::id -> set

        // Post-processing (HDR scene image -> bloom -> tonemap). Pool BEFORE m_windowPost and
        // m_viewports, whose PostTargets hold sets allocated from it.
        PostSettings                                m_postSettings;
        uint32_t                                    m_msaaSamples = 1;      // in use now (PostSettings::msaaSamples, clamped to the GPU)
        uint32_t                                    m_drawSamples = 1;      // samples of the target DrawRenderQueue is drawing into
        float                                       m_time = 0.0f;          // seconds since Init (FrameUniforms::time.x)
        float                                       m_deltaTime = 0.0f;     // last Update()'s dt (FrameUniforms::time.y)
        vk::raii::DescriptorPool                    m_postDescriptorPool = nullptr;
        PostTargets                                 m_windowPost;           // the window's own targets (standalone build)

        vk::raii::DescriptorPool                    m_descriptorPool = nullptr;
        std::vector<vk::raii::DescriptorSet>        m_descriptorSets;

        // Viewports (see ViewportHandle). Empty in a standalone Engine/Game build. The pools are
        // declared BEFORE m_viewports so they are destroyed after the viewports' descriptor sets
        // (same destruction-order rule as the material pools above).
        static constexpr uint32_t                  kMaxViewports = 8;
        vk::raii::DescriptorPool                   m_viewportDescriptorPool = nullptr;
        vk::raii::DescriptorPool                   m_outlineDescriptorPool = nullptr;
        std::unordered_map<uint32_t, Viewport>     m_viewports;
        uint32_t                                   m_nextViewportId = 0;
        vk::raii::Sampler                          m_viewportSampler = nullptr;
        vk::raii::Sampler                          m_pointSampler = nullptr;    // no filtering: for reading depth images

        // Pending async mesh uploads (see CreateMeshAsync).
        struct PendingMeshUpload { MeshHandle handle; MeshData data; std::string debugName; };
        std::deque<PendingMeshUpload> m_pendingMeshUploads;
        static constexpr uint32_t     kMaxMeshUploadsPerFrame = 2;

        // Vulkan helpers
        void RecordCommandBuffer(FrameContext& fc);
        void CreateUniformBuffers();
        void CreateDescriptorPool();
        void CreateDescriptorSets();
        void ProcessPendingMeshUploads();
        void UploadMesh(MeshHandle handle, const MeshData& data, const std::string& debugName);

        // Records the bind-and-draw loop over a view's main batches against whatever color/depth
        // attachments are currently bound (the swapchain, or a Viewport). Shared by the direct-
        // to-window path and the offscreen-viewport path so they can never drift apart.
        void DrawRenderQueue(vk::CommandBuffer cmd, vk::Extent2D extent, vk::DescriptorSet cameraSet, const ViewDrawList& draws);

        // ---- Particles ----
        void CreateParticleResources();
        // Culls this frame's submitted batches against one view, orders them far to near (and
        // sorts alpha-blended particles back to front), and appends them to m_particleScratch.
        void PrepareParticles(const Camera& camera, ViewDrawList& out, RenderCullStats& stats);
        // Copies m_particleScratch into this frame's particle buffer (grows it if needed).
        void UploadParticles(uint32_t frameIndex);
        // The descriptor set binding `texture` for the particle pipeline (created on first use).
        vk::DescriptorSet GetParticleTextureSet(TextureHandle texture);
        void DrawParticles(vk::CommandBuffer cmd, vk::DescriptorSet frameSet, const std::vector<ParticleBatch>& batches);

        // ---- Clustered lights ----
        void CreateClusterBuffers();
        // Bins this frame's point/spot lights into one view's cluster grid (appended to the
        // scratch arrays) and writes the grid's parameters into `ubo`. Returns how many lights
        // reach the view.
        uint32_t BuildLightClusters(const Camera& camera, vk::Extent2D extent, FrameUniforms& ubo);
        void UploadClusters(uint32_t frameIndex);

        // ---- Post-processing ----
        void CreatePostDescriptorPool();
        // (Re)creates a view's HDR image and bloom chain at the given size and points its
        // descriptor sets at them. The caller makes sure the GPU is idle when replacing.
        void CreatePostTargets(PostTargets& targets, uint32_t width, uint32_t height);
        // The scene pass: meshes, sky and particles into the view's HDR image (clearing colour
        // and depth), then the bloom chain. Leaves the HDR image and bloom ready to be sampled,
        // and depth holding the scene's depth for the debug lines that follow.
        void DrawSceneHdr(FrameContext& fc, PostTargets& targets, vk::Image depthImage, vk::ImageView depthView,
                          vk::Extent2D extent, vk::DescriptorSet frameSet, const ViewDrawList& draws);
        void DrawBloom(FrameContext& fc, PostTargets& targets);
        /// Ambient occlusion for one view: half-size depth pre-pass, occlusion, blur. Leaves
        /// targets.aoBlur ready to sample (all white when PostSettings::ao is off).
        void DrawAmbientOcclusion(FrameContext& fc, PostTargets& targets, vk::DescriptorSet frameSet, const ViewDrawList& draws);
        /// Points a view's set 0 at the current environment map (binding 8) and at that view's
        /// ambient occlusion image (binding 9). Called every frame for the frame's own set.
        void WriteViewTextures(vk::DescriptorSet frameSet, const PostTargets& targets);
        /// Builds a cubemap's envImage and sh from its faces (called by UploadCubemapFaces).
        void BuildEnvironmentLighting(GpuCubemap& cube, const void* faceData, uint32_t faceSize, vk::Format format);
        const GpuCubemap& CurrentSkyCubemap() const;
        // Draws the fullscreen tonemap triangle into the currently-open (final, sRGB) pass.
        void DrawTonemap(vk::CommandBuffer cmd, const PostTargets& targets, vk::Extent2D extent);

        // ---- Frame preparation (before recording) ----
        // Culls and batches everything one view draws this frame (main pass, shadow cascades,
        // selection mask) into `out`, appending instances to m_instanceScratch, and writes the
        // view's FrameUniforms to `uniformDst`.
        void PrepareView(Camera& camera, vk::Extent2D extent, RenderCullStats& stats, ViewDrawList& out,
                         bool outline, void* uniformDst);
        // Appends `items` (already sorted) as batches: consecutive items with the same mesh (and
        // material, if byMaterial) become one instanced draw. Returns the number of instances.
        uint32_t AppendBatches(const std::vector<RenderItem>& items, std::vector<DrawBatch>& batches, bool byMaterial);
        // Copies m_instanceScratch into this frame's instance buffer, growing it (and repointing
        // binding 2 of this frame's sets) if needed.
        void UploadInstances(uint32_t frameIndex);
        void CreateInstanceBuffers();
        // Points set 0's shared bindings (1 lights, 2 instances, 3/4 shadow map) of each per-frame
        // set at this frame's resources.
        void WriteSharedFrameDescriptors(std::vector<vk::raii::DescriptorSet>& sets);

        // ---- Shadows ----
        void CreateShadowResources();
        // Fits cascade `count` cascades to `camera` (stable: bounding spheres + texel snapping)
        // and writes their matrices/splits into `ubo`. Returns each cascade's view-projection.
        void ComputeShadowCascades(const Camera& camera, uint32_t count, FrameUniforms& ubo,
                                   std::array<glm::mat4, kMaxShadowCascades>& outViewProj) const;
        // Renders a view's cascades into the shared shadow map (barriers included).
        void DrawShadowCascades(FrameContext& fc, vk::DescriptorSet frameSet, const ViewDrawList& draws);
        void DrawMeshBatches(vk::CommandBuffer cmd, const std::vector<DrawBatch>& batches);

        // ---- Selection outline ----
        // The selected proxy's id, or 0xFFFFFFFF (re-finds it if the cached id went stale).
        uint32_t ResolveOutlineProxy();
        void WriteOutlineDescriptor(Viewport& vp);
        void DrawOutlineMask(FrameContext& fc, Viewport& vp, vk::DescriptorSet frameSet);
        void DrawOutlineComposite(FrameContext& fc, Viewport& vp);

        // ---- Culling / proxies ----
        // Fills m_visibleItems with what `camera` can see (proxies via the spatial index + immediate
        // items brute force), sorted by (material, mesh), and records the counts in `stats`.
        void BuildVisibleItems(const Camera& camera, RenderCullStats& stats);
        // Re-derives a proxy's world box and (re)inserts/updates/removes it in the spatial index.
        void RefreshProxyBounds(uint32_t proxyId);
        // Draws every proxy's box (DebugCategory::Bounds): settled = blue, moving = orange.
        void DrawProxyBoundsDebug();

        // ---- Debug lines (DebugDraw.h) ----
        // Copies this frame's DebugDraw lines into m_debugBuffers[frameIndex] (growing it if
        // needed) and records the depth-tested / on-top vertex counts.
        void UpdateDebugBuffer(uint32_t frameIndex);
        // Draws them into the currently-open rendering pass (after meshes + sky).
        void DrawDebugLines(vk::CommandBuffer cmd, vk::Extent2D extent, vk::DescriptorSet frameSet);

        void CreateViewportSampler();
        void CreateViewportDescriptorPool();
        void CreateViewportImages(Viewport& vp, uint32_t width, uint32_t height);
        void CreateViewportUniformsAndDescriptors(Viewport& vp);
        Viewport& GetViewportOrThrow(ViewportHandle handle);
        const Viewport& GetViewportOrThrow(ViewportHandle handle) const;

        // ---- Lights/environment/skybox helpers ----
        void CreateLightBuffers();
        // Copies this frame's m_lightQueue (or the fallback sun, if it's empty) into
        // m_lightBuffers[frameIndex] and records how many were written in m_frameLightCount.
        void UpdateLightBuffer(uint32_t frameIndex);
        // Everything FrameUniforms holds besides view/proj/cameraPos: ambient, exposure, skybox
        // intensity, light count. Shared by the window and viewport paths so they can't drift.
        void FillEnvironmentUniforms(FrameUniforms& ubo) const;
        void CreateSkyboxSampler();
        void CreateCubemapDescriptorPool();
        void CreateDefaultSkybox();
        // Shared core of every cubemap-creation path: uploads six tightly-packed faces (+X, -X,
        // +Y, -Y, +Z, -Z, each faceSize^2 texels of bytesPerTexel) to a new cube image, creates
        // its cube view and descriptor set, and returns the new handle.
        CubemapHandle UploadCubemapFaces(const void* faceData, uint32_t faceSize, vk::Format format,
                                         uint32_t bytesPerTexel, const std::string& debugName);
        // Draws the current environment's sky into the currently-open rendering pass. Must run
        // AFTER the opaque draws (it relies on their depth to skip covered pixels).
        void DrawSkybox(vk::CommandBuffer cmd, vk::DescriptorSet frameSet);
        uint32_t SupportedMsaaSamples(uint32_t wanted) const;   ///< Highest of 4 / 2 / 1 that is <= wanted and the GPU can render to.
        void ApplyMsaaSetting();                                ///< Rebuilds every view's targets when PostSettings::msaaSamples changed.

        // ---- Texture/Material helpers ----
        // Shared core of every texture-creation path: creates a new device-local image + view
        // for already-decoded RGBA8 pixels and queues the copy into it (m_uploads). Format is
        // eR8G8B8A8Srgb or eUnorm depending on isSRGB - see CreateTexture's doc comment on when
        // each is correct.
        TextureHandle UploadTexturePixels(const unsigned char* pixels, uint32_t width, uint32_t height, bool isSRGB);
        // 1x1 solid-color texture, used by CreateDefaultTextures() and directly reusable for any
        // other flat-color placeholder need.
        TextureHandle CreateSolidColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool isSRGB);
        void CreateDefaultTextures();
        void CreateMaterialSampler();
        // Allocates ONE new descriptor pool block (kMaterialsPerDescriptorPool materials' worth)
        // and appends it to m_materialDescriptorPools. Called once up front for the first block,
        // and again by CreateMaterial() whenever the current block is full - see that function.
        void CreateMaterialDescriptorPool();
        // Writes all five combined-image-sampler bindings of `set` from `data`, substituting
        // m_defaultWhiteTexture/m_defaultNormalTexture for any Invalid handle in `data`.
        void WriteMaterialDescriptorSet(vk::DescriptorSet set, const MaterialData& data);
        const GpuTexture& GetTextureOrThrow(TextureHandle handle) const;

        vk::raii::CommandBuffer BeginSingleTimeCommand();
        void EndSingleTimeCommand(vk::raii::CommandBuffer&& cmdBuff);

        // Generalized from a swapchain-color-only helper so it can also transition the depth
        // image: takes the target image and aspect mask explicitly instead of assuming color.
        void TransitionImageLayout(FrameContext fc, vk::Image image, vk::ImageAspectFlags aspectMask,
                                   vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                   vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
                                   vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask,
                                   uint32_t layerCount = 1);

    };  // RENDERER CLASS

}   // NAMESPACE CRUCIBLE
