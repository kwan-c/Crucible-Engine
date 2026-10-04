#pragma once
/*************************************************************************
* \file:         VulkanUploadQueue.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          05 October 2026
* \version       1.0
* \brief
*                Queues CPU -> GPU copies (mesh buffers, textures,
*                cubemaps) and records them into a frame's own command
*                buffer, instead of submitting and waiting on each one.
*
* \details
*                Declares VulkanUploadQueue. Filling a device-local buffer
*                or image always goes through a host-visible staging
*                buffer plus a GPU-side copy. Doing that copy in its own
*                submit followed by a wait-idle stalls the CPU until every
*                frame in flight has finished, once per upload. This class
*                removes the wait: the copy is recorded at the start of the
*                next frame's command buffer (so it runs before any draw
*                that reads the destination), and the staging buffer is
*                kept alive until that frame slot's fence has signalled.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "VulkanContext.h"
#include "VulkanFrameManager.h"
#include "VulkanResource.h"

#include <array>
#include <vector>

namespace Crucible {

    /*************************************************************************
     \brief
        Collects pending staging-buffer uploads and records them into a
        command buffer in one batch.

     \details
        Lifetime of one upload:
          1. QueueBuffer()/QueueImage() copies the caller's bytes into a new
             staging buffer and remembers where they have to go. The caller's
             pointer is not kept, so it may be freed straight after.
          2. Record() (called once per frame, before any rendering) records
             the GPU-side copies into the frame's command buffer and parks
             the staging buffers under that frame-in-flight index.
          3. ReleaseFrame() (called once BeginFrame() has waited on that
             index's fence, i.e. the GPU has finished the frame that used
             them) frees those staging buffers.

        Destinations are held as plain handles, not references, so the
        owning GpuMesh/GpuTexture is free to move (vector growth). There is
        no way to cancel an upload: a destination must not be destroyed
        while its upload is still pending.

        Single-threaded, like the rest of the renderer.
     *************************************************************************/
    class VulkanUploadQueue {

    public:
        /*********************************************************************
         \brief  Stores the context and command pool later calls go through.
                 Must be called before anything is queued.
         \param[in] context     The VulkanContext to create staging buffers
                                 and submit through.
         \param[in] commandPool Pool the one-shot command buffer of
                                 FlushNow() is allocated from.
         *********************************************************************/
        void Init(VulkanContext& context, vk::raii::CommandPool& commandPool);

        /*********************************************************************
         \brief
            Queues a copy of `size` bytes into the start of a device-local
            buffer.

         \param[in] destination The buffer to fill. Must have been created
                                 with eTransferDst usage.
         \param[in] data        Source bytes; copied before this returns.
         \param[in] size        Number of bytes to copy.
         *********************************************************************/
        void QueueBuffer(vk::Buffer destination, const void* data, vk::DeviceSize size);

        /*********************************************************************
         \brief
            Queues a copy of tightly packed texels into mip level 0 of a
            device-local color image, which ends up in
            eShaderReadOnlyOptimal layout.

         \param[in] destination The image to fill. Must have been created
                                 with eTransferDst usage and be in
                                 eUndefined layout (i.e. freshly created).
         \param[in] data        Source texels; copied before this returns.
                                 With several layers, each layer follows the
                                 previous one directly.
         \param[in] size        Total number of bytes, all layers included.
         \param[in] width       Image width, in texels.
         \param[in] height      Image height, in texels.
         \param[in] layerCount  Array layers to fill (6 for a cubemap).
         *********************************************************************/
        void QueueImage(vk::Image destination, const void* data, vk::DeviceSize size,
                        uint32_t width, uint32_t height, uint32_t layerCount = 1);

        /*********************************************************************
         \brief
            Records every queued copy into `cmd` and keeps the staging
            buffers alive under `frameIndex`. Does nothing when the queue is
            empty.

         \param[in] cmd        A command buffer in the recording state, with
                                no rendering pass open.
         \param[in] frameIndex The frame-in-flight index `cmd` belongs to.

         \details
            Must run before any command in `cmd` that reads a destination.
            Commands execute in recording order and the barriers recorded
            here make later reads wait for the copies, so everything queued
            before this call is usable by the rest of the same frame.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, uint32_t frameIndex);

        /*********************************************************************
         \brief  Frees the staging buffers Record() parked under this index
                 the last time it was used.
         \param[in] frameIndex The frame-in-flight index whose fence
                                BeginFrame() has just waited on.
         *********************************************************************/
        void ReleaseFrame(uint32_t frameIndex);

        /*********************************************************************
         \brief
            Blocking fallback: submits every queued copy in its own command
            buffer, waits for the queue to go idle and frees the staging
            buffers.

         \details
            Called automatically when more than kMaxPendingBytes of staging
            data is waiting, so loading a large scene before the first frame
            (or all in one frame) cannot hold an unbounded amount of staging
            memory. Not needed otherwise.
         *********************************************************************/
        void FlushNow();

        /*********************************************************************
         \brief  Accessor.
         \return True if at least one upload is waiting to be recorded.
         *********************************************************************/
        bool HasPending() const;

        /*********************************************************************
         \brief  Accessor.
         \return Total staging bytes waiting to be recorded.
         *********************************************************************/
        vk::DeviceSize PendingBytes() const;

        // Staging data allowed to wait for the next Record() before FlushNow() steps in.
        static constexpr vk::DeviceSize kMaxPendingBytes = 256ull * 1024ull * 1024ull;

    private:
        struct BufferUpload {
            VulkanBuffer   staging;
            vk::Buffer     destination;
            vk::DeviceSize size = 0;
        };

        struct ImageUpload {
            VulkanBuffer staging;
            vk::Image    destination;
            uint32_t     width = 0;
            uint32_t     height = 0;
            uint32_t     layerCount = 1;
        };

        // Creates a host-visible staging buffer holding a copy of `data`.
        VulkanBuffer MakeStagingBuffer(const void* data, vk::DeviceSize size);

        // Records all queued copies into `cmd`, moves their staging buffers into `keepAlive`
        // and empties the queue. Shared by Record() and FlushNow().
        void RecordCopies(vk::CommandBuffer cmd, std::vector<VulkanBuffer>& keepAlive);

        void FlushIfOverBudget();

    private:
        VulkanContext*            m_context = nullptr;       // non-owning, set by Init()
        vk::raii::CommandPool*    m_commandPool = nullptr;   // non-owning, set by Init()

        std::vector<BufferUpload> m_bufferUploads;
        std::vector<ImageUpload>  m_imageUploads;
        vk::DeviceSize            m_pendingBytes = 0;

        // Staging buffers whose copies have been recorded, per frame-in-flight index. The GPU
        // may still be reading them until that index's fence signals - see ReleaseFrame().
        std::array<std::vector<VulkanBuffer>, VulkanFrameManager::MAX_FRAMES_IN_FLIGHT> m_inFlight;

    };  // VULKAN UPLOAD QUEUE CLASS

}   // NAMESPACE CRUCIBLE
