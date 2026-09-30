#pragma once
/*************************************************************************
* \file:         ImGuiContext.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Plain-data bundle of the Vulkan handles ImGui's Vulkan
*                backend needs to initialize itself.
*
* \details
*                Declares ImGuiVulkanContext, filled in by
*                Renderer::GetImguiContext() (see RenderSystem.h/.cpp) from
*                the engine's own VulkanContext/VulkanSwapchain, and
*                consumed by whichever layer wires up
*                ImGui_ImplVulkan_Init() (the Editor). Kept as a standalone
*                C-style struct of raw Vk* handles (rather than passing
*                vk::raii types directly) since ImGui's C backend expects
*                plain Vulkan handles, not the RAII wrappers this engine
*                otherwise uses everywhere else.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <vulkan/vulkan_raii.hpp>
#include <GLFW/glfw3.h>

namespace Crucible {

    /*************************************************************************
     \brief
        Plain-data bundle of everything ImGui_ImplVulkan_Init() needs,
        filled in by Renderer::GetImguiContext().

     \details
        Every field is a raw Vulkan handle (or GLFW window pointer) rather
        than a vk::raii wrapper, matching ImGui's C-style Vulkan backend
        API exactly.
     *************************************************************************/
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
