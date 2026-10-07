/*************************************************************************
  \file       DebugLinePass.h
  \brief
    Draws the frame's debug lines (gizmos, bounds, light shapes) over a
    view's finished picture: first the ones hidden by scene geometry, then
    the ones that always show.
  \details
    Collects the lines from DebugDraw each frame, owns the vertex buffers
    they are copied into, and owns its pipelines.
*************************************************************************/
#pragma once

#include "Graphics/RenderTypes.h"
#include "Graphics/DebugDraw.h"   // DebugVertex
#include "Graphics/Vulkan/VulkanResource.h"

#include <vector>

namespace Crucible {

    class VulkanContext;

    class DebugLinePass {
    public:
        /*********************************************************************
         \brief  Creates the pass's pipelines. Call once, before recording.
 \details Two, differing only in depth state: one depth-tested
                 (LESS_OR_EQUAL), one always on top. Neither writes depth.
                 Layout = set 0 only.
         \param[in] context         The VulkanContext to create them through.
         \param[in] frameSetLayout  Layout of set 0 (the per-frame data).
         \param[in] colourFormat    Format of the image the lines are drawn into.
         \param[in] depthFormat     Format of the depth image they are tested against.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::Format colourFormat, vk::Format depthFormat);

        /*********************************************************************
         \brief  Collects this frame's DebugDraw lines and copies them into
                 this frame's vertex buffer, growing it if needed. Call once
                 a frame, before recording.
         \param[in] context         Used to create or grow the buffer.
         \param[in] frameIndex      The frame in flight being built.
         \param[in] framesInFlight  How many frames can be in progress at once.
         *********************************************************************/
        void Update(VulkanContext& context, uint32_t frameIndex, uint32_t framesInFlight);

        /// True when Update() found at least one line this frame.
        bool HasLines() const { return m_debugTestedCount + m_debugOnTopCount > 0; }

        /*********************************************************************
         \brief  Draws the frame's lines into the rendering pass that is
                 already open: the depth-tested ones, then the always-visible
                 ones. Does nothing when there are none.
         \param[in] cmd         Command buffer, inside the final pass.
         \param[in] extent      Size of the target.
         \param[in] frameSet    The view's set 0 (camera).
         \param[in] frameIndex  The frame in flight being recorded.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, vk::Extent2D extent, vk::DescriptorSet frameSet, uint32_t frameIndex) const;

    private:
        // Debug lines: one host-visible vertex buffer per frame in flight, grown on demand (see
        // Update()). Depth-tested vertices first, then the on-top ones.
        static constexpr uint32_t                   kMaxDebugVertices = 2'000'000;
        std::vector<VulkanBuffer>                   m_debugBuffers;
        std::vector<void*>                          m_debugBuffersMapped;
        std::vector<uint32_t>                       m_debugBufferCapacity;   // in vertices
        std::vector<DebugVertex>                    m_debugTested;           // scratch, reused every frame
        std::vector<DebugVertex>                    m_debugOnTop;
        uint32_t                                    m_debugTestedCount = 0;
        uint32_t                                    m_debugOnTopCount = 0;
        bool                                        m_warnedDebugOverflow = false;

        vk::raii::Pipeline       m_debugLinePipeline = nullptr;        // hidden by scene geometry
        vk::raii::Pipeline       m_debugLineOnTopPipeline = nullptr;   // always visible
        vk::raii::PipelineLayout m_debugLineLayout = nullptr;
    };

}   // NAMESPACE CRUCIBLE
