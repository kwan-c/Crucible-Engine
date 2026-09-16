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

    private:
        std::vector<RenderData> m_renderCache;
        WindowManager& m_window;

        // Core functions
        void InitVulkan();

        // Vulkan helpers
        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface();

        void PickPhysicalDevice();
        void CreateLogicalDevice();

        void CreateSwapChain();
        void RecreateSwapChain();
        void CleanupSwapChain();
        void CreateImageViews();
        void CreateGraphicsPipeline();

        void CreateCommandPool();
        void CreateCommandBuffers();
        void RecordCommandBuffer(uint32_t imageIndex);
        void CreateSyncObjects();

        // Input Buffers
        void CreateVertexBuffer();
        uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);


        std::vector<const char*> GetRequiredInstanceExtensions();
        bool IsDeviceSuitable(const vk::raii::PhysicalDevice& device);
        vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
        vk::PresentModeKHR ChooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);
        vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, GLFWwindow* window);
        uint32_t ChooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities);

        [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(const std::vector<char>& code) const;

        // Vulkan data members
        vk::raii::Context                   m_context;
        vk::raii::Instance                  m_instance = nullptr;           // connection btw app and vulk lib
        vk::raii::DebugUtilsMessengerEXT    m_debugMessenger = nullptr;
        vk::raii::SurfaceKHR                m_surface = nullptr;
        vk::raii::PhysicalDevice            m_physicalDevice = nullptr;     // graphics card
        vk::raii::Device                    m_device = nullptr;             // logical interface to physical device
        vk::raii::Queue                     m_graphicsQueue = nullptr;
        vk::raii::SwapchainKHR              m_swapChain = nullptr;
        vk::raii::PipelineLayout            m_pipelineLayout = nullptr;
        vk::raii::Pipeline                  m_graphicsPipeline = nullptr;

        uint32_t    m_queueIndex = ~0u;            // index for graphics queue for logical device
        uint32_t    m_frameIndex = 0;

        vk::raii::CommandPool                   m_commandPool = nullptr;
        std::vector<vk::raii::CommandBuffer>    m_commandBuffers;
        vk::raii::Buffer                        m_vertexBuffer = nullptr;
        vk::raii::DeviceMemory                  m_vertexBufferMemory = nullptr;

        // SwapChain parameters
        std::vector<vk::Image>              m_swapChainImages;
        std::vector<vk::raii::ImageView>    m_swapChainImageViews;
        vk::Extent2D                        m_swapChainExtent;
        vk::SurfaceFormatKHR                m_swapChainSurfaceFormat;

        // Synchonization primitives
        std::vector<vk::raii::Semaphore>    m_presentCompleteSemaphores;
        std::vector<vk::raii::Semaphore>    m_renderFinishedSemaphores;
        std::vector<vk::raii::Fence>        m_inFlightFences;


        static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT         severity,
                                                              vk::DebugUtilsMessageTypeFlagsEXT                type,
                                                              const vk::DebugUtilsMessengerCallbackDataEXT*    pCallbackData,
                                                              void*                                            pUserData);
        void TransitionImageLayout(uint32_t imageIndex, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                   vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
                                   vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask);

    };  // RENDERER CLASS

}   // NAMESPACE CRUCIBLE