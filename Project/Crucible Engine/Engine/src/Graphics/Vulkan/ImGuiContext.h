#pragma once

#include <vulkan/vulkan_raii.hpp>
#include <GLFW/glfw3.h>

namespace Crucible {

    struct ImGuiVulkanContext {
        GLFWwindow* window = nullptr;

        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;

        VkQueue graphicsQueue = VK_NULL_HANDLE;
        uint32_t graphicsQueueFamily = 0;

        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;

        VkFormat colorFormat = VK_FORMAT_UNDEFINED;

        uint32_t minImageCount = 2;
        uint32_t imageCount = 2;

        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

        VkAllocationCallbacks* allocator = nullptr;

    }; // ImGuiVulkanContext struct

}   // NAMESPACE CRUCIBLE