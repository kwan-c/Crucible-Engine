/*************************************************************************
  \file       VolumetricFogPass.cpp
  \brief
    See VolumetricFogPass.h.
*************************************************************************/
#include "Graphics/Passes/VolumetricFogPass.h"

#include "Graphics/Passes/ComputePipeline.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanCommands.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Vulkan/VulkanUploadQueue.h"

#include <glm/gtc/packing.hpp>   // packHalf1x16 - the "no fog" texel

#include <array>

namespace Crucible {

    namespace {
        constexpr uint32_t kGroupSize = 8;   // VolumetricFog.slang's numthreads(8, 8, 1)
    }

    void VolumetricFogPass::Create(vk::DescriptorSetLayout frameSetLayout, VulkanUploadQueue& uploads, uint32_t maxViews) {

        const vk::raii::Device& device = m_context.GetDevice();

        // Set 1: the view's two volumes, both as storage images (inject writes the first;
        // integrate reads it and writes the second).
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{ {
            { .binding = 0, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute },
            { .binding = 1, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute }
        } };
        m_volumeSetLayout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo{
            .bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data() });

        vk::DescriptorPoolSize poolSize{ .type = vk::DescriptorType::eStorageImage, .descriptorCount = 2 * maxViews };
        m_pool = vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,   // a view's set frees itself when the view goes
            .maxSets = maxViews, .poolSizeCount = 1, .pPoolSizes = &poolSize });

        std::array<vk::DescriptorSetLayout, 2> setLayouts{ frameSetLayout, *m_volumeSetLayout };
        m_layout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo{
            .setLayoutCount = static_cast<uint32_t>(setLayouts.size()), .pSetLayouts = setLayouts.data() });

        vk::raii::ShaderModule module = LoadShaderModule(m_context, "../../../Assets/Shaders/VolumetricFog.spv");
        m_injectPipeline    = CreateComputePipeline(m_context, module, "fogInject", *m_layout);
        m_integratePipeline = CreateComputePipeline(m_context, module, "fogIntegrate", *m_layout);

        // Shaders read the integrated volume with filtering in all three directions, so the
        // 160 x 90 x 128 grid shades smoothly across pixels and depth.
        m_sampler = vk::raii::Sampler(device, vk::SamplerCreateInfo{
            .magFilter    = vk::Filter::eLinear,
            .minFilter    = vk::Filter::eLinear,
            .mipmapMode   = vk::SamplerMipmapMode::eNearest,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .maxLod       = 0.0f });

        // "No fog": one texel, no light added (rgb 0), everything behind shows (a 1). Bound for
        // views without a volume of their own - their shaders then use the analytic fog, but the
        // binding must still be valid.
        m_emptyImage.Create3D(m_context, 1, 1, 1, kFormat,
                              vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                              vk::MemoryPropertyFlagBits::eDeviceLocal);
        const uint16_t zero = glm::packHalf1x16(0.0f), one = glm::packHalf1x16(1.0f);
        const std::array<uint16_t, 4> texel{ zero, zero, zero, one };
        uploads.QueueImage(*m_emptyImage.GetImage(), texel.data(), sizeof(texel), 1, 1);   // -> eShaderReadOnlyOptimal
        m_emptyView = vk::raii::ImageView(device, vk::ImageViewCreateInfo{
            .image = *m_emptyImage.GetImage(), .viewType = vk::ImageViewType::e3D, .format = kFormat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 } });

    }   // VolumetricFogPass::Create()

    void VolumetricFogPass::EnsureTargets(FogVolumeTargets& targets) {

        if (targets.IsCreated()) { return; }

        const vk::raii::Device& device = m_context.GetDevice();
        const auto makeVolume = [&](VulkanImage& image, vk::raii::ImageView& view, uint32_t depth, vk::ImageUsageFlags usage) {
            image.Create3D(m_context, kWidth, kHeight, depth, kFormat, usage, vk::MemoryPropertyFlagBits::eDeviceLocal);
            view = vk::raii::ImageView(device, vk::ImageViewCreateInfo{
                .image = *image.GetImage(), .viewType = vk::ImageViewType::e3D, .format = kFormat,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 } });
        };
        makeVolume(targets.lightImage, targets.lightView, kSlices, vk::ImageUsageFlagBits::eStorage);
        // One slice more: past the last, integrate stores each column's unshadowed light (the
        // light of the fog beyond the volume and of the sky's fog; Common.slang's FogColumnSource).
        makeVolume(targets.integratedImage, targets.integratedView, kSlices + 1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
        targets.initialised = false;

        vk::DescriptorSetLayout layout = *m_volumeSetLayout;
        targets.set = std::move(device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{
            .descriptorPool = *m_pool, .descriptorSetCount = 1, .pSetLayouts = &layout }).front());

        vk::DescriptorImageInfo lightInfo{ .imageView = *targets.lightView, .imageLayout = vk::ImageLayout::eGeneral };
        vk::DescriptorImageInfo integratedInfo{ .imageView = *targets.integratedView, .imageLayout = vk::ImageLayout::eGeneral };
        const std::array<vk::WriteDescriptorSet, 2> writes{ {
            { .dstSet = *targets.set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &lightInfo },
            { .dstSet = *targets.set, .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &integratedInfo }
        } };
        device.updateDescriptorSets(writes, {});

    }   // VolumetricFogPass::EnsureTargets()

    void VolumetricFogPass::Record(vk::CommandBuffer cmd, FogVolumeTargets& targets, vk::DescriptorSet frameSet) const {

        if (!targets.IsCreated()) { return; }

        // Both volumes stay in eGeneral (storage writes and sampled reads both allow it), so these
        // barriers only order work. Wait for their last users - the previous frame's integrate
        // (compute) and the scene's vertex and fragment shaders that sampled the result - before
        // writing them again. (Barriers cover all earlier submissions on the queue too.)
        const vk::ImageLayout oldLayout = targets.initialised ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined;
        TransitionImageLayout(cmd, *targets.lightImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            oldLayout, vk::ImageLayout::eGeneral,
            vk::AccessFlagBits2::eShaderStorageWrite, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader, vk::PipelineStageFlagBits2::eComputeShader);
        TransitionImageLayout(cmd, *targets.integratedImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            oldLayout, vk::ImageLayout::eGeneral,
            vk::AccessFlagBits2::eShaderStorageWrite, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eComputeShader);
        targets.initialised = true;

        const std::array<vk::DescriptorSet, 2> sets{ frameSet, *targets.set };
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_layout, 0, sets, nullptr);

        // 1. Light every froxel.
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *m_injectPipeline);
        cmd.dispatch(DispatchGroups(kWidth, kGroupSize), DispatchGroups(kHeight, kGroupSize), kSlices);

        TransitionImageLayout(cmd, *targets.lightImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
            vk::AccessFlagBits2::eShaderStorageWrite, vk::AccessFlagBits2::eShaderStorageRead,
            vk::PipelineStageFlagBits2::eComputeShader, vk::PipelineStageFlagBits2::eComputeShader);

        // 2. Add up each column from the camera outward.
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *m_integratePipeline);
        cmd.dispatch(DispatchGroups(kWidth, kGroupSize), DispatchGroups(kHeight, kGroupSize), 1);

        // 3. Ready for the scene: meshes and the sky sample it per pixel, particles per vertex.
        TransitionImageLayout(cmd, *targets.integratedImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
            vk::AccessFlagBits2::eShaderStorageWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eComputeShader, vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader);

    }   // VolumetricFogPass::Record()

    vk::DescriptorImageInfo VolumetricFogPass::SampledVolume(const FogVolumeTargets* targets) const {

        // A view's own volume is bound as soon as it exists: this is written while the frame is being
        // prepared, and the volume is filled (Record) earlier in the same command buffer than any
        // shader reads it.
        if (targets && targets->IsCreated()) {
            return { .sampler = *m_sampler, .imageView = *targets->integratedView, .imageLayout = vk::ImageLayout::eGeneral };
        }
        return { .sampler = *m_sampler, .imageView = *m_emptyView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

    }   // VolumetricFogPass::SampledVolume()

}   // NAMESPACE CRUCIBLE
