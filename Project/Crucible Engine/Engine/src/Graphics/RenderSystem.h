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

// Imgui
#include "Vulkan/ImGuiContext.h"
#include "EngineExport.h"

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

        // Renderer data members accessors
        VkDescriptorPool                GetVkDescriptorPool()   const { return *m_descriptorPool; }
        ENGINE_API ImGuiVulkanContext   GetImguiContext()       const;
        ENGINE_API vk::CommandBuffer    GetCurrentCommandBuff() const;

    private:
        WindowManager&          m_window;
        std::function<void()>   m_overlayCallback;

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