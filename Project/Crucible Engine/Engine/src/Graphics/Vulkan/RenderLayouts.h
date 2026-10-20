#pragma once
/*************************************************************************
* \file:         RenderLayouts.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                The descriptor set layouts the render passes share, the
*                Vertex layout, and the image formats the passes agree on.
*
* \details
*                Was VulkanPipeline. Every pipeline has since moved into the
*                pass that uses it (Graphics/Passes/), so what is left is
*                what more than one pass needs: set 0 (per-frame data), the
*                material set, the one-texture and two-texture sets, the
*                Vertex struct every mesh uses, and the format constants.
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
        glm::uvec4 joints{0u};
        glm::vec4 weights{0.0f};

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
         \return The seven attribute descriptions (including joint indices and weights).
         *********************************************************************/
        static std::array<vk::VertexInputAttributeDescription, 7> GetAttributeDescriptions() {
            return { {
                {.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,    .offset = offsetof(Vertex, pos)},
                {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,    .offset = offsetof(Vertex, normal)},
                {.location = 2, .binding = 0, .format = vk::Format::eR32G32Sfloat,       .offset = offsetof(Vertex, uv)},
                {.location = 3, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,    .offset = offsetof(Vertex, color)},
                {.location = 4, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = offsetof(Vertex, tangent)},
                {.location = 5, .binding = 0, .format = vk::Format::eR32G32B32A32Uint, .offset = offsetof(Vertex, joints)},
                {.location = 6, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = offsetof(Vertex, weights)}
            } };
        }
    };

    /*************************************************************************
     \brief
        Owns the descriptor set layouts that more than one render pass (or
        the Renderer itself) builds on. Each pass owns its own pipelines.
     *************************************************************************/
    class RenderLayouts {

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

        /// Format of the per-viewport selection mask (an attachment of the mask pipeline).
        static constexpr vk::Format kOutlineMaskFormat = vk::Format::eR8Unorm;
        /// Ambient occlusion: its half-size depth image and the one-channel result.
        static constexpr vk::Format kAoDepthFormat = vk::Format::eD32Sfloat;
        static constexpr vk::Format kAoFormat      = vk::Format::eR8Unorm;
        static constexpr vk::Format kAoNormalFormat = vk::Format::eR8G8B8A8Unorm;   ///< View-space normals from the pre-pass.

        /// Format the scene (meshes, sky, particles) is rendered in: floating point, so brightness
        /// above 1 survives until the tonemap pass. The bloom chain uses it too.
        static constexpr vk::Format kSceneColorFormat = vk::Format::eR16G16B16A16Sfloat;
        /// The scene pass's second output, for the toon outline pass (ToonOutlinePass): per
        /// pixel 1 / view depth, the surface's direction (octahedral, 2 channels) and how
        /// strong a line may be there. Floating point so 1/depth keeps its precision.
        static constexpr vk::Format kLineDataFormat = vk::Format::eR16G16B16A16Sfloat;

        /*********************************************************************
         \brief
            Creates the set layout with two sampled textures (bindings 0
            and 1, fragment stage) that the tonemap and ambient occlusion
            passes read their inputs through. See
            GetTonemapDescriptorSetLayout().
         \param[in] context The VulkanContext to create it through.
         *********************************************************************/
        void CreateTwoTextureSetLayout(VulkanContext& context);

        /// The scene pipelines (meshes, sky, particles) exist once per MSAA level: pass the
        /// number of samples per pixel of the target being drawn into (1, 2 or 4).
        static constexpr uint32_t kMsaaLevels = 3;
        static uint32_t MsaaIndex(uint32_t samples) { return samples >= 4 ? 2u : samples >= 2 ? 1u : 0u; }

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
         \return Const reference to the skybox's set-1 (cubemap)
                 descriptor set layout.
         *********************************************************************/
        const vk::raii::DescriptorSetLayout&    GetSkyboxDescriptorSetLayout() const;


        const vk::raii::DescriptorSetLayout&    GetTonemapDescriptorSetLayout() const;///< Two sampled textures (tonemap, ambient occlusion).
        /// One combined image sampler at binding 0 (fragment). The same layout object the skybox's
        /// cubemap set uses; particles and the bloom passes use it for their one texture.
        const vk::raii::DescriptorSetLayout&    GetSingleTextureSetLayout() const { return m_skyboxDescriptorSetLayout; }

    private:
        vk::raii::DescriptorSetLayout   m_descriptorSetLayout = nullptr;
        vk::raii::DescriptorSetLayout   m_materialDescriptorSetLayout = nullptr;

        vk::raii::DescriptorSetLayout   m_skyboxDescriptorSetLayout = nullptr;


        vk::raii::DescriptorSetLayout   m_tonemapDescriptorSetLayout = nullptr;

    };  // RENDER LAYOUTS CLASS

}   // NAMESPACE CRUCIBLE
