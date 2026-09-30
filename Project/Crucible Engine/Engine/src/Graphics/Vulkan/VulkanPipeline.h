#pragma once

#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "Math/Math.h"

#include <array>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Crucible {

    // Vertex layout shared by every mesh source: procedural primitives (Renderer::Primitives),
    // and .glb imports (GltfLoader converts into this layout - see AssetPipe/GltfLoader.h).
    //   - normal:  object-space normal. Required for lighting; glTF supplies NORMAL directly.
    //   - uv:      first UV set. Required for textured materials; glTF's TEXCOORD_0.
    //   - color:   vertex color tint, mainly for procedural/debug meshes. glTF meshes without a
    //              COLOR_0 attribute should have the loader fill this with {1,1,1}.
    //   - tangent: object-space tangent (xyz) + bitangent handedness sign (w, always +-1), for
    //              normal mapping. bitangent = cross(normal, tangent.xyz) * tangent.w - this is
    //              the same convention glTF's TANGENT attribute uses, so the loader can copy it
    //              through unchanged; procedural primitives compute it analytically (see
    //              Primitives.h's per-shape derivations).
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec3 color;
        glm::vec4 tangent;

        static vk::VertexInputBindingDescription GetBindingDescription() {
            return { .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex };
        }
        static std::array<vk::VertexInputAttributeDescription, 5> GetAttributeDescriptions() {
            return { {
                {.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,    .offset = offsetof(Vertex, pos)},
                {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,    .offset = offsetof(Vertex, normal)},
                {.location = 2, .binding = 0, .format = vk::Format::eR32G32Sfloat,       .offset = offsetof(Vertex, uv)},
                {.location = 3, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,    .offset = offsetof(Vertex, color)},
                {.location = 4, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = offsetof(Vertex, tangent)}
            } };
        }
    };

    class VulkanPipeline {

    public:
        void CreateDescriptorSetLayout(VulkanContext& context);
        // Set 1: the per-material bundle of five PBR texture bindings (see the .cpp for the
        // exact binding layout). Independent from set 0 (the per-frame camera UBO) so a material
        // change never touches the camera descriptor and vice versa.
        void CreateMaterialDescriptorSetLayout(VulkanContext& context);
        void CreatePipeline(VulkanContext& context, VulkanSwapchain& swapchain);
        void CleanUp();

        const vk::raii::Pipeline&               GetPipeline() const;
        const vk::raii::PipelineLayout&         GetLayout() const;
        const vk::raii::DescriptorSetLayout&    GetDescriptorSetLayout() const;
        const vk::raii::DescriptorSetLayout&    GetMaterialDescriptorSetLayout() const;

    private:
        std::vector<char> ReadFile(std::string const& filename);
        [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(VulkanContext& context, std::vector<char> const& code);

    private:
        vk::raii::Pipeline              m_graphicsPipeline  = nullptr;
        vk::raii::PipelineLayout        m_pipelineLayout    = nullptr;
        vk::raii::DescriptorSetLayout   m_descriptorSetLayout = nullptr;
        vk::raii::DescriptorSetLayout   m_materialDescriptorSetLayout = nullptr;

    };  // VULKAN PIPELINE CLASS

}   // NAMESPACE CRUCIBLE
