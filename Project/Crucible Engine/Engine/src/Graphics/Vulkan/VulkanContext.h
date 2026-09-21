#pragma once

#define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS  // To handle swap chain recreation.
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
    #include <vulkan/vulkan_raii.hpp>
#else
    import vulkan_hpp;
#endif
#include <GLFW/glfw3.h>

// Crucible Engine
#include "Systems/WindowManager.h"

namespace Crucible {

    class VulkanContext {

    public:
        VulkanContext() = default;
        ~VulkanContext() = default;

        void Init(WindowManager& window);
        void ShutDown();

        const vk::raii::Instance& GetInstance() const;
        const vk::raii::PhysicalDevice& GetPhysicalDevice() const;
        const vk::raii::Device& GetDevice() const;
        const vk::raii::Queue& GetGraphicsQueue() const;

        vk::SurfaceKHR GetSurface() const;
        uint32_t GetGraphicsQueueFamily() const;

    private:
        void CreateInstance(WindowManager& window);
        void SetupDebugMessenger();
        void CreateSurface(WindowManager& window);

        void PickPhysicalDevice();
        void CreateLogicalDevice();

        bool IsDeviceSuitable(const vk::raii::PhysicalDevice& device);
        std::vector<const char*> GetRequiredInstanceExtensions();

        static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT          severity,
                                                              vk::DebugUtilsMessageTypeFlagsEXT                 type,
                                                              const vk::DebugUtilsMessengerCallbackDataEXT*     pCallbackData,
                                                              void*                                             pUserData);

    private:
        vk::raii::Context m_context;
        vk::raii::Instance m_instance = nullptr;
        vk::raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;

        vk::raii::SurfaceKHR m_surface = nullptr;

        vk::raii::PhysicalDevice m_physicalDevice = nullptr;
        vk::raii::Device m_device = nullptr;

        vk::raii::Queue m_graphicsQueue = nullptr;

        uint32_t m_queueIndex = ~0u;

    };

}   // NAMESPACE CRUCIBLE