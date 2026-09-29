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
        // a handle for use in MeshRendererComponent. Use for meshes needed right away: built-in
        // primitives, or a handful of assets loaded at startup.
        ENGINE_API MeshHandle CreateMesh(const MeshData& data);

        // Returns a handle immediately; the actual GPU upload is throttled across frames
        // (see kMaxMeshUploadsPerFrame) instead of blocking. Use this for meshes streamed in at
        // runtime (e.g. the Resource Manager's .glb loader) so a burst of loaded models doesn't
        // stall a single frame. The mesh simply isn't drawn until its upload completes.
        ENGINE_API MeshHandle CreateMeshAsync(MeshData data);

        MeshHandle            GetCubeMesh() const { return m_cubeMesh; }

        // ---- Draw submission ----
        // Call once per visible object per frame (the ECS extraction step). The queue is
        // consumed and cleared at the end of Update().
        ENGINE_API void Submit(MeshHandle mesh, const glm::mat4& model);

        // Temporary: spawns a grid of spinning cubes so you can test camera + multi-draw
        // before the ECS components exist.
        void EnableDemoScene(bool enable) { m_demoScene = enable; }

        // Optional hook invoked while the frame's render pass is still open (after the scene
        // draw, before endRendering). Lets Core forward to layers (e.g. the Editor's ImGui).
        void SetOverlayCallback(std::function<void()> cb) { m_overlayCallback = std::move(cb); }

        // Renderer data members accessors
        VkDescriptorPool                GetVkDescriptorPool()   const { return *m_descriptorPool; }
        ENGINE_API ImGuiVulkanContext   GetImguiContext()       const;
        ENGINE_API vk::CommandBuffer    GetCurrentCommandBuff() const;

    private:
        WindowManager&              m_window;
        std::function<void()>       m_overlayCallback;

        VulkanContext               m_context;
        VulkanSwapchain             m_swapchain;
        VulkanPipeline              m_pipeline;
        VulkanFrameManager          m_frameManager;

        VulkanImage                 m_image;

        // Scene
        Camera                      m_camera;
        std::vector<GpuMesh>        m_meshes;
        std::vector<RenderItem>     m_renderQueue;
        MeshHandle                  m_cubeMesh;
        float                       m_time = 0.0f;
        bool                        m_demoScene = true;

        std::vector<VulkanBuffer>   m_uniformBuffers;
        std::vector<void*>          m_uniformBuffersMapped;

        vk::raii::DescriptorPool                m_descriptorPool = nullptr;
        std::vector<vk::raii::DescriptorSet>    m_descriptorSets;

        // Pending async mesh uploads (see CreateMeshAsync).
        struct PendingMeshUpload { MeshHandle handle; MeshData data; };
        std::deque<PendingMeshUpload> m_pendingMeshUploads;
        static constexpr uint32_t     kMaxMeshUploadsPerFrame = 2;

        // Vulkan helpers
        void RecordCommandBuffer(FrameContext& fc);
        void CreateUniformBuffers();
        void CreateDescriptorPool();
        void CreateDescriptorSets();
        void SubmitDemoScene();
        void ProcessPendingMeshUploads();
        void UploadMesh(MeshHandle handle, const MeshData& data);

        void CreateTextureImage(const std::string&);
        void CopyBufferToImage(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height);

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
