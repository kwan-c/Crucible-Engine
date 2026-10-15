/*************************************************************************
  \file       VulkanCommands.h
  \brief
    Small command-recording helpers shared by the Renderer and the render
    passes in Graphics/Passes/.
*************************************************************************/
#pragma once

#include "VulkanResource.h"   // brings in vulkan-hpp the way the rest of the engine does

#include <cstdint>

namespace Crucible {

    /*************************************************************************
     \brief
        Records one image layout change (a pipeline barrier) into `cmd`.
     \param[in] cmd            Command buffer, with no rendering pass open.
     \param[in] image          The image.
     \param[in] aspectMask     Colour or depth.
     \param[in] oldLayout      Layout it is in now (eUndefined discards its contents).
     \param[in] newLayout      Layout the following commands need.
     \param[in] srcAccessMask  How it was last used...
     \param[in] dstAccessMask  ...and how it will be used next.
     \param[in] srcStageMask   The pipeline stage of the last use...
     \param[in] dstStageMask   ...and of the next.
     \param[in] layerCount     Array layers covered (from layer 0).
     *************************************************************************/
    inline void TransitionImageLayout(vk::CommandBuffer cmd, vk::Image image, vk::ImageAspectFlags aspectMask,
                                      vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                      vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask,
                                      vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask,
                                      uint32_t layerCount = 1) {

        vk::ImageMemoryBarrier2 barrier = {
            .srcStageMask = srcStageMask,
            .srcAccessMask = srcAccessMask,
            .dstStageMask = dstStageMask,
            .dstAccessMask = dstAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {
                   .aspectMask = aspectMask,
                   .baseMipLevel = 0,
                   .levelCount = 1,
                   .baseArrayLayer = 0,
                   .layerCount = layerCount}
        };
        vk::DependencyInfo dependencyInfo = {
            .dependencyFlags = {},
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };
        cmd.pipelineBarrier2(dependencyInfo);

    }

}   // NAMESPACE CRUCIBLE
