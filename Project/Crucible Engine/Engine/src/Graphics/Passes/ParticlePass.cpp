/*************************************************************************
  \file       ParticlePass.cpp
  \brief
    See ParticlePass.h.
*************************************************************************/
#include "Graphics/Passes/ParticlePass.h"

#include "Graphics/Vulkan/VulkanPipeline.h"
#include "Graphics/Vulkan/VulkanCommands.h"
#include "Graphics/Vulkan/VulkanShader.h"

namespace Crucible {

    static_assert(VulkanPipeline::kMsaaLevels == 3, "ParticlePass keeps one pipeline per MSAA level");

    void ParticlePass::Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout textureSetLayout,
                              vk::Format colourFormat, vk::Format depthFormat) {


        vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "../../../Assets/Shaders/Particle.spv");
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
        vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blend };

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

        const vk::Format colorFormat = colourFormat;
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
            {.colorAttachmentCount = 1, .pColorAttachmentFormats = &colorFormat, .depthAttachmentFormat = depthFormat }
        };
        // One pipeline per MSAA level (1, 2, 4 samples per pixel): the sample count is baked into a
        // pipeline and must match the target it draws into. `multisampling` is referenced by
        // pointer above, so changing it here changes what the next pipeline is built with.
        for (uint32_t i = 0; i < VulkanPipeline::kMsaaLevels; ++i) {
            multisampling.rasterizationSamples = static_cast<vk::SampleCountFlagBits>(1u << i);
            m_particlePipelines[i] = vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
        }

    }   // ParticlePass::Create()

    void ParticlePass::Record(vk::CommandBuffer cmd, uint32_t samples, vk::DescriptorSet frameSet, const std::vector<ParticleBatch>& batches) const {

        if (batches.empty()) { return; }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_particlePipelines[VulkanPipeline::MsaaIndex(samples)]);
        // Set 0 again: this layout's push-constant range differs from the previous pipeline's, so
        // the earlier binding doesn't carry over.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_particleLayout, 0, frameSet, nullptr);

        TextureHandle bound;
        for (const ParticleBatch& batch : batches) {
            if (!(batch.texture == bound)) {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_particleLayout, 1, m_textureSet(batch.texture), nullptr);
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
