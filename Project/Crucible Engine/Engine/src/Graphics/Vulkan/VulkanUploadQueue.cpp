/*************************************************************************
* \file:         VulkanUploadQueue.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          05 October 2026
* \version       1.0
* \brief
*                See VulkanUploadQueue.h for what this class does and why.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "VulkanUploadQueue.h"

#include <cassert>
#include <cstring>      // memcpy
#include <utility>      // move

namespace Crucible {

    // ------------------------------------------------------------------------------------------------------------- //
    // ACCESSORS
    // ------------------------------------------------------------------------------------------------------------- //

    bool            VulkanUploadQueue::HasPending()   const { return !m_bufferUploads.empty() || !m_imageUploads.empty(); }
    vk::DeviceSize  VulkanUploadQueue::PendingBytes() const { return m_pendingBytes; }

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN UPLOAD QUEUE
    // ------------------------------------------------------------------------------------------------------------- //

    void VulkanUploadQueue::Init(VulkanContext& context, vk::raii::CommandPool& commandPool) {

        m_context     = &context;
        m_commandPool = &commandPool;

    }   // VulkanUploadQueue::Init()

    void VulkanUploadQueue::QueueBuffer(vk::Buffer destination, const void* data, vk::DeviceSize size) {

        if (size == 0) { return; }

        m_bufferUploads.push_back(BufferUpload{ MakeStagingBuffer(data, size), destination, size });
        m_pendingBytes += size;
        FlushIfOverBudget();

    }   // VulkanUploadQueue::QueueBuffer()

    void VulkanUploadQueue::QueueImage(vk::Image destination, const void* data, vk::DeviceSize size,
                                       uint32_t width, uint32_t height, uint32_t layerCount, uint32_t mipLevel) {

        if (size == 0) { return; }

        m_imageUploads.push_back(ImageUpload{ MakeStagingBuffer(data, size), destination, width, height, layerCount, mipLevel });
        m_pendingBytes += size;
        FlushIfOverBudget();

    }   // VulkanUploadQueue::QueueImage()

    void VulkanUploadQueue::Record(vk::CommandBuffer cmd, uint32_t frameIndex) {

        assert(frameIndex < m_inFlight.size());

        if (!HasPending()) { return; }
        RecordCopies(cmd, m_inFlight[frameIndex]);

    }   // VulkanUploadQueue::Record()

    void VulkanUploadQueue::ReleaseFrame(uint32_t frameIndex) {

        assert(frameIndex < m_inFlight.size());

        // Each VulkanBuffer frees its vk::Buffer and its VMA allocation as it is destroyed.
        m_inFlight[frameIndex].clear();

    }   // VulkanUploadQueue::ReleaseFrame()

    void VulkanUploadQueue::FlushNow() {

        if (!HasPending()) { return; }

        vk::CommandBufferAllocateInfo allocInfo{
            .commandPool = **m_commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1
        };
        vk::raii::CommandBuffer cmd = std::move(m_context->GetDevice().allocateCommandBuffers(allocInfo).front());

        std::vector<VulkanBuffer> staging;

        cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        RecordCopies(*cmd, staging);
        cmd.end();

        m_context->GetGraphicsQueue().submit(vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &*cmd }, nullptr);
        m_context->GetGraphicsQueue().waitIdle();

        // The copies have finished (wait-idle above), so `staging` can be freed as it goes out
        // of scope here; nothing has to be parked under a frame index.

    }   // VulkanUploadQueue::FlushNow()

    // ------------------------------------------------------------------------------------------------------------- //
    // HELPER FUNCTIONS
    // ------------------------------------------------------------------------------------------------------------- //

    VulkanBuffer VulkanUploadQueue::MakeStagingBuffer(const void* data, vk::DeviceSize size) {

        assert(m_context != nullptr && "VulkanUploadQueue used before Init()");
        assert(data != nullptr);

        VulkanBuffer staging;
        staging.Create(*m_context, size, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        // Host-visible allocations are persistently mapped (see VulkanResource::Map()).
        std::memcpy(staging.Map(), data, static_cast<size_t>(size));
        staging.Unmap();

        return staging;

    }   // VulkanUploadQueue::MakeStagingBuffer()

    void VulkanUploadQueue::FlushIfOverBudget() {

        if (m_pendingBytes > kMaxPendingBytes) { FlushNow(); }

    }   // VulkanUploadQueue::FlushIfOverBudget()

    void VulkanUploadQueue::RecordCopies(vk::CommandBuffer cmd, std::vector<VulkanBuffer>& keepAlive) {

        keepAlive.reserve(keepAlive.size() + m_bufferUploads.size() + m_imageUploads.size());

        // BUFFERS ------------------------------------------------------------------------------ //

        if (!m_bufferUploads.empty()) {

            for (BufferUpload& upload : m_bufferUploads) {
                cmd.copyBuffer(*upload.staging.GetBuffer(), upload.destination,
                    vk::BufferCopy{ .srcOffset = 0, .dstOffset = 0, .size = upload.size });
                keepAlive.push_back(std::move(upload.staging));
            }

            // Recording order alone does not make a later command wait for these writes: Vulkan
            // needs the dependency stated. One barrier covers every buffer copied above, and any
            // way the destinations are read afterwards (vertex input, index input, ...).
            const vk::MemoryBarrier2 barrier{
                .srcStageMask  = vk::PipelineStageFlagBits2::eCopy,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask = vk::AccessFlagBits2::eMemoryRead
            };
            cmd.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &barrier });
        }

        // IMAGES ------------------------------------------------------------------------------- //

        if (!m_imageUploads.empty()) {

            // 1. Every destination: eUndefined -> eTransferDstOptimal, in one barrier call.
            std::vector<vk::ImageMemoryBarrier2> barriers;
            barriers.reserve(m_imageUploads.size());

            for (const ImageUpload& upload : m_imageUploads) {
                barriers.push_back(vk::ImageMemoryBarrier2{
                    .srcStageMask        = vk::PipelineStageFlagBits2::eNone,
                    .srcAccessMask       = {},
                    .dstStageMask        = vk::PipelineStageFlagBits2::eCopy,
                    .dstAccessMask       = vk::AccessFlagBits2::eTransferWrite,
                    .oldLayout           = vk::ImageLayout::eUndefined,
                    .newLayout           = vk::ImageLayout::eTransferDstOptimal,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = upload.destination,
                    .subresourceRange    = {
                        .aspectMask     = vk::ImageAspectFlagBits::eColor,
                        .baseMipLevel   = upload.mipLevel,
                        .levelCount     = 1,
                        .baseArrayLayer = 0,
                        .layerCount     = upload.layerCount }
                });
            }
            cmd.pipelineBarrier2(vk::DependencyInfo{
                .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                .pImageMemoryBarriers    = barriers.data() });

            // 2. The copies. With bufferRowLength/bufferImageHeight 0 (tightly packed), a
            //    multi-layer copy reads each layer from the buffer immediately after the
            //    previous one - exactly how the six faces of a cubemap are laid out.
            for (ImageUpload& upload : m_imageUploads) {
                const vk::BufferImageCopy region{
                    .bufferOffset      = 0,
                    .bufferRowLength   = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource  = {
                        .aspectMask     = vk::ImageAspectFlagBits::eColor,
                        .mipLevel       = upload.mipLevel,
                        .baseArrayLayer = 0,
                        .layerCount     = upload.layerCount },
                    .imageOffset       = { 0, 0, 0 },
                    .imageExtent       = { upload.width, upload.height, 1 }
                };
                cmd.copyBufferToImage(*upload.staging.GetBuffer(), upload.destination,
                    vk::ImageLayout::eTransferDstOptimal, region);
                keepAlive.push_back(std::move(upload.staging));
            }

            // 3. Every destination: eTransferDstOptimal -> eShaderReadOnlyOptimal, the layout
            //    every sampled-image descriptor in the renderer declares.
            for (vk::ImageMemoryBarrier2& barrier : barriers) {
                barrier.srcStageMask  = vk::PipelineStageFlagBits2::eCopy;
                barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
                barrier.dstStageMask  = vk::PipelineStageFlagBits2::eFragmentShader;
                barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
                barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
                barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
            }
            cmd.pipelineBarrier2(vk::DependencyInfo{
                .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                .pImageMemoryBarriers    = barriers.data() });
        }

        m_bufferUploads.clear();
        m_imageUploads.clear();
        m_pendingBytes = 0;

    }   // VulkanUploadQueue::RecordCopies()

}   // NAMESPACE CRUCIBLE
