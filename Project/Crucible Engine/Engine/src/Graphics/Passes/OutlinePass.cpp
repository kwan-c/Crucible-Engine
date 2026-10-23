/*************************************************************************
  \file       OutlinePass.cpp
  \brief
    See OutlinePass.h.
*************************************************************************/
#include "Graphics/Passes/OutlinePass.h"

#include "Graphics/RenderResources.h"
#include "Graphics/Vulkan/RenderLayouts.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Vulkan/VulkanCommands.h"

#include <array>

namespace Crucible {

    void OutlinePass::Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout materialSetLayout, vk::Format colourFormat) {


        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo      viewportState{ .viewportCount = 1, .scissorCount = 1 };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };
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

        // ---- Mask: the selected object's instances -> R8, no depth (shows through walls) ----
        {
            vk::raii::ShaderModule module = LoadShaderModule(context, "OutlineMask.spv");
            vk::PipelineShaderStageCreateInfo stages[] = {
                { .stage = vk::ShaderStageFlagBits::eVertex,   .module = module, .pName = "maskVert" },
                { .stage = vk::ShaderStageFlagBits::eFragment, .module = module, .pName = "maskFrag" }
            };
            auto binding    = Vertex::GetBindingDescription();
            auto attributes = Vertex::GetAttributeDescriptions();
            const std::array solidAttributes{attributes[0], attributes[5], attributes[6]};
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
                .vertexBindingDescriptionCount   = 1,
                .pVertexBindingDescriptions      = &binding,
                .vertexAttributeDescriptionCount = static_cast<uint32_t>(solidAttributes.size()),
                .pVertexAttributeDescriptions    = solidAttributes.data()
            };
            vk::PipelineColorBlendAttachmentState blend{ .blendEnable = vk::False, .colorWriteMask = vk::ColorComponentFlagBits::eR };
            vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blend };

            // Set 1 and CutoutPush are only read by the cut-out pipeline; one layout for both, so
            // switching pipelines keeps the sets bound.
            const std::array<vk::DescriptorSetLayout, 2> setLayouts{ frameSetLayout, materialSetLayout };
            vk::PushConstantRange cutoutPush{ .stageFlags = vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(CutoutPush) };
            vk::PipelineLayoutCreateInfo layoutInfo{
                .setLayoutCount = static_cast<uint32_t>(setLayouts.size()), .pSetLayouts = setLayouts.data(),
                .pushConstantRangeCount = 1, .pPushConstantRanges = &cutoutPush };
            m_outlineMaskLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);

            const vk::Format maskFormat = RenderLayouts::kOutlineMaskFormat;
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
                    .layout              = m_outlineMaskLayout,
                    .renderPass          = nullptr
                },
                {.colorAttachmentCount = 1, .pColorAttachmentFormats = &maskFormat }
            };
            m_outlineMaskPipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());

            // Cut-out materials: also the uv (location 2), so the mask follows the leaf, not its card.
            const vk::PipelineShaderStageCreateInfo cutoutStages[] = {
                { .stage = vk::ShaderStageFlagBits::eVertex,   .module = module, .pName = "maskCutoutVert" },
                { .stage = vk::ShaderStageFlagBits::eFragment, .module = module, .pName = "maskCutoutFrag" }
            };
            const std::array cutoutAttributes{ attributes[0], attributes[2], attributes[5], attributes[6] };
            vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(cutoutAttributes.size());
            vertexInputInfo.pVertexAttributeDescriptions    = cutoutAttributes.data();
            chain.get<vk::GraphicsPipelineCreateInfo>().pStages = cutoutStages;
            m_outlineMaskCutoutPipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
        }

        // ---- Composite: fullscreen triangle, edge of the mask -> outline colour, blended ----
        {
            vk::DescriptorSetLayoutBinding maskBinding{
                .binding         = 0,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eFragment
            };
            vk::DescriptorSetLayoutCreateInfo setInfo{ .bindingCount = 1, .pBindings = &maskBinding };
            m_outlineDescriptorSetLayout = vk::raii::DescriptorSetLayout(context.GetDevice(), setInfo);

            vk::raii::ShaderModule module = LoadShaderModule(context, "OutlineComposite.spv");
            vk::PipelineShaderStageCreateInfo stages[] = {
                { .stage = vk::ShaderStageFlagBits::eVertex,   .module = module, .pName = "compositeVert" },
                { .stage = vk::ShaderStageFlagBits::eFragment, .module = module, .pName = "compositeFrag" }
            };
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};   // fullscreen triangle from the vertex index

            // Straight alpha blending over the finished scene.
            vk::PipelineColorBlendAttachmentState blend{
                .blendEnable         = vk::True,
                .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
                .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
                .colorBlendOp        = vk::BlendOp::eAdd,
                .srcAlphaBlendFactor = vk::BlendFactor::eZero,
                .dstAlphaBlendFactor = vk::BlendFactor::eOne,
                .alphaBlendOp        = vk::BlendOp::eAdd,
                .colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
            };
            vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blend };

            vk::PushConstantRange push{ .stageFlags = vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(OutlineCompositePush) };
            vk::DescriptorSetLayout setLayout = *m_outlineDescriptorSetLayout;
            vk::PipelineLayoutCreateInfo layoutInfo{
                .setLayoutCount = 1, .pSetLayouts = &setLayout,
                .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
            m_outlineCompositeLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);

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
                    .pDepthStencilState  = &noDepth,
                    .pColorBlendState    = &colorBlending,
                    .pDynamicState       = &dynamicState,
                    .layout              = m_outlineCompositeLayout,
                    .renderPass          = nullptr
                },
                {.colorAttachmentCount = 1, .pColorAttachmentFormats = &colorFormat }
            };
            m_outlineCompositePipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
        }

    }   // OutlinePass::Create()

    void OutlinePass::RecordMask(vk::CommandBuffer cmd, vk::Image maskImage, vk::ImageView maskView, vk::Extent2D extent,
                                 vk::DescriptorSet frameSet, const std::vector<DrawBatch>& meshes) const {

        // Last frame's composite sampled it; contents are re-cleared, so discard them.
        TransitionImageLayout(cmd, maskImage, vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
            {}, vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::PipelineStageFlagBits2::eColorAttachmentOutput);

        vk::RenderingAttachmentInfo color{
            .imageView   = maskView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp      = vk::AttachmentLoadOp::eClear,
            .storeOp     = vk::AttachmentStoreOp::eStore,
            .clearValue  = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f)
        };
        vk::RenderingInfo renderingInfo{
            .renderArea           = { .offset = { 0, 0 }, .extent = extent },
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color
        };
        cmd.beginRendering(renderingInfo);
        // Same flipped viewport as the main pass, so mask pixels line up with the scene's.
        cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(extent.height), static_cast<float>(extent.width),
                                        -static_cast<float>(extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_outlineMaskLayout, 0, frameSet, nullptr);
        const DepthPassPipelines pipelines{ .solid = *m_outlineMaskPipeline, .cutout = *m_outlineMaskCutoutPipeline, .layout = *m_outlineMaskLayout,
                                            .cutoutPushStages = vk::ShaderStageFlagBits::eFragment, .cutoutPushOffset = 0,
                                            .dynamicCull = false };   // no culling at all: the mask covers both sides
        m_resources.DrawDepthBatches(cmd, meshes, pipelines);   // binds the pipelines itself
        cmd.endRendering();

        TransitionImageLayout(cmd, maskImage, vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader);

    }   // OutlinePass::RecordMask()

    void OutlinePass::RecordComposite(vk::CommandBuffer cmd, vk::Image colourImage, vk::ImageView colourView, vk::Extent2D extent,
                                      vk::DescriptorSet outlineSet, const OutlineCompositePush& style) const {

        // The main pass's writes must land before blending reads them back.
        TransitionImageLayout(cmd, colourImage, vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eColorAttachmentOutput);

        vk::RenderingAttachmentInfo color{
            .imageView   = colourView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp      = vk::AttachmentLoadOp::eLoad,
            .storeOp     = vk::AttachmentStoreOp::eStore
        };
        vk::RenderingInfo renderingInfo{
            .renderArea           = { .offset = { 0, 0 }, .extent = extent },
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color
        };
        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_outlineCompositePipeline);
        // Fullscreen triangle: orientation doesn't matter, the shader works in pixel coordinates.
        cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_outlineCompositeLayout, 0, outlineSet, nullptr);
        cmd.pushConstants<OutlineCompositePush>(*m_outlineCompositeLayout, vk::ShaderStageFlagBits::eFragment, 0, style);
        cmd.draw(3, 1, 0, 0);
        cmd.endRendering();

    }   // OutlinePass::RecordComposite()

}   // NAMESPACE CRUCIBLE
