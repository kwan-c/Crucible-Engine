/*************************************************************************
  \file       ToonOutlinePass.cpp
  \brief
    See ToonOutlinePass.h.
*************************************************************************/
#include "Graphics/Passes/ToonOutlinePass.h"

#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Passes/FullscreenPipeline.h"
#include "Graphics/Vulkan/VulkanCommands.h"

#include <algorithm>
#include <cmath>

namespace Crucible {

    void ToonOutlinePass::Create(VulkanContext& context, vk::DescriptorSetLayout textureSetLayout, vk::Format colourFormat) {

        vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "Post.spv");
        vk::PushConstantRange push{ .stageFlags = vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(PostPush) };
        vk::DescriptorSetLayout setLayout = textureSetLayout;
        vk::PipelineLayoutCreateInfo layoutInfo{
            .setLayoutCount = 1, .pSetLayouts = &setLayout,
            .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
        m_layout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);
        // The shader returns a factor per pixel (1 = untouched); blending multiplies the image by it.
        m_pipeline = CreateFullscreenPipeline(context, shaderModule, "toonOutlineFrag", FullscreenBlend::Multiply, *m_layout,
                                              colourFormat, vk::Format::eUndefined);

    }   // ToonOutlinePass::Create()

    void ToonOutlinePass::Record(vk::CommandBuffer cmd, PostTargets& t, const EnvironmentSettings& env) const {

        // The scene pass left the HDR image ready to be sampled (its barrier ends at the fragment
        // shader stage, which this one starts from, so the two chain); draw into it once more.
        TransitionImageLayout(cmd, t.hdrImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eShaderSampledRead,
            vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput | vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

        vk::RenderingAttachmentInfo color{
            .imageView   = t.hdrView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp      = vk::AttachmentLoadOp::eLoad,
            .storeOp     = vk::AttachmentStoreOp::eStore
        };
        vk::RenderingInfo renderingInfo{
            .renderArea           = { .offset = { 0, 0 }, .extent = t.extent },
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color
        };
        cmd.beginRendering(renderingInfo);
        // Plain (unflipped) viewport: the shader reads the line data at its own pixel position.
        cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(t.extent.width), static_cast<float>(t.extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), t.extent));
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_layout, 0, *t.lineDataSet, nullptr);

        const float fadeEnd = std::max(env.toonLineFadeDistance, 0.1f);
        PostPush push;
        push.a = glm::vec4(glm::clamp(env.toonLineDarkness, 0.0f, 1.0f), std::clamp(env.toonLineWidth, 0.5f, 3.0f),
                           std::cos(glm::radians(std::clamp(env.toonLineCreaseAngle, 1.0f, 179.0f))),
                           std::clamp(env.toonLineDepthGap, 0.001f, 1.0f));
        push.b = glm::vec4(fadeEnd * 0.5f, fadeEnd, static_cast<float>(t.extent.width), static_cast<float>(t.extent.height));
        push.c = glm::vec4(glm::clamp(env.toonLineCreaseStrength, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f);
        cmd.pushConstants<PostPush>(*m_layout, vk::ShaderStageFlagBits::eFragment, 0, push);
        cmd.draw(3, 1, 0, 0);
        cmd.endRendering();

        TransitionImageLayout(cmd, t.hdrImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader);

    }   // ToonOutlinePass::Record()

}   // NAMESPACE CRUCIBLE
