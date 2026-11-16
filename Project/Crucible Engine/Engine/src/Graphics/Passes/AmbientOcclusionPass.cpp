/*************************************************************************
  \file       AmbientOcclusionPass.cpp
  \brief
    See AmbientOcclusionPass.h.
*************************************************************************/
#include "Graphics/Passes/AmbientOcclusionPass.h"

#include "Graphics/RenderResources.h"
#include "Graphics/Vulkan/RenderLayouts.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Passes/FullscreenPipeline.h"
#include "Graphics/Vulkan/VulkanCommands.h"

#include <algorithm>
#include <array>

namespace Crucible {

    void AmbientOcclusionPass::Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout materialSetLayout,
                                      vk::DescriptorSetLayout twoTextureSetLayout) {

        // Cull mode is dynamic so double-sided materials can turn it off (as in the scene pass).
        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor,
            vk::DynamicState::eCullMode, vk::DynamicState::eFrontFace };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo      viewportState{ .viewportCount = 1, .scissorCount = 1 };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };
        // Same culling as the main pipeline, so the depth it writes is the depth the scene will have.
        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable        = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode             = vk::PolygonMode::eFill,
            .cullMode                = vk::CullModeFlagBits::eBack,
            .frontFace               = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable         = vk::False,
            .lineWidth               = 1.0f
        };
        vk::PipelineMultisampleStateCreateInfo  multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };
        vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable       = vk::True,
            .depthWriteEnable      = vk::True,
            .depthCompareOp        = vk::CompareOp::eLess,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable     = vk::False
        };
        // One colour output: the view-space normal.
        vk::PipelineColorBlendAttachmentState blend{
            .blendEnable    = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA };
        vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blend };

        vk::raii::ShaderModule module = LoadShaderModule(context, "DepthNormal.spv");
        vk::PipelineShaderStageCreateInfo stages[] = {
            { .stage = vk::ShaderStageFlagBits::eVertex,   .module = module, .pName = "depthNormalVert" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = module, .pName = "depthNormalFrag" }
        };

        auto binding    = Vertex::GetBindingDescription();
        auto attributes = Vertex::GetAttributeDescriptions();
        const std::array solidAttributes{attributes[0], attributes[1], attributes[5], attributes[6]};
        const std::array cutoutAttributes{attributes[0], attributes[1], attributes[2], attributes[5], attributes[6]};
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount   = 1,
            .pVertexBindingDescriptions      = &binding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(solidAttributes.size()),   // position and normal
            .pVertexAttributeDescriptions    = solidAttributes.data()
        };

        // Set 0 (camera and instance data); set 1 and CutoutPush only for the cut-out pipeline.
        // One layout for both, so switching pipelines keeps the sets bound.
        const std::array<vk::DescriptorSetLayout, 2> prepassSetLayouts{ frameSetLayout, materialSetLayout };
        vk::PushConstantRange cutoutPush{ .stageFlags = vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(CutoutPush) };
        vk::PipelineLayoutCreateInfo prepassLayoutInfo{
            .setLayoutCount = static_cast<uint32_t>(prepassSetLayouts.size()), .pSetLayouts = prepassSetLayouts.data(),
            .pushConstantRangeCount = 1, .pPushConstantRanges = &cutoutPush };
        m_prepassLayout = vk::raii::PipelineLayout(context.GetDevice(), prepassLayoutInfo);

        const vk::Format normalFormat = RenderLayouts::kAoNormalFormat;
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
                .layout              = m_prepassLayout,
                .renderPass          = nullptr
            },
            {.colorAttachmentCount = 1, .pColorAttachmentFormats = &normalFormat, .depthAttachmentFormat = RenderLayouts::kAoDepthFormat }
        };
        m_depthPrepassPipeline = vk::raii::Pipeline(context.GetDevice(), context.GetPipelineCache(), chain.get<vk::GraphicsPipelineCreateInfo>());

        // Cut-out materials: also the uv (location 2), and pixels the material cuts out are dropped.
        const vk::PipelineShaderStageCreateInfo cutoutStages[] = {
            { .stage = vk::ShaderStageFlagBits::eVertex,   .module = module, .pName = "depthNormalCutoutVert" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = module, .pName = "depthNormalCutoutFrag" }
        };
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(cutoutAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = cutoutAttributes.data();   // position, normal, uv
        chain.get<vk::GraphicsPipelineCreateInfo>().pStages = cutoutStages;
        m_cutoutPrepassPipeline = vk::raii::Pipeline(context.GetDevice(), context.GetPipelineCache(), chain.get<vk::GraphicsPipelineCreateInfo>());


        // Occlusion and its blur: fullscreen steps reading two textures each (Post.spv).
        vk::raii::ShaderModule postModule = LoadShaderModule(context, "Post.spv");
        vk::PushConstantRange push{ .stageFlags = vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(PostPush) };
        {
            vk::DescriptorSetLayout setLayout = twoTextureSetLayout;
            vk::PipelineLayoutCreateInfo layoutInfo{
                .setLayoutCount = 1, .pSetLayouts = &setLayout,
                .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
            m_aoLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);
        }
        m_aoPipeline     = CreateFullscreenPipeline(context, postModule, "aoFrag",     false, *m_aoLayout, RenderLayouts::kAoFormat, vk::Format::eUndefined);   // depth + normals
        m_aoBlurPipeline = CreateFullscreenPipeline(context, postModule, "aoBlurFrag", false, *m_aoLayout, RenderLayouts::kAoFormat, vk::Format::eUndefined);

    }   // AmbientOcclusionPass::Create()

    void AmbientOcclusionPass::Record(vk::CommandBuffer cmd, PostTargets& t, const PostSettings& settings, vk::DescriptorSet frameSet,
                                      const std::vector<DrawBatch>& meshes, const glm::vec4& projInfo) const {

        if (!*t.aoBlurView) { return; }
        const vk::Extent2D extent = t.aoExtent;

        const auto beginColour = [&](VulkanImage& image, const vk::raii::ImageView& view) {
            TransitionImageLayout(cmd, image.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                {}, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput);
            vk::RenderingAttachmentInfo color{
                .imageView   = view,
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp      = vk::AttachmentLoadOp::eClear,
                .storeOp     = vk::AttachmentStoreOp::eStore,
                .clearValue  = vk::ClearColorValue(1.0f, 1.0f, 1.0f, 1.0f)   // white = no occlusion
            };
            vk::RenderingInfo renderingInfo{
                .renderArea = { .offset = { 0, 0 }, .extent = extent }, .layerCount = 1,
                .colorAttachmentCount = 1, .pColorAttachments = &color };
            cmd.beginRendering(renderingInfo);
            cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
            cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
        };
        const auto endColour = [&](VulkanImage& image) {
            cmd.endRendering();
            TransitionImageLayout(cmd, image.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader);
        };

        // Off: the scene shader still samples the image, so hand it an all-white one.
        if (!settings.ao) {
            beginColour(t.aoBlur, t.aoBlurView);
            endColour(t.aoBlur);
            return;
        }

        // 1. The scene's depth and smooth normals, at half size: every mesh the main pass will draw.
        TransitionImageLayout(cmd, t.aoNormal.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
            {}, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);
        TransitionImageLayout(cmd, t.aoDepth.GetImage(), vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
            {}, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests);
        {
            vk::RenderingAttachmentInfo depth{
                .imageView   = t.aoDepthView,
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp      = vk::AttachmentLoadOp::eClear,
                .storeOp     = vk::AttachmentStoreOp::eStore,
                .clearValue  = vk::ClearDepthStencilValue(1.0f, 0)
            };
            vk::RenderingAttachmentInfo normal{
                .imageView   = t.aoNormalView,
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp      = vk::AttachmentLoadOp::eClear,
                .storeOp     = vk::AttachmentStoreOp::eStore,
                .clearValue  = vk::ClearColorValue(0.5f, 0.5f, 1.0f, 1.0f)   // "facing the camera"; only sky pixels keep it
            };
            vk::RenderingInfo renderingInfo{
                .renderArea = { .offset = { 0, 0 }, .extent = extent }, .layerCount = 1,
                .colorAttachmentCount = 1, .pColorAttachments = &normal, .pDepthAttachment = &depth };
            cmd.beginRendering(renderingInfo);
            // Flipped like the main pass, so its pixels line up with the scene's.
            cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(extent.height), static_cast<float>(extent.width),
                                            -static_cast<float>(extent.height), 0.0f, 1.0f));
            cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_prepassLayout, 0, frameSet, nullptr);
            const DepthPassPipelines pipelines{ .solid = *m_depthPrepassPipeline, .cutout = *m_cutoutPrepassPipeline, .layout = *m_prepassLayout,
                                                .cutoutPushStages = vk::ShaderStageFlagBits::eFragment, .cutoutPushOffset = 0,
                                                  .dynamicCull = true, .cull = vk::CullModeFlagBits::eBack,
                                                  .dynamicFrontFace = true };
            m_resources.DrawDepthBatches(cmd, meshes, pipelines);   // binds the pipelines itself
            cmd.endRendering();
        }
        TransitionImageLayout(cmd, t.aoNormal.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader);
        TransitionImageLayout(cmd, t.aoDepth.GetImage(), vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eFragmentShader);

        PostPush push;
        push.a = glm::vec4(1.0f / static_cast<float>(extent.width), 1.0f / static_cast<float>(extent.height),
                           std::max(settings.aoRadius, 0.01f), std::max(settings.aoIntensity, 0.0f));
        push.b = projInfo;

        // 2. Occlusion from the depth image.
        beginColour(t.aoRaw, t.aoRawView);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_aoPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_aoLayout, 0, *t.aoDepthSet, nullptr);
        cmd.pushConstants<PostPush>(*m_aoLayout, vk::ShaderStageFlagBits::eFragment, 0, push);
        cmd.draw(3, 1, 0, 0);
        endColour(t.aoRaw);

        // 3. Smoothed.
        beginColour(t.aoBlur, t.aoBlurView);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_aoBlurPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_aoLayout, 0, *t.aoBlurSet, nullptr);
        cmd.pushConstants<PostPush>(*m_aoLayout, vk::ShaderStageFlagBits::eFragment, 0, push);
        cmd.draw(3, 1, 0, 0);
        endColour(t.aoBlur);

    }   // AmbientOcclusionPass::Record()

}   // NAMESPACE CRUCIBLE
