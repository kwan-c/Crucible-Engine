/*************************************************************************
  \file       TonemapPass.h
  \brief
    The tonemap pass: one fullscreen triangle that turns a view's HDR image
    plus its bloom into the final picture - exposure, the ACES curve, FXAA
    and the scene's look (Post.slang's tonemapFrag).
  \details
    Draws inside a rendering pass the caller has already begun on the final
    (sRGB) image, so the caller can draw debug lines straight after it. Owns its pipelines.
*************************************************************************/
#pragma once

#include "Graphics/ViewTargets.h"
#include "Graphics/RenderTypes.h"

namespace Crucible {

    class VulkanContext;

    class TonemapPass {
    public:
        /*********************************************************************
         \brief  Creates the pass's pipelines and their layouts. Call once,
                 before recording.
         \param[in] context              The VulkanContext to create them through.
         \param[in] twoTextureSetLayout  Layout of a set with two sampled textures
                                        (the HDR scene and the bloom).
         \param[in] colourFormat         Format of the final image drawn into.
         \param[in] depthFormat          Format of the depth image that stays bound
                                        for the debug lines that follow.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout twoTextureSetLayout, vk::Format colourFormat, vk::Format depthFormat);

        /*********************************************************************
         \brief  Records the fullscreen tonemap draw.
         \param[in] cmd       Command buffer, INSIDE a rendering pass whose colour
                              attachment is the final image.
         \param[in] targets   The view's targets (HDR image and finished bloom).
         \param[in] extent    Size of the final image.
         \param[in] settings  Bloom strength, FXAA on/off.
         \param[in] env       Exposure and the scene's look.
         \param[in] time      Seconds since start (animates the film grain).
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, const PostTargets& targets, vk::Extent2D extent,
                    const PostSettings& settings, const EnvironmentSettings& env, float time) const;

    private:
        vk::raii::Pipeline       m_tonemapPipeline = nullptr;
        vk::raii::PipelineLayout m_tonemapLayout = nullptr;   // two textures + PostPush
    };

}   // NAMESPACE CRUCIBLE
