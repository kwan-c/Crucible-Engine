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

// Crucible Engine
#include "Systems/WindowManager.h"
#include "Math/Math.h"
 
#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanSwapchain.h"
#include "Vulkan/VulkanFrameManager.h"

namespace {
    struct RenderData {

    };  // RENDERDATA STRUCT

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    struct Vertex {
        Math::vec2 pos;
        Math::vec3 color;

        static vk::VertexInputBindingDescription GetBindingDescription() {
            return { .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex };
        }
        static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescriptions() {
            return {{ {.location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, pos)},
                {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, color)} }};
        }
    };

    class Renderer {
        
        // Temp for testing.
        const std::vector<Vertex> vertices = {
            { {0.0f, -0.5f}, {1.0f, 0.0f, 0.0f} },
            { {0.5f, 0.5f}, {0.0f, 1.0f, 0.0f} },
            { {-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f} }
        };

    public:
        Renderer(WindowManager& window);
        ~Renderer();

        void Update(float dt);
        void DrawFrame();

        // Renderer data members accessors
        VkDescriptorPool     GetVkDescriptorPool() const { return *m_descriptorPool; }
        VkCommandPool        GetVkCommandPool() const { return *m_commandPool; }

    private:
        std::vector<RenderData> m_renderCache;
        WindowManager& m_window;

        // Core functions
        void InitVulkan();

        VulkanContext       m_context;
        VulkanSwapchain     m_swapchain;
        VulkanFrameManager  m_frameManager;

        // Vulkan helpers
        void CreateGraphicsPipeline();

        void RecordCommandBuffer(FrameContext& fc);

        // Input Buffers
        void CreateVertexBuffer();
        uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

        [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(const std::vector<char>& code) const;

        // Vulkan data members
        vk::raii::PipelineLayout            m_pipelineLayout = nullptr;
        vk::raii::Pipeline                  m_graphicsPipeline = nullptr;
        vk::raii::DescriptorPool            m_descriptorPool = nullptr;

        uint32_t    m_frameIndex = 0;

        vk::raii::CommandPool                   m_commandPool = nullptr;
        std::vector<vk::raii::CommandBuffer>    m_commandBuffers;
        vk::raii::Buffer                        m_vertexBuffer = nullptr;
        vk::raii::DeviceMemory                  m_vertexBufferMemory = nullptr;

        // Synchonization primitives
        std::vector<vk::raii::Semaphore>    m_presentCompleteSemaphores;
        std::vector<vk::raii::Semaphore>    m_renderFinishedSemaphores;
        std::vector<vk::raii::Fence>        m_inFlightFences;



        void TransitionImageLayout(FrameContext fc, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                   vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
                                   vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask);

    };  // RENDERER CLASS

}   // NAMESPACE CRUCIBLE