#pragma once

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
        Renderer(WindowManager& window);
        void ShutDown();

        void Update(float dt);
        void DrawFrame();
        void UpdateFrameUniforms(uint32_t frameIndex);

        // ---- Camera ----
        // The renderer owns the active camera; a controller (or the ECS) mutates it each frame
        // before Update(). Aspect ratio is kept in sync with the swapchain automatically.
        Camera&       GetCamera()       { return m_camera; }
        const Camera& GetCamera() const { return m_camera; }

        // ---- Meshes ----
        // Uploads mesh data to the GPU immediately (blocks until the copy completes) and returns
        // a handle for use in an entity's MeshRenderer component (ECS/Components/Components.h;
        // extracted into a Submit() call every frame by Graphics/RenderExtractSystem.h). Use for
        // meshes needed right away: built-in primitives, or a handful of assets loaded at startup.
        // debugName is optional and purely descriptive (see GetMeshName()) - pass the source
        // asset's own name when you have one (a glTF mesh's "name", say), otherwise leave it
        // empty and a generic "Mesh <id>" is used.
        ENGINE_API MeshHandle CreateMesh(const MeshData& data, const std::string& debugName = {});

        // Returns a handle immediately; the actual GPU upload is throttled across frames
        // (see kMaxMeshUploadsPerFrame) instead of blocking. Use this for meshes streamed in at
        // runtime (e.g. the Resource Manager's .glb loader) so a burst of loaded models doesn't
        // stall a single frame. The mesh simply isn't drawn until its upload completes.
        ENGINE_API MeshHandle CreateMeshAsync(MeshData data, const std::string& debugName = {});

        // The Renderer is the source of truth for "which mesh is id 3" - not any one Editor
        // panel. A name is descriptive metadata about a GPU resource, so it lives right next to
        // the resource (GpuMesh::debugName) instead of a separate map some panel keeps in sync by
        // hand: there's no single place in the Editor where "a mesh was just created" happens (a
        // primitive in Renderer's own constructor, a .glb load, gameplay code later), so any
        // panel-owned map would inevitably drift, and every panel wanting to show a name (today
        // just Inspector, maybe Hierarchy or a future asset browser tomorrow) would otherwise need
        // its own copy kept in sync independently. Centralizing it here also means it's available
        // to anything that isn't a panel at all - a debug log line, a future
        // vkSetDebugUtilsObjectNameEXT call for RenderDoc/Nsight captures, and so on.
        // Returns a reference into m_meshes - same lifetime caveat as GetMaterialData(): valid
        // until the next structural change to the mesh list, so don't hold it across frames.
        ENGINE_API const std::string& GetMeshName(MeshHandle handle) const;

        // ---- Textures ----
        // Loads an image file from disk and uploads it to the GPU, returning a handle for use in
        // a MaterialData. Cached by resolved path - calling this twice with the same path returns
        // the same handle instead of decoding/uploading twice. isSRGB should be true for color
        // data (albedo, emissive) and false for data textures (normal maps, metallic-roughness,
        // occlusion) - getting this wrong silently double-gamma-corrects those maps.
        ENGINE_API TextureHandle CreateTexture(const std::string& path, bool isSRGB = true);

        // Same as above but from an already-loaded image file's bytes in memory (still a file
        // format stb_image understands - PNG/JPG/etc, not raw pixels) - e.g. a glTF texture whose
        // image lives in a binary buffer view rather than as a standalone file. cacheKey is
        // optional; pass one (e.g. the glTF image index) to dedupe repeated calls the same way
        // the path-based overload does.
        ENGINE_API TextureHandle CreateTextureFromMemory(const unsigned char* fileBytes, size_t byteCount,
            bool isSRGB = true, const std::string& cacheKey = std::string());

        // ---- Materials ----
        // Creates a new material from the given texture bindings + scalar factors (any texture
        // left Invalid falls back to a neutral placeholder - see CreateDefaultTextures). Returns a
        // handle for an entity's MeshRenderer::material.
        ENGINE_API MaterialHandle CreateMaterial(const MaterialData& data);

        // Rewrites an existing material in place - this is what lets a designer change a
        // material's texture or tint while the Editor is running without every entity using that
        // material needing a new handle. Cheap (no Vulkan sync) if only `factors` changed; if any
        // texture handle differs from what this material currently has, does one graphics-queue
        // wait-idle before rewriting the descriptor set (see the .cpp) - fine for an occasional
        // designer edit, not something to call every frame.
        ENGINE_API void UpdateMaterial(MaterialHandle handle, const MaterialData& data);

        ENGINE_API const MaterialData& GetMaterialData(MaterialHandle handle) const;

        // Neutral white-albedo/flat-normal/no-effect material, substituted by RenderExtractSystem
        // for any entity whose MeshRenderer::material is Invalid so every draw always has a
        // material descriptor set bound (the shader never has to branch on "no material").
        MaterialHandle GetDefaultMaterial() const { return m_defaultMaterial; }

        // ---- Viewports (offscreen render targets for the Editor) ----
        // The Engine/Game build never calls these, so m_viewports stays empty and
        // RecordCommandBuffer takes the direct-to-window path with zero extra branching cost
        // beyond a single empty-container check.
        //
        // Typical Editor-panel usage:
        //     if (!m_viewport.IsValid()) m_viewport = renderer.CreateViewport(w, h);
        //     if (panelResized) renderer.ResizeViewport(m_viewport, newW, newH);
        //     ImGui::Image((ImTextureID)myCachedDescriptorSet, size);   // see note on AddTexture below
        //     // on panel close / shutdown:
        //     renderer.DestroyViewport(m_viewport);
        ENGINE_API ViewportHandle CreateViewport(uint32_t width, uint32_t height);
        ENGINE_API void           ResizeViewport(ViewportHandle handle, uint32_t width, uint32_t height);
        ENGINE_API void           DestroyViewport(ViewportHandle handle);

        ENGINE_API Camera&        GetViewportCamera(ViewportHandle handle);
        // One sampler is shared by every viewport image (they're just being displayed, not
        // filtered differently), so there is exactly one to hand to ImGui_ImplVulkan_AddTexture().
        ENGINE_API vk::Sampler    GetViewportSampler() const { return *m_viewportSampler; }
        ENGINE_API vk::ImageView  GetViewportColorView(ViewportHandle handle) const;

        // ---- Draw submission ----
        // Call once per visible object per frame (the ECS extraction step). The queue is
        // consumed and cleared at the end of Update(). Pass GetDefaultMaterial() (or let the
        // caller's own fallback logic do so - see RenderExtractSystem) rather than an Invalid
        // MaterialHandle; DrawRenderQueue always binds a material descriptor set per draw.
        ENGINE_API void Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model);

        // Temporary: spawns a grid of spinning cubes so you can test camera + multi-draw
        // before the ECS components exist.
        void EnableDemoScene(bool enable) { m_demoScene = enable; }

        // Optional hook invoked while the frame's render pass is still open (after the scene
        // draw, before endRendering). Lets Core forward to layers (e.g. the Editor's ImGui).
        void SetOverlayCallback(std::function<void()> cb) { m_overlayCallback = std::move(cb); }

        // Renderer data members accessors
        ENGINE_API ImGuiVulkanContext   GetImguiContext()       const;
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

        std::vector<VulkanBuffer>                   m_uniformBuffers;
        std::vector<void*>                          m_uniformBuffersMapped;

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

        void CopyBufferToImage(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height);

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
                                   vk::ImageLayout oldLayout, vk::ImageLayout newLayout);

        // Generalized from a swapchain-color-only helper so it can also transition the depth
        // image: takes the target image and aspect mask explicitly instead of assuming color.
        void TransitionImageLayout(FrameContext fc, vk::Image image, vk::ImageAspectFlags aspectMask,
                                   vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                   vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
                                   vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask);

    };  // RENDERER CLASS

}   // NAMESPACE CRUCIBLE
