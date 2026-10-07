/*************************************************************************
  \file       DebugLinePass.h
  \brief
    Draws the frame's debug lines (gizmos, bounds, light shapes) over a
    view's finished picture: first the ones hidden by scene geometry, then
    the ones that always show.
  \details
    Collecting the lines and filling the vertex buffer are still the
    Renderer's (UpdateDebugBuffer). Owns its pipelines.
*************************************************************************/
#pragma once

#include "Graphics/RenderTypes.h"

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
         \brief  Draws the lines into the rendering pass that is already open.
         \param[in] cmd           Command buffer, inside the final pass.
         \param[in] extent        Size of the target.
         \param[in] frameSet      The view's set 0 (camera).
         \param[in] vertexBuffer  This frame's line vertices: the depth-tested
                                 ones first, then the always-visible ones.
         \param[in] testedCount   Vertices that are hidden by scene geometry.
         \param[in] onTopCount    Vertices that always show.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, vk::Extent2D extent, vk::DescriptorSet frameSet, vk::Buffer vertexBuffer,
                    uint32_t testedCount, uint32_t onTopCount) const;

    private:
        vk::raii::Pipeline       m_debugLinePipeline = nullptr;        // hidden by scene geometry
        vk::raii::Pipeline       m_debugLineOnTopPipeline = nullptr;   // always visible
        vk::raii::PipelineLayout m_debugLineLayout = nullptr;
    };

}   // NAMESPACE CRUCIBLE
