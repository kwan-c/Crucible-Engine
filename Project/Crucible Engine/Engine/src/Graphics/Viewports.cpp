/*************************************************************************
  \file       Viewports.cpp
  \brief
    See Viewports.h.
*************************************************************************/
#include "Graphics/Viewports.h"

#include "Graphics/Vulkan/RenderLayouts.h"   // image format constants

#include <stdexcept>

namespace Crucible {

    void ViewportSet::Init(uint32_t framesInFlight, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout maskSetLayout,
                           vk::Format colourFormat, vk::Format depthFormat) {

        m_framesInFlight = framesInFlight;
        m_frameSetLayout = frameSetLayout;
        m_maskSetLayout  = maskSetLayout;
        m_colourFormat   = colourFormat;
        m_depthFormat    = depthFormat;
        CreateDescriptorPools();

    }   // ViewportSet::Init()

    ViewportHandle ViewportSet::Create(uint32_t width, uint32_t height, uint32_t samples, ViewportRole role) {

        if (m_viewports.size() >= kMaxViewports) {
            throw std::runtime_error("[Renderer] CreateViewport: kMaxViewports exceeded; raise the limit if you genuinely need more.");
        }

        ViewportHandle handle{ m_nextViewportId++ };
        Viewport& vp = m_viewports[handle.id];   // default-constructs in place
        vp.role = role;
        CreateViewportImages(vp, width, height, samples);
        CreateViewportUniformsAndDescriptors(vp);
        return handle;

    }   // ViewportSet::Create()

    void ViewportSet::Resize(ViewportHandle handle, uint32_t width, uint32_t height, uint32_t samples) {

        Viewport& vp = Get(handle);
        if (vp.extent.width == width && vp.extent.height == height) { return; }   // no-op: avoids rebuilding on every identical layout pass
        if (width == 0 || height == 0) { return; }                                // panel fully collapsed; keep the last valid image

        // The old images might still be read by an in-flight frame (e.g. ImGui sampling last
        // frame's texture while this frame is being recorded). Resizing is a rare, interactive-
        // only event (dragging a dock splitter), so a full wait here is the simplest correct
        // option; it is not something that happens on the per-frame hot path.
        m_context.GetDevice().waitIdle();

        // Only the images change size; the uniform buffers/descriptor sets are resolution-
        // independent (they just hold view/proj), so keep reusing the ones allocated at
        // CreateViewport time instead of re-allocating from the (fixed-capacity) descriptor pool.
        CreateViewportImages(vp, width, height, samples);

    }   // ViewportSet::Resize()

    void ViewportSet::Destroy(ViewportHandle handle) {

        auto it = m_viewports.find(handle.id);
        if (it == m_viewports.end()) { return; }

        // Same reasoning as ResizeViewport: don't tear down GPU resources a frame in flight
        // might still be using.
        m_context.GetDevice().waitIdle();
        m_viewports.erase(it);

    }   // ViewportSet::Destroy()

    Viewport& ViewportSet::Get(ViewportHandle handle) {

        auto it = m_viewports.find(handle.id);
        if (it == m_viewports.end()) {
            throw std::runtime_error("[Renderer] Unknown or already-destroyed ViewportHandle.");
        }
        return it->second;

    }   // ViewportSet::Get()

    const Viewport& ViewportSet::Get(ViewportHandle handle) const {

        auto it = m_viewports.find(handle.id);
        if (it == m_viewports.end()) {
            throw std::runtime_error("[Renderer] Unknown or already-destroyed ViewportHandle.");
        }
        return it->second;

    }   // ViewportSet::Get()

