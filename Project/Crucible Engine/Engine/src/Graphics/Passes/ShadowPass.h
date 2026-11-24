/*************************************************************************
  \file       ShadowPass.h
  \brief
    The sun shadow pass: draws a view's shadow casters into each cascade of
    the shared shadow map (Shadow.slang).
  \details
    Owns the shadow map, its sampler and its pipeline, and fits the
    cascades to a camera. Which light casts, and which objects are casters,
    are the Renderer's.
*************************************************************************/
#pragma once

#include "Graphics/RenderTypes.h"
#include "Graphics/DrawList.h"
#include "Graphics/Vulkan/VulkanResource.h"

#include <array>

namespace Crucible {

    class VulkanContext;
    class RenderResources;
    class Camera;

    class ShadowPass {
    public:
        explicit ShadowPass(const RenderResources& resources) : m_resources(resources) {}

        /*********************************************************************
         \brief  Creates the pass's pipeline. Call once, before recording.
 \details Depth only, position-only vertex input, no culling, slope-scaled
                 depth bias. Layout = set 0 + set 1 (a material) + a push
                 constant (the cascade index, then CutoutPush). A second
                 pipeline draws cut-out materials: it also reads the uv and
                 drops the pixels the material's alpha cuts out.
         \param[in] context         The VulkanContext to create it through.
         \param[in] frameSetLayout     Layout of set 0 (the per-frame data).
         \param[in] materialSetLayout  Layout of a material's set (set 1).
         \param[in] depthFormat        The shadow map's format.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout materialSetLayout, vk::Format depthFormat);

        /*********************************************************************
         \brief  Builds the pass's two pipelines - solid and cut-out - from a
                 module that has Shadow.slang's entry points: Shadow.spv
                 itself, or a mesh shader that moves vertices (MaterialShaders
                 calls this for each, on its background thread, so it only
                 reads state that no longer changes). After Create().
         \param[in] shader         The module.
         \param[in] allAttributes  true for a mesh shader: its vertex stage is
                                   given every vertex attribute, not just the
                                   position (and uv).
         \param[out] solid, cutout The pipelines.
         *********************************************************************/
        void BuildPipelines(VulkanContext& context, vk::ShaderModule shader, bool allAttributes,
                            vk::raii::Pipeline& solid, vk::raii::Pipeline& cutout) const;

        /*********************************************************************
         \brief  Creates the shadow map (one layer per cascade), a view of the
                 whole array to sample and one per layer to draw into, and the
                 comparison sampler. Call once. The image starts in an
                 undefined layout: the caller moves it to shader-read.
         \param[in] context  The VulkanContext to create them through.
         *********************************************************************/
        void CreateResources(VulkanContext& context);

        /*********************************************************************
         \brief  Fits `count` cascades to a camera (stable: bounding spheres
                 and texel snapping) and writes their matrices and split
                 distances into `ubo`.
         \param[in] camera        The view's camera.
         \param[in] lightDir      Direction the sun's light travels.
         \param[in] settings      Distance, split spacing, biases, fade.
         \param[in] count         Cascades to fit, 1..kMaxShadowCascades.
         \param[in,out] ubo       Receives shadowViewProj, cascadeSplits,
                                 cascadeTexelSize and shadowParams.
         \param[out] outViewProj  Each cascade's view-projection, for culling
                                 its casters.
         *********************************************************************/
        void ComputeCascades(const Camera& camera, const glm::vec3& lightDir, const ShadowSettings& settings, uint32_t count,
                             FrameUniforms& ubo, std::array<glm::mat4, kMaxShadowCascades>& outViewProj) const;

        /*********************************************************************
         \brief  Records one view's cascades into the shared shadow map,
                 barriers included. Leaves the map ready to sample.
         \param[in] cmd           Command buffer, with no rendering pass open.
         \param[in] frameSet      The view's set 0 (cascade matrices, instances).
         \param[in] cascades      Shadow casters, per cascade.
         \param[in] cascadeCount  Cascades in use; 0 records nothing.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, vk::DescriptorSet frameSet,
                    const std::array<std::vector<DrawBatch>, kMaxShadowCascades>& cascades, uint32_t cascadeCount) const;

        static constexpr vk::Format kFormat  = vk::Format::eD32Sfloat;
        static constexpr uint32_t   kMapSize = 2048;   ///< Width and height of each cascade, in texels.

        vk::Image     Image() const     { return m_shadowImage.GetImage(); }   ///< The whole map.
        vk::ImageView ArrayView() const { return *m_shadowArrayView; }         ///< Every layer, for sampling.
        vk::Sampler   Sampler() const   { return *m_shadowSampler; }           ///< Compares against the stored depth.

    private:
        vk::raii::Pipeline       m_shadowPipeline = nullptr;
        vk::raii::Pipeline       m_shadowCutoutPipeline = nullptr;   // cut-out materials (leaves)
        vk::raii::PipelineLayout m_shadowLayout = nullptr;
        vk::Format               m_depthFormat = vk::Format::eUndefined;   // the shadow map's (Create)

        // One D32 array image (a layer per cascade) shared by every view: each view renders its
        // own cascades into it right before its main pass. It rests in SHADER_READ_ONLY whenever
        // it isn't being rendered, so it is always valid to sample.
        VulkanImage                                         m_shadowImage;
        vk::raii::ImageView                                 m_shadowArrayView = nullptr;
        std::array<vk::raii::ImageView, kMaxShadowCascades> m_shadowLayerViews{ nullptr, nullptr, nullptr, nullptr };
        vk::raii::Sampler                                   m_shadowSampler = nullptr;
        const RenderResources& m_resources;   // the meshes it draws
    };

}   // NAMESPACE CRUCIBLE
