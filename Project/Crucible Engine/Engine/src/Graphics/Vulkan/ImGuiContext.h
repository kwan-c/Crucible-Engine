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
        // The swapchain's depth attachment format. ImGui's own pipeline draws inside the
        // SAME open dynamic-rendering pass as the scene (Renderer's overlay callback runs
        // before endRendering()), and that pass has a real depth attachment bound - so ImGui's
        // pipeline must declare a matching depthAttachmentFormat too, or the validation layer
        // flags every ImGui draw call (VUID-vkCmdDrawIndexed-dynamicRenderingUnusedAttachments-08914).
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;

        uint32_t minImageCount = 2;
        uint32_t imageCount = 2;

        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

        VkAllocationCallbacks* allocator = nullptr;

    }; // ImGuiVulkanContext struct

}   // NAMESPACE CRUCIBLE