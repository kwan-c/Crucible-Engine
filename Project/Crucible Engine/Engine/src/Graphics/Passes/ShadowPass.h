/*************************************************************************
  \file       ShadowPass.h
  \brief
    The sun shadow pass: draws a view's shadow casters into each cascade of
    the shared shadow map (Shadow.slang).
  \details
    The shadow map itself, and the fitting of cascades to the camera, are
    still the Renderer's (CreateShadowResources, ComputeShadowCascades).
    Owns its pipeline.
*************************************************************************/
#pragma once

#include "Graphics/RenderTypes.h"
#include "Graphics/DrawList.h"

#include <array>

namespace Crucible {

    class VulkanContext;
    class RenderResources;

    class ShadowPass {
    public:
        explicit ShadowPass(const RenderResources& resources) : m_resources(resources) {}

        /*********************************************************************
         \brief  Creates the pass's pipeline. Call once, before recording.
 \details Depth only, position-only vertex input, no culling, slope-scaled
                 depth bias. Layout = set 0 + a 4-byte vertex push constant
                 (the cascade index).
         \param[in] context         The VulkanContext to create it through.
         \param[in] frameSetLayout  Layout of set 0 (the per-frame data).
         \param[in] depthFormat     The shadow map's format.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::Format depthFormat);

        /*********************************************************************
         \brief  Records one view's cascades into the shared shadow map,
                 barriers included. Leaves the map ready to sample.
         \param[in] cmd           Command buffer, with no rendering pass open.
         \param[in] shadowImage   The shadow map (one layer per cascade).
         \param[in] layerViews    One view per layer, to render into.
         \param[in] mapSize       Width and height of the map, in texels.
         \param[in] frameSet      The view's set 0 (cascade matrices, instances).
         \param[in] cascades      Shadow casters, per cascade.
         \param[in] cascadeCount  Cascades in use; 0 records nothing.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, vk::Image shadowImage,
                    const std::array<vk::raii::ImageView, kMaxShadowCascades>& layerViews, uint32_t mapSize,
                    vk::DescriptorSet frameSet,
                    const std::array<std::vector<DrawBatch>, kMaxShadowCascades>& cascades, uint32_t cascadeCount) const;

    private:
        vk::raii::Pipeline       m_shadowPipeline = nullptr;
        vk::raii::PipelineLayout m_shadowLayout = nullptr;
        const RenderResources& m_resources;   // the meshes it draws
    };

}   // NAMESPACE CRUCIBLE
