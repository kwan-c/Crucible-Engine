/*************************************************************************
  \file       FullscreenPipeline.h
  \brief
    Builds a pipeline that draws one triangle covering the whole target and
    runs a fragment shader on every pixel: the shape of every
    post-processing step (bloom, tonemap, ambient occlusion). Shared by the
    passes that own such pipelines.
*************************************************************************/
#pragma once

#include "Graphics/Vulkan/VulkanContext.h"

#include <vector>

namespace Crucible {

    /*********************************************************************
     \brief  Creates one fullscreen-triangle pipeline: no vertex input, no
             culling, no depth test, dynamic viewport and scissor.
     \param[in] context       The VulkanContext to create it through.
     \param[in] shaderModule  Post.spv (its vertex entry point is postVert).
     \param[in] fragEntry     Name of the fragment entry point to run.
     \param[in] additive      True to add the result onto the target
                             (ONE, ONE); false to replace it.
     \param[in] layout        The pipeline layout (its sets and push constant).
     \param[in] colorFormat   Format of the image drawn into.
     \param[in] depthFormat   Format of a depth image that is bound but not
                             used, or eUndefined when there is none.
     *********************************************************************/
    [[nodiscard]] inline vk::raii::Pipeline CreateFullscreenPipeline(VulkanContext& context, const vk::raii::ShaderModule& shaderModule,
                                                                     const char* fragEntry, bool additive, vk::PipelineLayout layout,
                                                                     vk::Format colorFormat, vk::Format depthFormat) {

    // Shared by every fullscreen pipeline: a fullscreen triangle, nothing culled, no depth test.
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
    vk::PipelineMultisampleStateCreateInfo  multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };
    vk::PipelineDepthStencilStateCreateInfo noDepth{ .depthTestEnable = vk::False, .depthWriteEnable = vk::False };

    const vk::ColorComponentFlags rgba = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineShaderStageCreateInfo stages[] = {
        { .stage = vk::ShaderStageFlagBits::eVertex,   .module = *shaderModule, .pName = "postVert" },
        { .stage = vk::ShaderStageFlagBits::eFragment, .module = *shaderModule, .pName = fragEntry }
    };
    vk::PipelineColorBlendAttachmentState blend{
        .blendEnable         = additive ? vk::True : vk::False,
        .srcColorBlendFactor = vk::BlendFactor::eOne,
        .dstColorBlendFactor = vk::BlendFactor::eOne,
        .colorBlendOp        = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp        = vk::BlendOp::eAdd,
        .colorWriteMask      = rgba
    };
    vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blend };
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        {
            .stageCount          = 2,
            .pStages             = stages,
            .pVertexInputState   = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState      = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState   = &multisampling,
            .pDepthStencilState  = &noDepth,
            .pColorBlendState    = &colorBlending,
            .pDynamicState       = &dynamicState,
            .layout              = layout,
            .renderPass          = nullptr
        },
        {.colorAttachmentCount = 1, .pColorAttachmentFormats = &colorFormat, .depthAttachmentFormat = depthFormat }
    };
    return vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
    }

}   // NAMESPACE CRUCIBLE
