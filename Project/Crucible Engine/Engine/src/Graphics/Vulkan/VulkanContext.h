#pragma once

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
    #include <vulkan/vulkan_raii.hpp>
#else
    import vulkan_hpp;
#endif
#include <GLFW/glfw3.h>

// VMA (Vulkan Memory Allocator) - declarations only here (no VMA_IMPLEMENTATION defined in this
// header). Exactly one .cpp (VulkanContext.cpp) additionally re-includes this with
// VMA_IMPLEMENTATION defined to compile the library body once. VMA_STATIC_VULKAN_FUNCTIONS
// defaults to 1 whenever VK_NO_PROTOTYPES isn't defined (it isn't here), so VMA calls the core
// vkAllocateMemory/vkCreateBuffer/etc. entry points directly against the Vulkan loader we already
// link (Vulkan::Vulkan) - no VmaVulkanFunctions table needs to be filled in by hand.
#include <vk_mem_alloc.h>

// Crucible Engine
#include "Systems/WindowManager.h"

namespace Crucible {

    class VulkanContext {

    public:
        VulkanContext(WindowManager& window);
        ~VulkanContext();
        void ShutDown();

        // VulkanContext owns the one VmaAllocator for the whole engine (see m_allocator below);
        // it is never meaningfully copyable or movable (nothing in the codebase does either -
        // every user takes it by reference), so say so explicitly rather than let the destructor
        // we just declared silently suppress the implicit move members too.
        VulkanContext(const VulkanContext&)            = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;
        VulkanContext(VulkanContext&&)                 = delete;
        VulkanContext& operator=(VulkanContext&&)      = delete;

        const vk::raii::Instance&       GetInstance() const;
        const vk::raii::PhysicalDevice& GetPhysicalDevice() const;
        const vk::raii::Device&         GetDevice() const;
        const vk::raii::Queue&          GetGraphicsQueue() const;

        vk::SurfaceKHR                  GetSurface() const;
        uint32_t                        GetGraphicsQueueFamily() const;

        // The single suballocator every VulkanBuffer/VulkanImage allocates its GPU memory from
        // (see VulkanResource.h/.cpp) instead of each doing its own vkAllocateMemory. Valid from
        // the end of construction until just before ~VulkanContext() tears down the device.
        VmaAllocator                     GetAllocator() const;

    private:
        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface(WindowManager& window);

        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateAllocator();

        bool IsDeviceSuitable(const vk::raii::PhysicalDevice& device);
        std::vector<const char*> GetRequiredInstanceExtensions();

        static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT          severity,
                                                              vk::DebugUtilsMessageTypeFlagsEXT                 type,
                                                              const vk::DebugUtilsMessengerCallbackDataEXT*     pCallbackData,
                                                              void*                                             pUserData);

    private:
        vk::raii::Context                   m_context;
        vk::raii::Instance                  m_instance = nullptr;
        vk::raii::DebugUtilsMessengerEXT    m_debugMessenger = nullptr;

        vk::raii::SurfaceKHR                m_surface = nullptr;
        vk::raii::PhysicalDevice            m_physicalDevice = nullptr;
        vk::raii::Device                    m_device = nullptr;
        vk::raii::Queue                     m_graphicsQueue = nullptr;

        uint32_t                            m_queueIndex = ~0u;

        // Declared last so it is created last and destroyed FIRST among VulkanContext's own
        // members (C++ destroys members in reverse declaration order) - it must be torn down
        // while m_device/m_instance are still alive, since vmaDestroyAllocator() calls back into
        // the device. It must also outlive every VulkanBuffer/VulkanImage allocated from it; that
        // holds automatically here because m_context is itself declared (in Renderer) before
        // m_meshes/m_textures, so this allocator isn't destroyed until after every mesh/texture
        // buffer or image that used it has already freed its allocation in its own destructor.
        // Not a vk::raii type: VMA is a C API, so this is a raw handle we create/destroy by hand
        // in CreateAllocator()/~VulkanContext().
        VmaAllocator                         m_allocator = VK_NULL_HANDLE;

    };  // VULKAN CONTEXT CLASS

}   // NAMESPACE CRUCIBLE