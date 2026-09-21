#pragma once

#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "Math/Math.h"

namespace Crucible {

    struct Vertex {
        Math::vec2 pos;
        Math::vec3 color;

        static vk::VertexInputBindingDescription GetBindingDescription() {
            return { .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex };
        }
        static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescriptions() {
            return { { {.location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, pos)},
                {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, color)} } };
        }
    };

    class VulkanPipeline {

    public:
        void CreatePipeline(VulkanContext& context, VulkanSwapchain& swapchain);
        void CleanUp();

        const vk::raii::Pipeline& GetPipeline() const;
        const vk::raii::PipelineLayout& GetLayout() const;

    private:
        std::vector<char> ReadFile(std::string const& filename);
        [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(VulkanContext& context, std::vector<char> const& code);

    private:
        vk::raii::Pipeline          m_graphicsPipeline = nullptr;
        vk::raii::PipelineLayout    m_pipelineLayout = nullptr;

    };  // VULKAN PIPELINE CLASS

}   // NAMESPACE CRUCIBLE