#include "VulkanPipeline.h"

namespace Crucible {

    // ------------------------------------------------------------------------------------------------------------- //
    // ACCESSORS
    // ------------------------------------------------------------------------------------------------------------- //
    
    const vk::raii::Pipeline&       VulkanPipeline::GetPipeline()   const { return m_graphicsPipeline; }
    const vk::raii::PipelineLayout& VulkanPipeline::GetLayout()     const { return m_pipelineLayout; }

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN PIPELINE
    // ------------------------------------------------------------------------------------------------------------- //

    void VulkanPipeline::Init(VulkanContext& context, VulkanSwapchain& swapchain) {



    }   // VulkanPipeline::Init()

    void VulkanPipeline::CleanUp() {

    }   // VulkanPipeline::CleanUp()

}   // NAMESPACE CRUCIBLE