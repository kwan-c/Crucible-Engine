/*************************************************************************
  \file       ParticlePass.h
  \brief
    Draws a view's particles as camera-facing squares (Particle.slang),
    inside the scene pass, after the meshes and the sky.
  \details
    Sorting, the particle buffer and the per-texture descriptor sets are
    still the Renderer's. Owns its pipelines.
*************************************************************************/
#pragma once

#include "Graphics/RenderTypes.h"
#include "Graphics/DrawList.h"

#include <array>

namespace Crucible {

    class VulkanContext;

    class ParticlePass {
    public:
        // Returns the descriptor set that binds a texture for the particle pipeline. Supplied by
        // the Renderer, which owns those sets. Temporary: goes away when textures move into RenderResources.
        using TextureSetFn = std::function<vk::DescriptorSet(TextureHandle)>;

        explicit ParticlePass(TextureSetFn textureSet) : m_textureSet(std::move(textureSet)) {}

        /*********************************************************************
         \brief  Creates the pass's pipelines. Call once, before recording.
 \details One per MSAA level. Depth-tested but not depth-writing,
                 blending (ONE, ONE_MINUS_SRC_ALPHA) so one pipeline does both
                 additive and alpha-blended particles. Layout = set 0 + one
                 texture + a 16-byte push constant (ParticlePush).
         \param[in] context           The VulkanContext to create them through.
         \param[in] frameSetLayout    Layout of set 0 (the per-frame data).
         \param[in] textureSetLayout  Layout of set 1 (one sampled texture).
         \param[in] colourFormat      Format of the HDR scene image.
         \param[in] depthFormat       Format of the scene's depth image.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout textureSetLayout,
                    vk::Format colourFormat, vk::Format depthFormat);

        /*********************************************************************
         \brief  Draws the batches into the rendering pass that is already open.
         \param[in] cmd       Command buffer, inside the scene pass.
         \param[in] samples   MSAA samples of the target being drawn into.
         \param[in] frameSet  The view's set 0.
         \param[in] batches   Far to near.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, uint32_t samples, vk::DescriptorSet frameSet, const std::vector<ParticleBatch>& batches) const;

    private:
        std::array<vk::raii::Pipeline, 3> m_particlePipelines{ nullptr, nullptr, nullptr };   // 1, 2, 4 samples
        vk::raii::PipelineLayout          m_particleLayout = nullptr;
        TextureSetFn          m_textureSet;
    };

}   // NAMESPACE CRUCIBLE
