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

#include "Camera.h"
#include "RenderTypes.h"

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
        };

        // An offscreen render target: its own color+depth image, camera, and per-frame-in-flight
        // uniform buffer/descriptor set, so several can be drawn per frame with different cameras
        // (Scene view vs. Game view) without stepping on each other's data.
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
         \brief  Advances demo-scene time, drains any pending async mesh
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
         \brief  Refreshes the given frame-in-flight's camera uniform
                 buffer from the main camera's current view/projection.
         \param[in] frameIndex Index of the frame in flight to update.
         *********************************************************************/
        void UpdateFrameUniforms(uint32_t frameIndex);

        /*********************************************************************
         \brief  Accessor for the active (main) camera.
         \return Mutable reference to the main camera.
         \note  The renderer owns the active camera; a controller (or the
                ECS) mutates it each frame before Update(). Aspect ratio is
                kept in sync with the swapchain automatically.
         *********************************************************************/
        Camera&       GetCamera()       { return m_camera; }

        /*********************************************************************
         \brief  Accessor for the active (main) camera.
         \return Const reference to the main camera.
         *********************************************************************/
        const Camera& GetCamera() const { return m_camera; }

        /*********************************************************************
         \brief
            Uploads mesh data to the GPU immediately (blocks until the
            copy completes) and returns a handle for use in an entity's
            MeshRenderer component.

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
            Returns a handle immediately; the actual GPU upload is
            throttled across frames (see kMaxMeshUploadsPerFrame) instead
            of blocking.

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
        ENGINE_API void Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model);

        /*********************************************************************
         \brief  Enables/disables the temporary demo scene.
         \param[in] enable true to spawn/keep spawning the demo scene each
                            frame.
         \note  Temporary: spawns a grid of spinning cubes so you can test
                camera + multi-draw before the ECS components exist.
         *********************************************************************/
        void EnableDemoScene(bool enable) { m_demoScene = enable; }

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

        // Scene
        Camera                      m_camera;
        std::vector<GpuMesh>        m_meshes;
        std::vector<RenderItem>     m_renderQueue;
        std::vector<MeshHandle>     m_primitiveMeshes;
        void CreatePrimitiveMeshes();
        MaterialHandle              m_demoMaterial;   // demo-scene cubes' material (see SubmitDemoScene)

        MeshHandle                  m_glbMesh;
        MaterialHandle              m_glbMaterial;

        float                       m_time = 0.0f;
        bool                        m_demoScene = true;

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

        vk::raii::DescriptorPool                    m_descriptorPool = nullptr;
        std::vector<vk::raii::DescriptorSet>        m_descriptorSets;

        // Viewports (see ViewportHandle). Empty in a standalone Engine/Game build.
        static constexpr uint32_t                  kMaxViewports = 8;
        std::unordered_map<uint32_t, Viewport>     m_viewports;
        uint32_t                                   m_nextViewportId = 0;
        vk::raii::DescriptorPool                   m_viewportDescriptorPool = nullptr;
        vk::raii::Sampler                          m_viewportSampler = nullptr;

        // Pending async mesh uploads (see CreateMeshAsync).
        struct PendingMeshUpload { MeshHandle handle; MeshData data; std::string debugName; };
        std::deque<PendingMeshUpload> m_pendingMeshUploads;
        static constexpr uint32_t     kMaxMeshUploadsPerFrame = 2;

        // Vulkan helpers
        void RecordCommandBuffer(FrameContext& fc);
        void CreateUniformBuffers();
        void CreateDescriptorPool();
        void CreateDescriptorSets();
        void SubmitDemoScene();
        void ProcessPendingMeshUploads();
        void UploadMesh(MeshHandle handle, const MeshData& data, const std::string& debugName);

        // Records the bind-and-draw loop over m_renderQueue against whatever color/depth
        // attachments are currently bound (the swapchain, or a Viewport). Shared by the direct-
        // to-window path and the offscreen-viewport path so they can never drift apart.
        void DrawRenderQueue(vk::CommandBuffer cmd, vk::Extent2D extent, vk::DescriptorSet cameraSet);

        void CreateViewportSampler();
        void CreateViewportDescriptorPool();
        void CreateViewportImages(Viewport& vp, uint32_t width, uint32_t height);
        void CreateViewportUniformsAndDescriptors(Viewport& vp);
        void UpdateViewportUniforms(Viewport& vp, uint32_t frameIndex);
        Viewport& GetViewportOrThrow(ViewportHandle handle);
        const Viewport& GetViewportOrThrow(ViewportHandle handle) const;

        void CopyBufferToImage(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height,
                               uint32_t layerCount = 1);

        // ---- Lights/environment/skybox helpers ----
        void CreateLightBuffers();
        // Copies this frame's m_lightQueue (or the fallback sun, if it's empty) into
        // m_lightBuffers[frameIndex] and records how many were written in m_frameLightCount.
        void UpdateLightBuffer(uint32_t frameIndex);
        // Everything FrameUniforms holds besides view/proj/cameraPos: ambient, exposure, skybox
        // intensity, light count. Shared by the window and viewport paths so they can't drift.
        void FillEnvironmentUniforms(FrameUniforms& ubo) const;
        // Points binding 1 (the light buffer) of each per-frame set 0 at m_lightBuffers[i].
        void WriteLightBufferDescriptors(std::vector<vk::raii::DescriptorSet>& sets);
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

        // ---- Texture/Material helpers ----
        // Shared core of every texture-creation path: uploads already-decoded RGBA8 pixels to a
        // new device-local image + view (same staging-buffer-and-barrier dance CreateTextureImage
        // used to do ad hoc for the one hardcoded texture). Format is eR8G8B8A8Srgb or eUnorm
        // depending on isSRGB - see CreateTexture's doc comment on when each is correct.
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

        void TransitionImageLayout_I(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Image& image, 
                                   vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t layerCount = 1);

        // Generalized from a swapchain-color-only helper so it can also transition the depth
        // image: takes the target image and aspect mask explicitly instead of assuming color.
        void TransitionImageLayout(FrameContext fc, vk::Image image, vk::ImageAspectFlags aspectMask,
                                   vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                   vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
                                   vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask);

    };  // RENDERER CLASS

}   // NAMESPACE CRUCIBLE
