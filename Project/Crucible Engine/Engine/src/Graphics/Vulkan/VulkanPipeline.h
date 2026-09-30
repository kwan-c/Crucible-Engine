#pragma once
/*************************************************************************
* \file:         VulkanPipeline.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                The engine's one graphics pipeline, its descriptor set
*                layouts, and the shared Vertex layout.
*
* \details
*                Declares Vertex (the vertex layout shared by every mesh
*                source - procedural primitives and .glb imports alike)
*                and VulkanPipeline, which owns the pipeline layout,
*                descriptor set layouts (camera UBO at set 0, material
*                textures at set 1), and the single graphics pipeline
*                itself.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "Math/Math.h"

#include <array>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Crucible {

    /*************************************************************************
     \brief
        Vertex layout shared by every mesh source: procedural primitives
        (Renderer::Primitives), and .glb imports (GltfLoader converts into
        this layout - see AssetPipe/GltfLoader.h).

     \details
        - normal:  object-space normal. Required for lighting; glTF
                   supplies NORMAL directly.
        - uv:      first UV set. Required for textured materials; glTF's
                   TEXCOORD_0.
        - color:   vertex color tint, mainly for procedural/debug meshes.
                   glTF meshes without a COLOR_0 attribute should have the
                   loader fill this with {1,1,1}.
        - tangent: object-space tangent (xyz) + bitangent handedness sign
                   (w, always +-1), for normal mapping. bitangent =
                   cross(normal, tangent.xyz) * tangent.w - this is the
                   same convention glTF's TANGENT attribute uses, so the
                   loader can copy it through unchanged; procedural
                   primitives compute it analytically (see Primitives.h's
                   per-shape derivations).
     *************************************************************************/
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec3 color;
        glm::vec4 tangent;

        /*********************************************************************
         \brief  Vertex input binding description (binding 0, per-vertex).
         \return The binding description.
         *********************************************************************/
        static vk::VertexInputBindingDescription GetBindingDescription() {
            return { .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex };
        }

        /*********************************************************************
         \brief  Vertex input attribute descriptions for pos/normal/uv/
                 color/tangent, in that location order.
         \return The five attribute descriptions.
         *********************************************************************/
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

    /*************************************************************************
     \brief
        Owns the engine's one graphics pipeline: descriptor set layouts,
        pipeline layout, and the pipeline object itself.
     *************************************************************************/
    class VulkanPipeline {

    public:
        /*********************************************************************
         \brief  Creates the set-0 descriptor set layout (the per-frame
                 camera UBO).
         \param[in] context The VulkanContext to create the layout through.
         *********************************************************************/
        void CreateDescriptorSetLayout(VulkanContext& context);

        /*********************************************************************
         \brief
            Creates the set-1 descriptor set layout: the per-material
            bundle of five PBR texture bindings.

         \param[in] context The VulkanContext to create the layout through.

         \details
            Independent from set 0 (the per-frame camera UBO) so a
            material change never touches the camera descriptor and vice
            versa. See the .cpp for the exact binding layout.
         *********************************************************************/
        void CreateMaterialDescriptorSetLayout(VulkanContext& context);

        /*********************************************************************
         \brief  Creates the graphics pipeline itself: shader stages,
                 fixed-function state, pipeline layout, and dynamic
                 rendering attachment formats.
         \param[in] context   The VulkanContext to create the pipeline
                               through.
         \param[in] swapchain Supplies the color/depth attachment formats
                               the pipeline must match.
         *********************************************************************/
        void CreatePipeline(VulkanContext& context, VulkanSwapchain& swapchain);

        /*********************************************************************
         \brief  Currently a no-op (vk::raii members clean themselves up);
                 kept as an explicit hook for symmetry with other Vulkan
                 owner types.
         *********************************************************************/
        void CleanUp();

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the owned pipeline.
         *********************************************************************/
        const vk::raii::Pipeline&               GetPipeline() const;

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the owned pipeline layout.
         *********************************************************************/
        const vk::raii::PipelineLayout&         GetLayout() const;

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the set-0 (camera UBO) descriptor set
                 layout.
         *********************************************************************/
        const vk::raii::DescriptorSetLayout&    GetDescriptorSetLayout() const;

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the set-1 (material textures)
                 descriptor set layout.
         *********************************************************************/
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
