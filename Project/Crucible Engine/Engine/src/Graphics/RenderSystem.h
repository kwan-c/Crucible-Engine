#pragma once

#define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS  // To handle swap chain recreation.
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
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

namespace {
    struct RenderData {

    };  // RENDERDATA STRUCT

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    class Renderer {
        
        // Temp for testing.
        const std::vector<Vertex> vertices = {
            { {0.5f, 0.5f}, {1.0f, 0.0f, 0.0f} },
            { {0.0f, -0.5f}, {0.0f, 1.0f, 0.0f} },
            { {-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f} }
        };

    public:
        Renderer(WindowManager& window);
        ~Renderer();

        void Update(float dt);
        void DrawFrame();

        // Optional hook invoked while the frame's render pass is still open (after the scene
        // draw, before endRendering). Lets Core forward to layers (e.g. the Editor's ImGui).
        void SetOverlayCallback(std::function<void()> cb) { m_overlayCallback = std::move(cb); }

        // Blocks until the GPU is idle. Call before tearing down anything the GPU may still use.
        void WaitIdle() { m_device.waitIdle(); }

        // Renderer data members accessors
        VkDescriptorPool    GetVkDescriptorPool() const { return *m_descriptorPool; }

    private:

        // Raw Vulkan handles required by ImGui/Vulkan backend
        VkInstance           GetVkInstance() const { return *m_instance; }
        VkPhysicalDevice     GetVkPhysicalDevice() const { return *m_physicalDevice; }
        VkDevice             GetVkDevice() const { return *m_device; }
        VkQueue              GetVkQueue() const { return *m_graphicsQueue; }
        VkDescriptorPool     GetVkDescriptorPool() const { return *m_descriptorPool; }
        VkCommandPool        GetVkCommandPool() const { return *m_commandPool; }
        VkCommandBuffer      GetCurrentCommandBuffer() const { return *m_commandBuffers[m_frameIndex]; }
        const VkFormat*      GetVkColorAttachmentFormats() const { return &m_colorAttachmentFormat; }
        uint32_t             GetVkQueueFamily() const { return m_queueIndex; }
        uint32_t             GetVkMinImageCount() const { return ChooseSwapMinImageCount(m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface)); }
        uint32_t             GetVkImageCount() const { return static_cast<uint32_t>(m_swapChainImages.size()); }

    private:
        std::vector<RenderData> m_renderCache;
        WindowManager& m_window;
        std::function<void()> m_overlayCallback;

        VulkanContext           m_context;
        VulkanSwapchain         m_swapchain;
        VulkanPipeline          m_pipeline;
        VulkanFrameManager      m_frameManager;

        // Vulkan helpers
        void RecordCommandBuffer(FrameContext& fc);

        // Input Buffers
        void CreateVertexBuffer();
        uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

        // Vulkan data members
        vk::raii::DescriptorPool    m_descriptorPool = nullptr;

        vk::raii::Buffer            m_vertexBuffer = nullptr;
        vk::raii::DeviceMemory      m_vertexBufferMemory = nullptr;

        void TransitionImageLayout(FrameContext fc, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                   vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
                                   vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask);

    };  // RENDERER CLASS

}   // NAMESPACE CRUCIBLE