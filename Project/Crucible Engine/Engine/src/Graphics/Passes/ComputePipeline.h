/*************************************************************************
  \file       ComputePipeline.h
  \brief
    Builds a compute pipeline: one compute shader entry point and a
    layout. Shared by the passes that own compute work (the first is the
    volumetric fog), the compute counterpart of FullscreenPipeline.h.
  \details
    Compute work is recorded on the same queue as drawing, outside any
    rendering pass (vkCmdDispatch is not allowed inside one), and is
    ordered against the draws around it with ordinary pipeline barriers
    (Vulkan/VulkanCommands.h's TransitionImageLayout works for these too:
    pass the same layout as old and new to get a plain memory barrier).
*************************************************************************/
#pragma once

#include "Graphics/Vulkan/VulkanContext.h"

namespace Crucible {

    /*********************************************************************
     \brief  Creates one compute pipeline.
     \param[in] context       The VulkanContext to create it through.
     \param[in] shaderModule  The compiled shader holding the entry point.
     \param[in] entry         Name of the compute entry point to run.
     \param[in] layout        The pipeline layout (its sets and push constants).
     \return The pipeline.
     *********************************************************************/
    [[nodiscard]] inline vk::raii::Pipeline CreateComputePipeline(VulkanContext& context, const vk::raii::ShaderModule& shaderModule,
                                                                  const char* entry, vk::PipelineLayout layout) {

        vk::ComputePipelineCreateInfo info{
            .stage  = { .stage = vk::ShaderStageFlagBits::eCompute, .module = *shaderModule, .pName = entry },
            .layout = layout
        };
        return vk::raii::Pipeline(context.GetDevice(), nullptr, info);

    }   // CreateComputePipeline()

    /// Number of workgroups needed to cover `count` items with groups of `groupSize`.
    [[nodiscard]] constexpr uint32_t DispatchGroups(uint32_t count, uint32_t groupSize) { return (count + groupSize - 1) / groupSize; }

}   // NAMESPACE CRUCIBLE
