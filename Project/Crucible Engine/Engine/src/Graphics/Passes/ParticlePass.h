/*************************************************************************
  \file       ParticlePass.h
  \brief
    Draws a view's particles as camera-facing squares (Particle.slang),
    inside the scene pass, after the meshes and the sky.
  \details
    Also holds the frame's submitted particles, sorts them for each view,
    uploads them, and owns the per-texture descriptor sets and pipelines.
*************************************************************************/
#pragma once

#include "Graphics/RenderTypes.h"
#include "Graphics/DrawList.h"

#include "Graphics/Vulkan/VulkanResource.h"

#include <array>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Crucible {

    class VulkanContext;
    class RenderResources;
    class Camera;

    class ParticlePass {
    public:
        /*********************************************************************
         \param[in] context    Used to create buffers and sets. Must outlive this object.
         \param[in] resources  The textures particles are drawn with.
         *********************************************************************/
        ParticlePass(VulkanContext& context, const RenderResources& resources) : m_context(context), m_resources(resources) {}

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
         \brief  Creates one particle buffer per frame in flight and the pool
                 the per-texture descriptor sets come from. Call once.
         \param[in] framesInFlight    How many frames can be in progress at once.
         \param[in] textureSetLayout  Layout of set 1 (one sampled texture).
         \param[in] clampSampler      Sampler for particle textures: clamped, so
                                     a flipbook frame never bleeds in from the
                                     opposite edge of the atlas.
         *********************************************************************/
        void InitResources(uint32_t framesInFlight, vk::DescriptorSetLayout textureSetLayout, vk::Sampler clampSampler);

        /// Queues one batch of particles for this frame (see Renderer::SubmitParticles).
        void Submit(const ParticleDrawDesc& desc, const GpuParticle* particles, uint32_t count);
        /// Forgets everything submitted. Call once the frame has been drawn.
        void ClearSubmitted() { m_particleQueue.clear(); m_particleSubmits.clear(); }
        /// Empties the per-frame list Prepare() fills. Call before the frame's first view.
        void ClearPrepared() { m_particleScratch.clear(); }

        /*********************************************************************
         \brief  Culls this frame's submitted batches against one view, orders
                 them far to near (and sorts alpha-blended particles back to
                 front), and appends them to the list Upload() sends.
         \param[in] camera  The view's camera.
         \param[out] out    Receives the view's batches, far to near.
         \param[in,out] stats  particles and particleDrawCalls are added to.
         \param[in] hideSceneHidden  true (an editor Scene view): batches with
                            ParticleDrawDesc::sceneHidden are left out.
         *********************************************************************/
        void Prepare(const Camera& camera, std::vector<ParticleBatch>& out, RenderCullStats& stats, bool hideSceneHidden);

        /*********************************************************************
         \brief  Copies everything Prepare() gathered into this frame's buffer.
         \param[in] frameIndex  The frame in flight being built.
         \return true if the buffer had to be replaced by a bigger one, in
                 which case every descriptor set bound to it (set 0, binding 5)
                 must be pointed at Buffer(frameIndex) again.
         *********************************************************************/
        bool Upload(uint32_t frameIndex);

        /// The particle buffer of one frame in flight (set 0, binding 5), and how many there are.
        vk::Buffer Buffer(uint32_t frameIndex) const { return *m_particleBuffers[frameIndex].GetBuffer(); }
        size_t     BufferCount() const               { return m_particleBuffers.size(); }

        /// The descriptor set binding `texture` for the particle pipeline, created on first use.
        /// Called once at startup for the white texture, whose set is the fallback.
        vk::DescriptorSet EnsureTextureSet(TextureHandle texture);

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

        VulkanContext&         m_context;
        const RenderResources& m_resources;
        vk::DescriptorSetLayout m_textureSetLayout;   // set by InitResources()
        vk::Sampler             m_clampSampler;

        // This frame's submitted batches, the per-view sorted copy that is uploaded (grows like
        // the instance buffer), and one small descriptor set per texture used by particles.
        // Pool BEFORE the sets it allocates.
        struct ParticleSubmit { ParticleDrawDesc desc; uint32_t first = 0; uint32_t count = 0; };
        std::vector<GpuParticle>                    m_particleQueue;
        std::vector<ParticleSubmit>                 m_particleSubmits;
        std::vector<GpuParticle>                    m_particleScratch;
        std::vector<std::pair<float, uint32_t>>     m_particleOrder;        // scratch: (distance, submit index)
        std::vector<VulkanBuffer>                   m_particleBuffers;
        std::vector<void*>                          m_particleBuffersMapped;
        std::vector<uint32_t>                       m_particleCapacity;     // in particles
        static constexpr uint32_t                   kInitialParticleCapacity = 16384;
        static constexpr uint32_t                   kMaxParticlesPerFrame = 1u << 20;
        static constexpr uint32_t                   kMaxParticleTextures = 128;
        bool                                        m_warnedParticleOverflow = false;
        vk::raii::DescriptorPool                    m_particleTexturePool = nullptr;
        std::unordered_map<uint32_t, vk::raii::DescriptorSet> m_particleTextureSets;   // TextureHandle::id -> set
    };

}   // NAMESPACE CRUCIBLE
