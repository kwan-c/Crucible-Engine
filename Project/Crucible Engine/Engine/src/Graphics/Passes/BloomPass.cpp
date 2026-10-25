/*************************************************************************
  \file       BloomPass.cpp
  \brief
    See BloomPass.h.
*************************************************************************/
#include "Graphics/Passes/BloomPass.h"

#include "Graphics/Vulkan/RenderLayouts.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Passes/FullscreenPipeline.h"
#include "Graphics/Vulkan/VulkanCommands.h"

#include <algorithm>

namespace Crucible {

    void BloomPass::Create(VulkanContext& context, vk::DescriptorSetLayout textureSetLayout, vk::Format colourFormat) {

        vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "Post.spv");
        vk::PushConstantRange push{ .stageFlags = vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(PostPush) };
        {
            vk::DescriptorSetLayout setLayout = textureSetLayout;
            vk::PipelineLayoutCreateInfo layoutInfo{
                .setLayoutCount = 1, .pSetLayouts = &setLayout,
                .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
            m_bloomLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);
        }
        m_bloomDownPipeline = CreateFullscreenPipeline(context, shaderModule, "bloomDownFrag", false, *m_bloomLayout, colourFormat, vk::Format::eUndefined);
        m_bloomUpPipeline   = CreateFullscreenPipeline(context, shaderModule, "bloomUpFrag",   true,  *m_bloomLayout, colourFormat, vk::Format::eUndefined);

    }   // BloomPass::Create()

    void BloomPass::Record(vk::CommandBuffer cmd, PostTargets& t, const PostSettings& settings, float exposureSetting) const {

        const auto toAttachment = [&](uint32_t i, bool keepContents) {
            TransitionImageLayout(cmd, t.bloomImages[i].GetImage(), vk::ImageAspectFlagBits::eColor,
                keepContents ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal,
                keepContents ? vk::AccessFlagBits2::eShaderSampledRead : vk::AccessFlags2{},
                vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput);
        };
        const auto toSampled = [&](uint32_t i) {
            TransitionImageLayout(cmd, t.bloomImages[i].GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader);
        };
        const auto begin = [&](uint32_t i, vk::AttachmentLoadOp loadOp) {
            vk::RenderingAttachmentInfo color{
                .imageView   = t.bloomViews[i],
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp      = loadOp,
                .storeOp     = vk::AttachmentStoreOp::eStore,
                .clearValue  = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f)
            };
            vk::RenderingInfo renderingInfo{
                .renderArea           = { .offset = { 0, 0 }, .extent = t.bloomExtents[i] },
                .layerCount           = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments    = &color
            };
            cmd.beginRendering(renderingInfo);
            // Plain (unflipped) viewport: these passes work in texture space.
            cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(t.bloomExtents[i].width), static_cast<float>(t.bloomExtents[i].height), 0.0f, 1.0f));
            cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), t.bloomExtents[i]));
        };

        // Bloom off: the tonemap pass still samples step 0, so give it a defined (black) image.
        if (!settings.bloom || t.bloomCount == 0) {
            toAttachment(0, false);
            begin(0, vk::AttachmentLoadOp::eClear);
            cmd.endRendering();
            toSampled(0);
            return;
        }

        const float exposure = std::max(exposureSetting, 0.0f);

        // Down the chain: each step filters the previous one into half the size. The first step
        // reads the scene and keeps only what is brighter than the threshold.
        vk::DescriptorSet source       = *t.hdrSet;
        vk::Extent2D      sourceExtent = t.extent;
        for (uint32_t i = 0; i < t.bloomCount; ++i) {
            toAttachment(i, false);
            begin(i, vk::AttachmentLoadOp::eDontCare);
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_bloomDownPipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_bloomLayout, 0, source, nullptr);
            PostPush push;
            push.a = glm::vec4(1.0f / static_cast<float>(sourceExtent.width), 1.0f / static_cast<float>(sourceExtent.height),
                               i == 0 ? std::max(settings.bloomThreshold, 0.0f) : -1.0f, std::max(settings.bloomKnee, 1e-4f));
            push.b = glm::vec4(exposure, 0.0f, 0.0f, 0.0f);
            cmd.pushConstants<PostPush>(*m_bloomLayout, vk::ShaderStageFlagBits::eFragment, 0, push);
            cmd.draw(3, 1, 0, 0);
            cmd.endRendering();
            toSampled(i);
            source       = *t.bloomSets[i];
            sourceExtent = t.bloomExtents[i];
        }

        // Back up the chain: each step is blurred up and ADDED onto the next larger one, so step 0
        // ends up holding glow at every scale - tight around small lights, wide around big ones.
        for (uint32_t i = t.bloomCount - 1; i > 0; --i) {
            toAttachment(i - 1, true);
            begin(i - 1, vk::AttachmentLoadOp::eLoad);
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_bloomUpPipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_bloomLayout, 0, *t.bloomSets[i], nullptr);
            PostPush push;
            push.a = glm::vec4(1.0f / static_cast<float>(t.bloomExtents[i].width), 1.0f / static_cast<float>(t.bloomExtents[i].height), 0.0f, 0.0f);
            cmd.pushConstants<PostPush>(*m_bloomLayout, vk::ShaderStageFlagBits::eFragment, 0, push);
            cmd.draw(3, 1, 0, 0);
            cmd.endRendering();
            toSampled(i - 1);
        }

    }   // BloomPass::Record()

}   // NAMESPACE CRUCIBLE
