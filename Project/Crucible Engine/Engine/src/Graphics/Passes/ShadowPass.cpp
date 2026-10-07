/*************************************************************************
  \file       ShadowPass.cpp
  \brief
    See ShadowPass.h.
*************************************************************************/
#include "Graphics/Passes/ShadowPass.h"

#include "Graphics/RenderResources.h"
#include "Graphics/Vulkan/VulkanPipeline.h"
#include "Graphics/Vulkan/VulkanCommands.h"
#include "Graphics/Vulkan/VulkanShader.h"

namespace Crucible {

    void ShadowPass::Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::Format depthFormat) {


        vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "../../../Assets/Shaders/Shadow.spv");

        // Vertex stage only: with no fragment shader the rasterizer still writes depth, which is
        // all a shadow map needs.
        vk::PipelineShaderStageCreateInfo stage{ .stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "shadowVert" };

        // Same vertex buffers as the main pass, but only location 0 (position) is read.
        auto binding    = Vertex::GetBindingDescription();
        auto attributes = Vertex::GetAttributeDescriptions();
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount   = 1,
            .pVertexBindingDescriptions      = &binding,
            .vertexAttributeDescriptionCount = 1,
            .pVertexAttributeDescriptions    = &attributes[0]
        };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

        // No culling: open meshes (planes, cards) must still cast. Slope-scaled bias pushes
        // steep surfaces back so they don't self-shadow; the shader adds a normal offset on top.
        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable        = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode             = vk::PolygonMode::eFill,
            .cullMode                = vk::CullModeFlagBits::eNone,
            .frontFace               = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable         = vk::True,
            .depthBiasConstantFactor = 1.0f,
            .depthBiasClamp          = 0.0f,
            .depthBiasSlopeFactor    = 1.75f,
            .lineWidth               = 1.0f
        };
        vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };
        vk::PipelineColorBlendStateCreateInfo  colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 0 };
        vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable       = vk::True,
            .depthWriteEnable      = vk::True,
            .depthCompareOp        = vk::CompareOp::eLessOrEqual,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable     = vk::False
        };

        vk::PushConstantRange push{ .stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = sizeof(uint32_t) };
        vk::DescriptorSetLayout setLayout = frameSetLayout;
        vk::PipelineLayoutCreateInfo layoutInfo{
            .setLayoutCount = 1, .pSetLayouts = &setLayout,
            .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
        m_shadowLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);

        vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
            {
                .stageCount          = 1,
                .pStages             = &stage,
                .pVertexInputState   = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState      = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState   = &multisampling,
                .pDepthStencilState  = &depthStencil,
                .pColorBlendState    = &colorBlending,
                .pDynamicState       = &dynamicState,
                .layout              = m_shadowLayout,
                .renderPass          = nullptr
            },
            {.colorAttachmentCount = 0, .depthAttachmentFormat = depthFormat }
        };
        m_shadowPipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());

    }   // ShadowPass::Create()

    void ShadowPass::Record(vk::CommandBuffer cmd, vk::Image shadowImage,
                            const std::array<vk::raii::ImageView, kMaxShadowCascades>& layerViews, uint32_t mapSize,
                            vk::DescriptorSet frameSet,
                            const std::array<std::vector<DrawBatch>, kMaxShadowCascades>& cascades, uint32_t cascadeCount) const {

        if (cascadeCount == 0) { return; }

        // Shared by every view: wait for whatever sampled it last (an earlier view this frame, or a
        // previous frame - barriers cover all earlier submissions on the queue too). Its old
        // contents are discarded (UNDEFINED) since every layer is cleared below.
        TransitionImageLayout(cmd, shadowImage, vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
            {}, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            kMaxShadowCascades);

        const vk::Extent2D extent{ mapSize, mapSize };
        for (uint32_t c = 0; c < cascadeCount; ++c) {

            vk::RenderingAttachmentInfo depth{
                .imageView   = *layerViews[c],
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp      = vk::AttachmentLoadOp::eClear,
                .storeOp     = vk::AttachmentStoreOp::eStore,
                .clearValue  = vk::ClearDepthStencilValue(1.0f, 0)
            };
            vk::RenderingInfo renderingInfo{
                .renderArea       = { .offset = { 0, 0 }, .extent = extent },
                .layerCount       = 1,
                .pDepthAttachment = &depth
            };
            cmd.beginRendering(renderingInfo);

            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_shadowPipeline);
            // Same flipped viewport as every other pass, so the shader's uv mapping is uniform.
            cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(mapSize), static_cast<float>(mapSize),
                                            -static_cast<float>(mapSize), 0.0f, 1.0f));
            cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_shadowLayout, 0, frameSet, nullptr);
            cmd.pushConstants<uint32_t>(*m_shadowLayout, vk::ShaderStageFlagBits::eVertex, 0, c);
            m_resources.DrawBatches(cmd, cascades[c]);

            cmd.endRendering();
        }

        TransitionImageLayout(cmd, shadowImage, vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eLateFragmentTests, vk::PipelineStageFlagBits2::eFragmentShader,
            kMaxShadowCascades);

    }   // ShadowPass::Record()

}   // NAMESPACE CRUCIBLE
