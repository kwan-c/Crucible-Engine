#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <GLFW/glfw3.h>

// STL
#include <vector>

// Crucible engine
#include "Graphics/Pipeline.h"

namespace {
    struct RenderData {

    };  // RENDERDATA STRUCT

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    class Renderer {
        
    public:
        Renderer(GLFWwindow* window);
        void Init(GLFWwindow* window);
        void Update(float dt);

    private:
        std::vector<RenderData> m_renderCache;

        // Core Vulkan functions
        void InitVulkan(GLFWwindow* window);

        // Vulkan utility helpers
        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface(GLFWwindow* window);
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateSwapChain(GLFWwindow* window);
        void CreateImageViews();

        std::vector<const char*> GetRequiredInstanceExtensions();
        vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
        vk::PresentModeKHR ChooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);
        vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, GLFWwindow* window);
        uint32_t ChooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities);
        bool IsDeviceSuitable(const vk::raii::PhysicalDevice& device);

        // Vulkan data members
        vk::raii::Context           m_context;
        vk::raii::Instance          m_instance = nullptr;           // connection btw app and vulk lib
        vk::raii::SurfaceKHR        m_surface = nullptr;
        vk::raii::PhysicalDevice    m_physicalDevice = nullptr;     // graphics card
        vk::raii::Device            m_device = nullptr;             // logical interface to physical device
        vk::raii::Queue             m_graphicsQueue = nullptr;
        vk::raii::SwapchainKHR      m_swapChain = nullptr;

        // SwapChain parameters
        std::vector<vk::Image>              m_swapChainImages;
        std::vector<vk::raii::ImageView>    m_swapChainImageViews;
        vk::Extent2D                        m_swapChainExtent;
        vk::SurfaceFormatKHR                m_swapChainSurfaceFormat;

        vk::raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;

        static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT         severity,
                                                              vk::DebugUtilsMessageTypeFlagsEXT                type,
                                                              const vk::DebugUtilsMessengerCallbackDataEXT*    pCallbackData,
                                                              void*                                            pUserData);


    };  // RENDERER CLASS

}   // NAMESPACE CRUCIBLE