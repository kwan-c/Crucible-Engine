/*************************************************************************
  \file       AmbientOcclusionPass.h
  \brief
    The ambient occlusion pass: darkens creases and contact points. Draws
    the view's depth and smooth normals at half size (DepthNormal.slang),
    works out how enclosed each pixel is (Post.slang's aoFrag), then smooths
    the result (aoBlurFrag).
  \details
    Writes PostTargets::aoDepth, aoNormal, aoRaw and aoBlur; the result the
    scene shader reads is aoBlur. Owns its pipelines.
*************************************************************************/
#pragma once

#include "Graphics/ViewTargets.h"
#include "Graphics/RenderTypes.h"
#include "Graphics/DrawList.h"

namespace Crucible {

    class VulkanContext;
    class RenderResources;

    class AmbientOcclusionPass {
    public:
        explicit AmbientOcclusionPass(const RenderResources& resources) : m_resources(resources) {}

        /*********************************************************************
         \brief  Creates the pass's pipelines and their layouts. Call once,
                 before recording.
         \param[in] context              The VulkanContext to create them through.
         \param[in] frameSetLayout       Layout of set 0 (the per-frame data).
         \param[in] materialSetLayout    Layout of a material's set (set 1 of
                                         the pre-pass, for cut-out materials).
         \param[in] twoTextureSetLayout  Layout of a set with two sampled textures.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout materialSetLayout,
                    vk::DescriptorSetLayout twoTextureSetLayout);

        /*********************************************************************
         \brief  Records ambient occlusion for one view.
         \param[in] cmd          Command buffer, with no rendering pass open.
         \param[in,out] targets  The view's targets.
         \param[in] settings     On/off, radius, intensity.
         \param[in] frameSet     The view's set 0 (camera and instance data).
         \param[in] meshes       Every mesh the main pass will draw.
         \param[in] projInfo     proj[0][0], [1][1], [2][2], [3][2] of the view's
                                projection, to rebuild positions from depth.
         \note  With occlusion off it still clears aoBlur to white, because the
                scene shader always samples it.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, PostTargets& targets, const PostSettings& settings, vk::DescriptorSet frameSet,
                    const std::vector<DrawBatch>& meshes, const glm::vec4& projInfo) const;

    private:
        vk::raii::Pipeline       m_depthPrepassPipeline = nullptr;   // meshes -> depth + normals
        vk::raii::Pipeline       m_cutoutPrepassPipeline = nullptr;  // the same for cut-out materials
        vk::raii::PipelineLayout m_prepassLayout = nullptr;          // set 0, set 1 (material), CutoutPush
        vk::raii::Pipeline       m_aoPipeline = nullptr;             // depth + normals -> occlusion
        vk::raii::Pipeline       m_aoBlurPipeline = nullptr;         // occlusion + depth -> smoothed
        vk::raii::PipelineLayout m_aoLayout = nullptr;               // two textures + PostPush
        const RenderResources& m_resources;   // the meshes it draws
    };

}   // NAMESPACE CRUCIBLE
