/*************************************************************************
  \file       ParticlePass.cpp
  \brief
    See ParticlePass.h.
*************************************************************************/
#include "Graphics/Passes/ParticlePass.h"

#include "Graphics/RenderResources.h"
#include "Graphics/Camera.h"
#include "Graphics/Vulkan/RenderLayouts.h"
#include "Graphics/Vulkan/VulkanCommands.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Spatial/Bounds.h"
#include "Diagnostics/Logger.h"

#include <algorithm>
#include <cstring>

namespace Crucible {

    static_assert(RenderLayouts::kMsaaLevels == 3, "ParticlePass keeps one pipeline per MSAA level");

    void ParticlePass::Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout textureSetLayout,
                              vk::Format colourFormat, vk::Format depthFormat) {


        vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "Particle.spv");
        vk::PipelineShaderStageCreateInfo stages[] = {
            { .stage = vk::ShaderStageFlagBits::eVertex,   .module = shaderModule, .pName = "particleVert" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "particleFrag" }
        };

        // No vertex buffers: six vertices per particle are generated from the vertex index.
        vk::PipelineVertexInputStateCreateInfo   vertexInputInfo{};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable        = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode             = vk::PolygonMode::eFill,
            .cullMode                = vk::CullModeFlagBits::eNone,
            .frontFace               = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable         = vk::False,
            .lineWidth               = 1.0f
        };
        vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };

        // Premultiplied blending: result = src + dst * (1 - src.a). The shader writes alpha 0 for
        // additive particles (pure add) and the real alpha for alpha-blended ones.
        vk::PipelineColorBlendAttachmentState blend{
            .blendEnable         = vk::True,
            .srcColorBlendFactor = vk::BlendFactor::eOne,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .colorBlendOp        = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eZero,
            .dstAlphaBlendFactor = vk::BlendFactor::eOne,
            .alphaBlendOp        = vk::BlendOp::eAdd,
            .colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };
        // Drawn inside the scene pass, which has a second output (the toon outline pass's line
        // data). Same blend state there (independentBlend is not enabled); Particle.slang writes
        // zero with zero alpha to it, which this blend keeps unchanged.
        const vk::PipelineColorBlendAttachmentState blends[2] = { blend, blend };
        vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 2, .pAttachments = blends };

        // Hidden behind meshes, but never hiding anything themselves (no depth write) - otherwise
        // a transparent quad would cut holes in the particles behind it.
        vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable       = vk::True,
            .depthWriteEnable      = vk::False,
            .depthCompareOp        = vk::CompareOp::eLessOrEqual,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable     = vk::False
        };

        vk::PushConstantRange push{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .offset = 0, .size = sizeof(ParticlePush) };
        std::array<vk::DescriptorSetLayout, 2> setLayouts{ frameSetLayout, textureSetLayout };
        vk::PipelineLayoutCreateInfo layoutInfo{
            .setLayoutCount = static_cast<uint32_t>(setLayouts.size()), .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
        m_particleLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);

        const vk::Format colourFormats[2] = { colourFormat, RenderLayouts::kLineDataFormat };
        vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
            {
                .stageCount          = 2,
                .pStages             = stages,
                .pVertexInputState   = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState      = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState   = &multisampling,
                .pDepthStencilState  = &depthStencil,
                .pColorBlendState    = &colorBlending,
                .pDynamicState       = &dynamicState,
                .layout              = m_particleLayout,
                .renderPass          = nullptr
            },
            {.colorAttachmentCount = 2, .pColorAttachmentFormats = colourFormats, .depthAttachmentFormat = depthFormat }
        };
        // One pipeline per MSAA level (1, 2, 4 samples per pixel): the sample count is baked into a
        // pipeline and must match the target it draws into. `multisampling` is referenced by
        // pointer above, so changing it here changes what the next pipeline is built with.
        for (uint32_t i = 0; i < RenderLayouts::kMsaaLevels; ++i) {
            multisampling.rasterizationSamples = static_cast<vk::SampleCountFlagBits>(1u << i);
            m_particlePipelines[i] = vk::raii::Pipeline(context.GetDevice(), context.GetPipelineCache(), chain.get<vk::GraphicsPipelineCreateInfo>());
        }

    }   // ParticlePass::Create()

    // ------------------------------------------------------------------------------------------------------------- //
    //  THIS FRAME'S PARTICLES: SUBMIT, SORT PER VIEW, UPLOAD
    // ------------------------------------------------------------------------------------------------------------- //

    void ParticlePass::InitResources(uint32_t framesInFlight, vk::DescriptorSetLayout textureSetLayout, vk::Sampler clampSampler) {

        m_textureSetLayout = textureSetLayout;
        m_clampSampler     = clampSampler;

        const uint32_t frames = framesInFlight;
        for (uint32_t i = 0; i < frames; ++i) {
            m_particleBuffers.emplace_back();
            m_particleBuffers.back().Create(m_context, vk::DeviceSize{ kInitialParticleCapacity } * sizeof(GpuParticle),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_particleBuffersMapped.push_back(m_particleBuffers.back().Map());
            m_particleCapacity.push_back(kInitialParticleCapacity);
        }

        // One set per texture that particles use (binding 0 = the texture).
        vk::DescriptorPoolSize size{ .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = kMaxParticleTextures };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = kMaxParticleTextures,
            .poolSizeCount = 1, .pPoolSizes = &size };
        m_particleTexturePool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

    }   // ParticlePass::InitResources()

    void ParticlePass::Submit(const ParticleDrawDesc& desc, const GpuParticle* particles, uint32_t count) {

        if (count == 0 || particles == nullptr) { return; }
        const uint32_t first = static_cast<uint32_t>(m_particleQueue.size());
        m_particleQueue.insert(m_particleQueue.end(), particles, particles + count);
        m_particleSubmits.push_back(ParticleSubmit{ desc, first, count });

    }   // ParticlePass::Submit()

    void ParticlePass::Prepare(const Camera& camera, std::vector<ParticleBatch>& out, RenderCullStats& stats, bool hideSceneHidden) {

        if (m_particleSubmits.empty()) { return; }

        const Frustum   frustum = Frustum::FromViewProjection(camera.GetProjection() * camera.GetView());
        const glm::vec3 eye     = camera.position;

        // Visible batches, far to near: blending is only right when what is behind is drawn first.
        m_particleOrder.clear();
        for (uint32_t i = 0; i < m_particleSubmits.size(); ++i) {
            const ParticleDrawDesc& desc = m_particleSubmits[i].desc;
            if (hideSceneHidden && desc.sceneHidden) { continue; }   // hidden in the editor's Scene view
            if (!frustum.Intersects(AABB(desc.boundsMin, desc.boundsMax))) { continue; }
            const glm::vec3 d = (desc.boundsMin + desc.boundsMax) * 0.5f - eye;
            m_particleOrder.emplace_back(glm::dot(d, d), i);
        }
        std::sort(m_particleOrder.begin(), m_particleOrder.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

        for (const auto& entry : m_particleOrder) {

            const ParticleSubmit& submit = m_particleSubmits[entry.second];
            if (m_particleScratch.size() + submit.count > kMaxParticlesPerFrame) {
                if (!m_warnedParticleOverflow) {
                    CRU_WARN("[Renderer] More than {} particles drawn in one frame; the rest are dropped.", kMaxParticlesPerFrame);
                    m_warnedParticleOverflow = true;
                }
                break;
            }

            const uint32_t first = static_cast<uint32_t>(m_particleScratch.size());
            m_particleScratch.insert(m_particleScratch.end(),
                m_particleQueue.begin() + submit.first, m_particleQueue.begin() + submit.first + submit.count);

            // Additive particles sum to the same result in any order. Alpha-blended ones don't, so
            // this view gets its own copy sorted back to front.
            if (!submit.desc.additive && submit.count > 1) {
                std::sort(m_particleScratch.begin() + first, m_particleScratch.end(), [&](const GpuParticle& a, const GpuParticle& b) {
                    const glm::vec3 da = glm::vec3(a.posSize) - eye, db = glm::vec3(b.posSize) - eye;
                    return glm::dot(da, da) > glm::dot(db, db);
                });
            }

            const bool textured = m_resources.IsTextureUsable(submit.desc.texture);
            ParticleBatch batch;
            batch.texture = textured ? submit.desc.texture : m_resources.DefaultWhiteTexture();
            batch.first   = first;
            batch.count   = submit.count;
            batch.push.params = glm::vec4(
                static_cast<float>(std::max(submit.desc.flipbookCols, 1u)), static_cast<float>(std::max(submit.desc.flipbookRows, 1u)),
                submit.desc.additive ? 1.0f : 0.0f, textured ? 1.0f : 0.0f);
            EnsureTextureSet(batch.texture);   // made here, so Record() only has to look it up
            out.push_back(batch);

            stats.particles += submit.count;
            ++stats.particleDrawCalls;
        }

    }   // ParticlePass::Prepare()

    bool ParticlePass::Upload(uint32_t frameIndex) {

        bool grew = false;
        const size_t count = m_particleScratch.size();

        // Same growth rule and safety argument as UploadInstances().
        if (count > m_particleCapacity[frameIndex]) {
            uint32_t capacity = m_particleCapacity[frameIndex];
            while (capacity < count) { capacity *= 2; }
            m_particleBuffers[frameIndex].Create(m_context, vk::DeviceSize{ capacity } * sizeof(GpuParticle),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_particleBuffersMapped[frameIndex] = m_particleBuffers[frameIndex].Map();
            m_particleCapacity[frameIndex]      = capacity;

            grew = true;
        }

        if (count > 0) { memcpy(m_particleBuffersMapped[frameIndex], m_particleScratch.data(), count * sizeof(GpuParticle)); }
        return grew;

    }   // ParticlePass::Upload()

    vk::DescriptorSet ParticlePass::EnsureTextureSet(TextureHandle texture) {

        if (!m_resources.IsTextureUsable(texture)) { texture = m_resources.DefaultWhiteTexture(); }   // also destroyed ones

        if (auto it = m_particleTextureSets.find(texture.id); it != m_particleTextureSets.end()) { return *it->second; }

        // Pool full (more than kMaxParticleTextures different particle textures): fall back to the
        // white texture's set, which is created first and so always exists.
        if (m_particleTextureSets.size() >= kMaxParticleTextures) {
            return *m_particleTextureSets.at(m_resources.DefaultWhiteTexture().id);
        }

        vk::DescriptorSetLayout layout = m_textureSetLayout;
        vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = m_particleTexturePool, .descriptorSetCount = 1, .pSetLayouts = &layout };
        vk::raii::DescriptorSet set = std::move(m_context.GetDevice().allocateDescriptorSets(allocInfo).front());

        // Clamped sampling (the viewport sampler), so a flipbook frame never bleeds in from the
        // opposite edge of the atlas.
        vk::DescriptorImageInfo imageInfo{
            .sampler = m_clampSampler, .imageView = *m_resources.Textures()[texture.id].view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::WriteDescriptorSet write{
            .dstSet = *set, .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageInfo };
        m_context.GetDevice().updateDescriptorSets(write, {});

        return *m_particleTextureSets.emplace(texture.id, std::move(set)).first->second;

    }   // ParticlePass::EnsureTextureSet()

    // ------------------------------------------------------------------------------------------------------------- //
    //  DRAWING
    // ------------------------------------------------------------------------------------------------------------- //

    void ParticlePass::Record(vk::CommandBuffer cmd, uint32_t samples, vk::DescriptorSet frameSet, const std::vector<ParticleBatch>& batches) const {

        if (batches.empty()) { return; }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_particlePipelines[RenderLayouts::MsaaIndex(samples)]);
        // Set 0 again: this layout's push-constant range differs from the previous pipeline's, so
        // the earlier binding doesn't carry over.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_particleLayout, 0, frameSet, nullptr);

        TextureHandle bound;
        for (const ParticleBatch& batch : batches) {
            if (!(batch.texture == bound)) {
                // Prepare() made this texture's set; the white texture's is the fallback when the pool is full.
                auto it = m_particleTextureSets.find(batch.texture.id);
                if (it == m_particleTextureSets.end()) { it = m_particleTextureSets.find(m_resources.DefaultWhiteTexture().id); }
                if (it == m_particleTextureSets.end()) { continue; }
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_particleLayout, 1, *it->second, nullptr);
                bound = batch.texture;
            }
            cmd.pushConstants<ParticlePush>(*m_particleLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, batch.push);
            // Six vertices per particle, no vertex buffer; the vertex index (which includes
            // firstVertex) tells the shader which particle and which corner.
            cmd.draw(batch.count * 6, 1, batch.first * 6, 0);
        }

    }   // ParticlePass::Record()

}   // NAMESPACE CRUCIBLE
