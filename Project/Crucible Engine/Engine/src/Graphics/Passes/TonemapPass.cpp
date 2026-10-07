/*************************************************************************
  \file       TonemapPass.cpp
  \brief
    See TonemapPass.h.
*************************************************************************/
#include "Graphics/Passes/TonemapPass.h"

#include "Graphics/Vulkan/RenderLayouts.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Passes/FullscreenPipeline.h"

#include <algorithm>

namespace Crucible {

    void TonemapPass::Create(VulkanContext& context, vk::DescriptorSetLayout twoTextureSetLayout, vk::Format colourFormat, vk::Format depthFormat) {

        vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "../../../Assets/Shaders/Post.spv");
        vk::PushConstantRange push{ .stageFlags = vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(PostPush) };
        {
            vk::DescriptorSetLayout setLayout = twoTextureSetLayout;
            vk::PipelineLayoutCreateInfo layoutInfo{
                .setLayoutCount = 1, .pSetLayouts = &setLayout,
                .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
            m_tonemapLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);
        }
        // The tonemap pass keeps the scene's depth attachment bound (unused here) so the debug
        // lines drawn right after it, in the same pass, can still be depth-tested.
        m_tonemapPipeline = CreateFullscreenPipeline(context, shaderModule, "tonemapFrag", false, *m_tonemapLayout, colourFormat, depthFormat);

    }   // TonemapPass::Create()

    void TonemapPass::Record(vk::CommandBuffer cmd, const PostTargets& t, vk::Extent2D extent,
                             const PostSettings& settings, const EnvironmentSettings& env, float time) const {

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_tonemapPipeline);
        cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_tonemapLayout, 0, *t.tonemapSet, nullptr);

        // The glow is the sum of bloomCount steps; dividing by that keeps bloomIntensity meaning
        // roughly the same thing whatever the render target's size.
        const bool  bloom    = settings.bloom && t.bloomCount > 0;
        const float strength = bloom ? std::max(settings.bloomIntensity, 0.0f) / static_cast<float>(t.bloomCount) : 0.0f;
        PostPush push;
        push.a = glm::vec4(std::max(env.exposure, 0.0f), strength, settings.fxaa ? 1.0f : 0.0f, time);
        push.b = glm::vec4(1.0f / static_cast<float>(std::max(extent.width, 1u)), 1.0f / static_cast<float>(std::max(extent.height, 1u)),
                           glm::clamp(env.vignetteIntensity, 0.0f, 1.0f), glm::clamp(env.vignetteSoftness, 0.01f, 1.0f));

        // The scene's look. Temperature shifts the balance between red and blue (warm = more red,
        // less blue); the colour filter multiplies on top.
        const float     warmth = glm::clamp(env.temperature, -1.0f, 1.0f) * 0.25f;
        const glm::vec3 white  = glm::max(env.colourFilter, glm::vec3(0.0f)) * glm::vec3(1.0f + warmth, 1.0f, 1.0f - warmth);
        push.c = glm::vec4(glm::clamp(env.contrast, 0.0f, 3.0f), glm::clamp(env.saturation, 0.0f, 3.0f), glm::clamp(env.filmGrain, 0.0f, 1.0f), 0.0f);
        push.d = glm::vec4(white, glm::clamp(env.chromaticAberration, 0.0f, 1.0f) * 0.01f);
        cmd.pushConstants<PostPush>(*m_tonemapLayout, vk::ShaderStageFlagBits::eFragment, 0, push);
        cmd.draw(3, 1, 0, 0);

    }   // TonemapPass::Record()

}   // NAMESPACE CRUCIBLE
