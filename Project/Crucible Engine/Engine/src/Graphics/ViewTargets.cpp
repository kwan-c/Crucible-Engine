/*************************************************************************
  \file       ViewTargets.cpp
  \brief
    See ViewTargets.h.
*************************************************************************/
#include "Graphics/ViewTargets.h"

#include "Graphics/Vulkan/RenderLayouts.h"   // image format constants

#include <algorithm>

namespace Crucible {

    void ViewTargetAllocator::Init(uint32_t maxViews, vk::DescriptorSetLayout oneTextureSetLayout,
                                   vk::DescriptorSetLayout twoTextureSetLayout, vk::Format depthFormat) {

        m_maxViews            = maxViews;
        m_oneTextureSetLayout = oneTextureSetLayout;
        m_twoTextureSetLayout = twoTextureSetLayout;
        m_depthFormat         = depthFormat;
        CreateSamplers();
        CreateDescriptorPool();

    }   // ViewTargetAllocator::Init()

    void ViewTargetAllocator::CreateSamplers() {

        vk::SamplerCreateInfo samplerInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .anisotropyEnable = vk::False,
            .compareEnable = vk::False,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = vk::BorderColor::eIntOpaqueBlack,
            .unnormalizedCoordinates = vk::False
        };
        m_viewportSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

