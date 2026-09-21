#pragma once

#include "VulkanContext.h"
#include "VulkanSwapchain.h"

namespace Crucible {

    class VulkanPipeline {

    public:
        void Init(VulkanContext& context, VulkanSwapchain& swapchain);
        void CleanUp();

        const vk::raii::Pipeline& GetPipeline() const;
        const vk::raii::PipelineLayout& GetLayout() const;

    private:
        vk::raii::ShaderModule CreateShaderModule(VulkanContext& context, std::vector<char> const& code);
        std::vector<char> ReadFile(std::string const& filename);

    private:
        vk::raii::Pipeline          m_graphicsPipeline = nullptr;
        vk::raii::PipelineLayout    m_pipelineLayout = nullptr;

    };  // VULKAN PIPELINE CLASS

}   // NAMESPACE CRUCIBLE