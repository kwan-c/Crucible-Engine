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
        void Init();
        void Update(float dt);

    private:
        std::vector<RenderData> m_renderCache;

        Pipeline m_pipeline{ 
            "../../../Assets/Shaders/Triangle.vert.spv", 
            "../../../Assets/Shaders/Triangle.frag.spv" , {} };

        // Core Vulkan functions
        void InitVulkan();

        // Vulkan utility helpers
        void CreateInstance();
        void SetupDebugMessenger();
        void PickPhysicalDevice();
        void CreateLogicalDevice();

        std::vector<const char*> GetRequiredInstanceExtensions();
        bool IsDeviceSuitable(const vk::raii::PhysicalDevice& device);

        // Vulkan data members
        vk::raii::Context           m_context;
        vk::raii::Instance          m_instance = nullptr;           // connection btw app and vulk lib
        vk::raii::PhysicalDevice    m_physicalDevice = nullptr;     // graphics card
        vk::raii::Device            m_device = nullptr;             // logical interface to physical device

        vk::raii::Queue             m_graphicsQueue = nullptr;

        vk::raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;

        static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT         severity,
                                                              vk::DebugUtilsMessageTypeFlagsEXT                type,
                                                              const vk::DebugUtilsMessengerCallbackDataEXT*    pCallbackData,
                                                              void*                                            pUserData);


    };  // RENDERER CLASS

}   // NAMESPACE CRUCIBLE