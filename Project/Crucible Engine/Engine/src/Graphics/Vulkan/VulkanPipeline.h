#pragma once

#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "Math/Math.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Crucible {

    // Vertex layout shared by every mesh source: procedural primitives (Renderer::Primitives),
    // and .glb imports (Resource Manager's glTF loader must convert into this layout).
    //   - normal: object-space normal. Required for lighting; glTF supplies NORMAL directly.
    //   - uv:     first UV set. Required for textured materials; glTF's TEXCOORD_0.
    //   - color:  vertex color tint, mainly for procedural/debug meshes. glTF meshes without a
    //             COLOR_0 attribute should have the loader fill this with {1,1,1}.
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec3 color;

        static vk::VertexInputBindingDescription GetBindingDescription() {
            return { .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex };
        }
        static std::array<vk::VertexInputAttributeDescription, 4> GetAttributeDescriptions() {
            return { {
                {.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, pos)},
                {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, normal)},
                {.location = 2, .binding = 0, .format = vk::Format::eR32G32Sfloat,    .offset = offsetof(Vertex, uv)},
                {.location = 3, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, color)}
            } };
        }
    };

    class VulkanPipeline {

    public:
        void CreateDescriptorSetLayout(VulkanContext& context);
        void CreatePipeline(VulkanContext& context, VulkanSwapchain& swapchain);
        void CleanUp();

        const vk::raii::Pipeline&               GetPipeline() const;
        const vk::raii::PipelineLayout&         GetLayout() const;
        const vk::raii::DescriptorSetLayout&    GetDescriptorSetLayout() const;

    private:
        std::vector<char> ReadFile(std::string const& filename);
        [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(VulkanContext& context, std::vector<char> const& code);

    private:
        vk::raii::Pipeline              m_graphicsPipeline  = nullptr;
        vk::raii::PipelineLayout        m_pipelineLayout    = nullptr;
        vk::raii::DescriptorSetLayout   m_descriptorSetLayout = nullptr;

    };  // VULKAN PIPELINE CLASS

}   // NAMESPACE CRUCIBLE
