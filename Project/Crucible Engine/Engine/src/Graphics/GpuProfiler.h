/*************************************************************************
  \file       GpuProfiler.h
  \brief
    Measures how long the GPU spends on each part of a frame (shadows,
    occlusion, scene, bloom, ...), for the timings overlay.
  \details
    The Renderer writes a mark into the command buffer at the start of each
    part. A mark is a GPU timestamp; the time between two marks belongs to
    the part the earlier one started. Results are read back a few frames
    later, once the GPU has finished that frame. Moved out of the Renderer.
*************************************************************************/
#pragma once

#include "Vulkan/VulkanContext.h"
#include "RenderTypes.h"

#include <vector>

namespace Crucible {

    class GpuProfiler {
    public:
        /*********************************************************************
         \brief  Creates one block of timestamp queries per frame in flight.
                 Does nothing on a GPU without timestamps: marks are then
                 ignored and no timings are reported.
         \param[in] context         Must outlive this object.
         \param[in] framesInFlight  How many frames can be in progress at once.
         *********************************************************************/
        void Init(VulkanContext& context, uint32_t framesInFlight);

        /// Clears this frame's block of timestamps. Call first thing in the frame's command buffer.
        void BeginFrame(vk::CommandBuffer cmd, uint32_t frameIndex);
        /// Writes a GPU timestamp: everything recorded from here to the next mark counts as `section`.
        void Mark(vk::CommandBuffer cmd, GpuSection section);
        /// Reads back the timestamps of the frame that last used this frame index and blends
        /// them into stats.gpuMs / gpuTotalMs. Leaves `stats` alone if they are not ready.
        void Read(uint32_t frameIndex, RenderFrameStats& stats) const;

    private:
        static constexpr uint32_t kMaxGpuMarks = 128;   // per frame

        VulkanContext*                       m_context = nullptr;
        vk::raii::QueryPool                  m_timestampPool = nullptr;
        std::vector<std::vector<GpuSection>> m_gpuMarks;             // which section each timestamp of a frame started
        float                                m_timestampPeriodNs = 0.0f;
        uint32_t                             m_markFrame = 0;        // frame index being recorded
    };

}   // NAMESPACE CRUCIBLE