    void ViewportSet::CreateDescriptorPools() {

        // Sized up front for the worst case (kMaxViewports simultaneously alive, each with its
        // own set per frame in flight) so viewport creation never has to touch pool capacity.
        uint32_t maxSets = kMaxViewports * m_framesInFlight;
        // Each set 0 = FrameUniforms UBO (binding 0), five storage buffers (lights 1, instances 2,
        // particles 5, light-cluster grid 6 and indices 7), shadow map image + comparison sampler (3, 4).
        // ... plus three textures: the environment map (8), the view's ambient occlusion (9) and
        // its fog volume (10).
        std::array<vk::DescriptorPoolSize, 5> poolSizes{ {
            { .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = maxSets * 3 },
            { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = maxSets },
            { .type = vk::DescriptorType::eStorageBuffer, .descriptorCount = maxSets * 5 },
            { .type = vk::DescriptorType::eSampledImage,  .descriptorCount = maxSets },
            { .type = vk::DescriptorType::eSampler,       .descriptorCount = maxSets }
        } };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = maxSets,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()), .pPoolSizes = poolSizes.data() };

        m_viewportDescriptorPool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

        // One outline-composite set per viewport (binding 0 = its mask image).
        vk::DescriptorPoolSize outlineSize{ .type = vk::DescriptorType::eSampledImage, .descriptorCount = kMaxViewports };
        vk::DescriptorPoolCreateInfo outlinePoolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = kMaxViewports,
            .poolSizeCount = 1, .pPoolSizes = &outlineSize };
        m_outlineDescriptorPool = vk::raii::DescriptorPool(m_context.GetDevice(), outlinePoolInfo);

    }   // ViewportSet::CreateDescriptorPools()

    void ViewportSet::CreateViewportImages(Viewport& vp, uint32_t width, uint32_t height, uint32_t samples) {

        vp.extent = { width, height };

        // Must match the pipeline's baked-in color/depth attachment formats exactly (dynamic
        // rendering requires this), so reuse whatever the swapchain already settled on.
        const vk::Format colorFormat = m_colourFormat;
        const vk::Format depthFormat = m_depthFormat;

        vp.colorImage.Create(m_context, width, height, colorFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::ImageViewCreateInfo colorViewInfo{
            .image = vp.colorImage.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = colorFormat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 }
        };
        vp.colorView = vk::raii::ImageView(m_context.GetDevice(), colorViewInfo);

        // Transfer source: the scene pass copies it for the water (PostTargets::depthCopy).
        vp.depthImage.Create(m_context, width, height, depthFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::ImageViewCreateInfo depthViewInfo{
            .image = vp.depthImage.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = depthFormat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1 }
        };
        vp.depthView = vk::raii::ImageView(m_context.GetDevice(), depthViewInfo);

        // Selection-outline mask, same size (cheap: one byte per pixel).
        vp.maskImage.Create(m_context, width, height, RenderLayouts::kOutlineMaskFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::ImageViewCreateInfo maskViewInfo{
            .image = vp.maskImage.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = RenderLayouts::kOutlineMaskFormat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 }
        };
        vp.maskView = vk::raii::ImageView(m_context.GetDevice(), maskViewInfo);
        WriteOutlineDescriptor(vp);   // no-op on first creation (the set doesn't exist yet)

        // The HDR image the scene is actually drawn into, and its bloom chain.
        m_targets.Create(vp.post, width, height, samples);

    }   // ViewportSet::CreateViewportImages()

    void ViewportSet::CreateViewportUniformsAndDescriptors(Viewport& vp) {

        uint32_t maxFrames = m_framesInFlight;
        for (uint32_t i = 0; i < maxFrames; ++i) {
            vp.uniformBuffers.emplace_back();
            vp.uniformBuffers.back().Create(m_context, sizeof(FrameUniforms),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            vp.uniformBuffersMapped.emplace_back(vp.uniformBuffers.back().Map());
        }

        std::vector<vk::DescriptorSetLayout> layouts(maxFrames, m_frameSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo{
            .descriptorPool     = m_viewportDescriptorPool,
            .descriptorSetCount = maxFrames,
            .pSetLayouts        = layouts.data()
        };
        vp.descriptorSets = m_context.GetDevice().allocateDescriptorSets(allocInfo);

        for (uint32_t i = 0; i < maxFrames; ++i) {
            vk::DescriptorBufferInfo bufferInfo{ .buffer = vp.uniformBuffers[i].GetBuffer(), .offset = 0, .range = sizeof(FrameUniforms) };
            vk::WriteDescriptorSet   write{
                .dstSet = vp.descriptorSets[i], .dstBinding = 0, .dstArrayElement = 0,
                .descriptorCount = 1, .descriptorType = vk::DescriptorType::eUniformBuffer, .pBufferInfo = &bufferInfo };
            m_context.GetDevice().updateDescriptorSets(write, {});
        }

        // (Bindings 1-7, the per-frame buffers every view shares, are written by the Renderer.)

        // The outline composite's set (binding 0 = this viewport's mask).
        vk::DescriptorSetLayout outlineLayout = m_maskSetLayout;
        vk::DescriptorSetAllocateInfo outlineAlloc{
            .descriptorPool     = m_outlineDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &outlineLayout
        };
        vp.outlineSet = std::move(m_context.GetDevice().allocateDescriptorSets(outlineAlloc).front());
        WriteOutlineDescriptor(vp);

    }   // ViewportSet::CreateViewportUniformsAndDescriptors()

    void ViewportSet::WriteOutlineDescriptor(Viewport& vp) {

        if (!*vp.outlineSet || !*vp.maskView) { return; }
        vk::DescriptorImageInfo imageInfo{ .imageView = *vp.maskView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::WriteDescriptorSet write{
            .dstSet = *vp.outlineSet, .dstBinding = 0, .dstArrayElement = 0,
            .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage, .pImageInfo = &imageInfo };
        m_context.GetDevice().updateDescriptorSets(write, {});

    }   // ViewportSet::WriteOutlineDescriptor()

}   // NAMESPACE CRUCIBLE
