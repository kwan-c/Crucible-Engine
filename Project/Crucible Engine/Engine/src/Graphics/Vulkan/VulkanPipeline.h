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
         \brief  Creates the set-0 descriptor set layout: binding 0 = the
                 per-view FrameUniforms UBO, binding 1 = the per-frame
                 light storage buffer, binding 2 = the per-frame instance
                 buffer (InstanceData), binding 3 = the shadow-map array
                 (sampled image), binding 4 = its comparison sampler.
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
         \brief
            Creates the skybox pipeline's set-1 descriptor set layout: a
            single cubemap combined-image-sampler.

         \param[in] context The VulkanContext to create the layout through.
         *********************************************************************/
        void CreateSkyboxDescriptorSetLayout(VulkanContext& context);

        /*********************************************************************
         \brief
            Creates the skybox pipeline (Assets/Shaders/Skybox.spv): no
            vertex input, no culling, depth test LESS_OR_EQUAL with depth
            writes off, drawn after the opaque meshes.

         \param[in] context   The VulkanContext to create the pipeline
                               through.
         \param[in] swapchain Supplies the color/depth attachment formats
                               the pipeline must match.

         \note
            Requires CreateDescriptorSetLayout() and
            CreateSkyboxDescriptorSetLayout() to have run first (its
            layout is set 0 = the per-frame set, set 1 = the cubemap).
         *********************************************************************/
        void CreateSkyboxPipeline(VulkanContext& context, VulkanSwapchain& swapchain);

        /// Format of the per-viewport selection mask (an attachment of the mask pipeline).
        static constexpr vk::Format kOutlineMaskFormat = vk::Format::eR8Unorm;
        /// Ambient occlusion: its half-size depth image and the one-channel result.
        static constexpr vk::Format kAoDepthFormat = vk::Format::eD32Sfloat;
        static constexpr vk::Format kAoFormat      = vk::Format::eR8Unorm;
        static constexpr vk::Format kAoNormalFormat = vk::Format::eR8G8B8A8Unorm;   ///< View-space normals from the pre-pass.

        /// Format the scene (meshes, sky, particles) is rendered in: floating point, so brightness
        /// above 1 survives until the tonemap pass. The bloom chain uses it too.
        static constexpr vk::Format kSceneColorFormat = vk::Format::eR16G16B16A16Sfloat;

        /*********************************************************************
         \brief
            Creates the set layout with two sampled textures (bindings 0
            and 1, fragment stage) that the tonemap and ambient occlusion
            passes read their inputs through. See
            GetTonemapDescriptorSetLayout().
         \param[in] context The VulkanContext to create it through.
         *********************************************************************/
        void CreateTwoTextureSetLayout(VulkanContext& context);

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
        /// The scene pipelines (meshes, sky, particles) exist once per MSAA level: pass the
        /// number of samples per pixel of the target being drawn into (1, 2 or 4).
        static constexpr uint32_t kMsaaLevels = 3;
        static uint32_t MsaaIndex(uint32_t samples) { return samples >= 4 ? 2u : samples >= 2 ? 1u : 0u; }

        const vk::raii::Pipeline&               GetPipeline(uint32_t samples = 1) const;

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

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the skybox pipeline.
         *********************************************************************/
        const vk::raii::Pipeline&               GetSkyboxPipeline(uint32_t samples = 1) const;

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the skybox pipeline layout.
         *********************************************************************/
        const vk::raii::PipelineLayout&         GetSkyboxLayout() const;

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the skybox's set-1 (cubemap)
                 descriptor set layout.
         *********************************************************************/
        const vk::raii::DescriptorSetLayout&    GetSkyboxDescriptorSetLayout() const;


        const vk::raii::DescriptorSetLayout&    GetTonemapDescriptorSetLayout() const;///< Two sampled textures (tonemap, ambient occlusion).
        /// One combined image sampler at binding 0 (fragment). The same layout object the skybox's
        /// cubemap set uses; particles and the bloom passes use it for their one texture.
        const vk::raii::DescriptorSetLayout&    GetSingleTextureSetLayout() const { return m_skyboxDescriptorSetLayout; }

    private:
        std::vector<char> ReadFile(std::string const& filename);
        [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(VulkanContext& context, std::vector<char> const& code);

    private:
        std::array<vk::raii::Pipeline, kMsaaLevels> m_graphicsPipelines{ nullptr, nullptr, nullptr };   // 1, 2, 4 samples
        vk::raii::PipelineLayout        m_pipelineLayout    = nullptr;
        vk::raii::DescriptorSetLayout   m_descriptorSetLayout = nullptr;
        vk::raii::DescriptorSetLayout   m_materialDescriptorSetLayout = nullptr;

        std::array<vk::raii::Pipeline, kMsaaLevels> m_skyboxPipelines{ nullptr, nullptr, nullptr };
        vk::raii::PipelineLayout        m_skyboxPipelineLayout = nullptr;
        vk::raii::DescriptorSetLayout   m_skyboxDescriptorSetLayout = nullptr;


        vk::raii::DescriptorSetLayout   m_tonemapDescriptorSetLayout = nullptr;

    };  // VULKAN PIPELINE CLASS

}   // NAMESPACE CRUCIBLE
