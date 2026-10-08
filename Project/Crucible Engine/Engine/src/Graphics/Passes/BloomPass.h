/*************************************************************************
  \file       BloomPass.h
  \brief
    The bloom pass: the glow around very bright things. Shrinks the bright
    parts of a view's HDR image down a chain of smaller images, then adds
    them back up the chain (Post.slang's bloomDownFrag / bloomUpFrag).
  \details
    Reads PostTargets::hdrSet, writes PostTargets::bloomImages; the result
    the tonemap pass reads is bloomImages[0]. Owns its pipelines.
*************************************************************************/
#pragma once

#include "Graphics/ViewTargets.h"
#include "Graphics/RenderTypes.h"

namespace Crucible {

    class VulkanContext;

    class BloomPass {
    public:
        /*********************************************************************
         \brief  Creates the pass's pipelines and their layouts. Call once,
                 before recording.
         \param[in] context           The VulkanContext to create them through.
         \param[in] textureSetLayout  Layout of a set with one sampled texture.
         \param[in] colourFormat      Format of the HDR images in the bloom chain.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout textureSetLayout, vk::Format colourFormat);

        /*********************************************************************
         \brief  Records the whole bloom chain for one view.
         \param[in] cmd             Command buffer, with no rendering pass open.
         \param[in,out] targets     The view's targets; hdrImage must be readable.
         \param[in] settings        Bloom on/off, threshold, knee.
         \param[in] exposureSetting The scene's exposure (the threshold is
                                    judged on exposed brightness).
         \note  With bloom off it still clears bloomImages[0] to black, because
                the tonemap pass always samples it.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, PostTargets& targets, const PostSettings& settings, float exposureSetting) const;

    private:
        vk::raii::Pipeline       m_bloomDownPipeline = nullptr;   // half-size blur step
        vk::raii::Pipeline       m_bloomUpPipeline = nullptr;     // additive upsample step
        vk::raii::PipelineLayout m_bloomLayout = nullptr;         // one texture + PostPush
    };

}   // NAMESPACE CRUCIBLE