        // The same without filtering, for reading depth images (which not every GPU can filter).
        samplerInfo.magFilter  = vk::Filter::eNearest;
        samplerInfo.minFilter  = vk::Filter::eNearest;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        m_pointSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

    }   // ViewTargetAllocator::CreateSamplers()

    void ViewTargetAllocator::CreateDescriptorPool() {

        // Per PostTargets: the HDR image as a bloom source (1 set, 1 image), one set per bloom
        // step, and the tonemap set (2 images). One PostTargets per viewport, plus the window's.
        const uint32_t targets = m_maxViews;
        // ... plus ambient occlusion: its depth + normals set and its blur set (2 images each).
        const uint32_t sets    = targets * (kBloomMips + 4);
        vk::DescriptorPoolSize size{ .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = targets * (kBloomMips + 7) };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = sets,
            .poolSizeCount = 1, .pPoolSizes = &size };
        m_postDescriptorPool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

    }   // ViewTargetAllocator::CreateDescriptorPool()

    void ViewTargetAllocator::Create(PostTargets& t, uint32_t width, uint32_t height, uint32_t samples) {

        width  = std::max(width, 1u);
        height = std::max(height, 1u);
        t.extent = vk::Extent2D{ width, height };

        const vk::Format format = RenderLayouts::kSceneColorFormat;
        const auto makeView = [&](VulkanImage& image) {
            vk::ImageViewCreateInfo info{
                .image = image.GetImage(), .viewType = vk::ImageViewType::e2D, .format = format,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 } };
            return vk::raii::ImageView(m_context.GetDevice(), info);
        };

        t.hdrImage.Create(m_context, width, height, format, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        t.hdrView = makeView(t.hdrImage);

        // MSAA targets: what the scene is actually drawn into when samples > 1. Never sampled or
        // kept (they resolve into hdrImage and the view's depth image at the end of the pass).
        t.samples       = samples;
        t.msaaColorView = nullptr;
        t.msaaDepthView = nullptr;
        t.msaaColor     = VulkanImage{};
        t.msaaDepth     = VulkanImage{};
        if (t.samples > 1) {
            const vk::SampleCountFlagBits samples     = static_cast<vk::SampleCountFlagBits>(t.samples);
            const vk::Format              depthFormat = m_depthFormat;
            t.msaaColor.Create(m_context, width, height, format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, 1, {}, 1, samples);
            t.msaaColorView = makeView(t.msaaColor);
            t.msaaDepth.Create(m_context, width, height, depthFormat, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, 1, {}, 1, samples);
            vk::ImageViewCreateInfo depthViewInfo{
                .image = t.msaaDepth.GetImage(), .viewType = vk::ImageViewType::e2D, .format = depthFormat,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1 } };
            t.msaaDepthView = vk::raii::ImageView(m_context.GetDevice(), depthViewInfo);
        }

        // Bloom chain: each step half the size of the one before. Tiny targets get fewer steps.
        uint32_t steps = 1;
        while (steps < kBloomMips && (std::min(width, height) >> (steps + 1)) >= 2) { ++steps; }
        t.bloomCount = steps;
        for (uint32_t i = 0; i < steps; ++i) {
            t.bloomExtents[i] = vk::Extent2D{ std::max(width >> (i + 1), 1u), std::max(height >> (i + 1), 1u) };
            t.bloomImages[i].Create(m_context, t.bloomExtents[i].width, t.bloomExtents[i].height, format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            t.bloomViews[i] = makeView(t.bloomImages[i]);
        }

        // Ambient occlusion, at half size.
        t.aoExtent = vk::Extent2D{ std::max(width / 2, 1u), std::max(height / 2, 1u) };
        t.aoDepthView = nullptr;
        t.aoRawView   = nullptr;
        t.aoNormalView = nullptr;
        t.aoBlurView  = nullptr;
        t.aoDepth.Create(m_context, t.aoExtent.width, t.aoExtent.height, RenderLayouts::kAoDepthFormat, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        {
            vk::ImageViewCreateInfo info{
                .image = t.aoDepth.GetImage(), .viewType = vk::ImageViewType::e2D, .format = RenderLayouts::kAoDepthFormat,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1 } };
            t.aoDepthView = vk::raii::ImageView(m_context.GetDevice(), info);
        }
        const auto makeAoImage = [&](VulkanImage& image) {
            image.Create(m_context, t.aoExtent.width, t.aoExtent.height, RenderLayouts::kAoFormat, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            vk::ImageViewCreateInfo info{
                .image = image.GetImage(), .viewType = vk::ImageViewType::e2D, .format = RenderLayouts::kAoFormat,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 } };
            return vk::raii::ImageView(m_context.GetDevice(), info);
        };
        t.aoRawView  = makeAoImage(t.aoRaw);
        t.aoNormal.Create(m_context, t.aoExtent.width, t.aoExtent.height, RenderLayouts::kAoNormalFormat, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        {
            vk::ImageViewCreateInfo info{
                .image = t.aoNormal.GetImage(), .viewType = vk::ImageViewType::e2D, .format = RenderLayouts::kAoNormalFormat,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 } };
            t.aoNormalView = vk::raii::ImageView(m_context.GetDevice(), info);
        }
        t.aoBlurView = makeAoImage(t.aoBlur);

        // The sets are allocated once and re-pointed whenever the images are recreated.
        if (!*t.hdrSet) {
            const auto allocate = [&](vk::DescriptorSetLayout layout) {
                vk::DescriptorSetLayout raw = layout;
                vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = m_postDescriptorPool, .descriptorSetCount = 1, .pSetLayouts = &raw };
                return std::move(m_context.GetDevice().allocateDescriptorSets(allocInfo).front());
            };
            t.hdrSet = allocate(m_oneTextureSetLayout);
            for (auto& set : t.bloomSets) { set = allocate(m_oneTextureSetLayout); }
            t.tonemapSet = allocate(m_twoTextureSetLayout);
            t.aoDepthSet = allocate(m_twoTextureSetLayout);
            t.aoBlurSet  = allocate(m_twoTextureSetLayout);
        }

        const auto write = [&](const vk::raii::DescriptorSet& set, uint32_t binding, const vk::raii::ImageView& view) {
            vk::DescriptorImageInfo imageInfo{
                .sampler = *m_viewportSampler, .imageView = *view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            vk::WriteDescriptorSet w{
                .dstSet = *set, .dstBinding = binding, .dstArrayElement = 0, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageInfo };
            m_context.GetDevice().updateDescriptorSets(w, {});
        };
        write(t.hdrSet, 0, t.hdrView);
        for (uint32_t i = 0; i < steps; ++i) { write(t.bloomSets[i], 0, t.bloomViews[i]); }
        write(t.tonemapSet, 0, t.hdrView);
        write(t.tonemapSet, 1, t.bloomViews[0]);

        // Depth is read unfiltered (m_pointSampler); the raw occlusion with the usual sampler.
        const auto writeDepth = [&](const vk::raii::DescriptorSet& set, uint32_t binding) {
            vk::DescriptorImageInfo imageInfo{
                .sampler = *m_pointSampler, .imageView = *t.aoDepthView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            vk::WriteDescriptorSet w{
                .dstSet = *set, .dstBinding = binding, .dstArrayElement = 0, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageInfo };
            m_context.GetDevice().updateDescriptorSets(w, {});
        };
        writeDepth(t.aoDepthSet, 0);
        {
            // Normals are read unfiltered too: blending the directions of two different objects
            // across an edge would give a direction that belongs to neither.
            vk::DescriptorImageInfo imageInfo{
                .sampler = *m_pointSampler, .imageView = *t.aoNormalView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            vk::WriteDescriptorSet w{
                .dstSet = *t.aoDepthSet, .dstBinding = 1, .dstArrayElement = 0, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageInfo };
            m_context.GetDevice().updateDescriptorSets(w, {});
        }
        write(t.aoBlurSet, 0, t.aoRawView);
        writeDepth(t.aoBlurSet, 1);

    }   // ViewTargetAllocator::Create()

}   // NAMESPACE CRUCIBLE
