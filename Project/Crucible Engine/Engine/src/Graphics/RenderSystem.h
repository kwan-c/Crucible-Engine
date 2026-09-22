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
#include <functional>

// Crucible Engine
#include "Systems/WindowManager.h"
#include "Math/Math.h"
 
#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanSwapchain.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanFrameManager.h"
#include "Vulkan/VulkanBuffer.h"

// Imgui
#include "Vulkan/ImGuiContext.h"
#include "EngineExport.h"

namespace Crucible {

    class Renderer {
        
        // Temp for testing.
        const std::vector<Vertex> vertices = {
            {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
            {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
            {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
            {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.5f, 0.8f}},

            {{-0.5f, -0.5f, 0.5f}, {0.8f, 0.0f, 0.8f}},
            {{0.5f, -0.5f, 0.5f}, {0.0f, 0.8f, 0.5f}},
            {{0.5f, 0.5f, 0.5f}, {0.8f, 0.8f, 0.0f}},
            {{-0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}}
        };
        const std::vector<uint32_t> indices = {
            // Back face: z = -0.5
            0, 3, 2,
            0, 2, 1,

            // Front face: z = +0.5
            4, 5, 6,
            4, 6, 7,

            // Left face: x = -0.5
            0, 4, 7,
            0, 7, 3,

            // Right face: x = +0.5
            1, 2, 6,
            1, 6, 5,

            // Bottom face: y = -0.5
            0, 1, 5,
            0, 5, 4,

            // Top face: y = +0.5
            3, 7, 6,
            3, 6, 2
        };

    public:
        Renderer(WindowManager& window);
        void ShutDown();

        void Update(float dt);
        void DrawFrame();
        void UpdateUniformBuffer(uint32_t frameIndex);

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

        VulkanBuffer                m_vertexBuffer;
        VulkanBuffer                m_indexBuffer;

        std::vector<VulkanBuffer>   m_uniformBuffers;
        std::vector<void*>          m_uniformBuffersMapped;

        vk::raii::DescriptorPool                m_descriptorPool = nullptr;
        std::vector<vk::raii::DescriptorSet>    m_descriptorSets;

        // Vulkan helpers
        void RecordCommandBuffer(FrameContext& fc);
        void CreateVertexBuffer();
        void CreateIndexBuffer();
        void CreateUniformBuffers();
        void CreateDescriptorPool();
        void CreateDescriptorSets();

        void TransitionImageLayout(FrameContext fc, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                   vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
                                   vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask);

    };  // RENDERER CLASS

}   // NAMESPACE CRUCIBLE