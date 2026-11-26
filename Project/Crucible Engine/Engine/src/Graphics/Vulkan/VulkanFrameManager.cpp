/*************************************************************************
* \file:         VulkanFrameManager.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                See VulkanFrameManager.h for what this class owns and why.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "VulkanFrameManager.h"

namespace Crucible {

    // ------------------------------------------------------------------------------------------------------------- //
    // ACCESSORS
    // ------------------------------------------------------------------------------------------------------------- //
    
    const FrameData&        VulkanFrameManager::GetCurrentFrame()   const { return m_frames[m_frameIndex]; }
    uint32_t                VulkanFrameManager::GetFrameIndex()     const { return m_frameIndex; }
    uint32_t                VulkanFrameManager::MaxFramesInFlight() const { return MAX_FRAMES_IN_FLIGHT; }
    vk::raii::CommandPool&  VulkanFrameManager::GetCommandPool()          { return m_commandPool; }

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN FRAME MANAGER
    // ------------------------------------------------------------------------------------------------------------- //
    
    void VulkanFrameManager::Init(VulkanContext& context, VulkanSwapchain& swapchain) {

        // Create command pool
        vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = context.GetGraphicsQueueFamily()
        };
        m_commandPool = vk::raii::CommandPool(context.GetDevice(), poolInfo);

        // Create command buffers
        vk::CommandBufferAllocateInfo allocInfo{
            .commandPool        = m_commandPool,
            .level              = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT
        };
        std::vector<vk::raii::CommandBuffer> commandBuffers = context.GetDevice().allocateCommandBuffers(allocInfo);

        // Create sync objects and insert command buffers into m_frames
        m_frames.resize(MAX_FRAMES_IN_FLIGHT);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {

            m_frames[i].commandBuffer   = std::move(commandBuffers[i]);
            m_frames[i].imageAvailable  = vk::raii::Semaphore(context.GetDevice(), vk::SemaphoreCreateInfo());
            m_frames[i].inFlight        = vk::raii::Fence(context.GetDevice(), vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
        }

        for (uint32_t i = 0; i < swapchain.GetImages().size(); ++i) {
            m_presentFinishedSemaphores.emplace_back(context.GetDevice(), vk::SemaphoreCreateInfo());
        }

    }   // VulkanFrameManager::Init()

    FrameContext VulkanFrameManager::BeginFrame(VulkanContext& context, VulkanSwapchain& swapchain, bool& err) {

        FrameData& frame = m_frames[m_frameIndex];
        auto fenceResult = context.GetDevice().waitForFences(*frame.inFlight, vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("[Renderer | FrameManager] Failed to wait for fence.");
        }

        // Obtain image from swap chain.
        auto [result, imageIndex] = swapchain.GetSwapchain().acquireNextImage(UINT64_MAX, *frame.imageAvailable, nullptr);
        
        // Handle out-of-date or suboptimal swap chains.
        if (result == vk::Result::eErrorOutOfDateKHR) {
            swapchain.Recreate(context);
            err = true;
            return { frame, imageIndex };
        }
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
            assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
            throw std::runtime_error("[Renderer | FrameManager] Failed to acquire swap chain image.");
        }

        context.GetDevice().resetFences(*frame.inFlight);
        frame.commandBuffer.reset();
        err = false;

        return { frame, imageIndex };

    }   // VulkanFrameManager::BeginFrame()

    void VulkanFrameManager::EndFrame(VulkanContext& context, VulkanSwapchain& swapchain, FrameContext& fc, WindowManager& window) {

        vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);

        const vk::SubmitInfo submitInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*fc.frame.imageAvailable,
            .pWaitDstStageMask = &waitDestinationStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &*fc.frame.commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*m_presentFinishedSemaphores[fc.imageIndex]
        };
        context.GetGraphicsQueue().submit(submitInfo, *fc.frame.inFlight);

        // Submit result back to swap chain.
        const vk::PresentInfoKHR presentInfoKHR{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*m_presentFinishedSemaphores[fc.imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &*swapchain.GetSwapchain(),
            .pImageIndices = &fc.imageIndex
        };
        auto result = context.GetGraphicsQueue().presentKHR(presentInfoKHR);
        if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR) || window.m_framebufferResized) {
            window.m_framebufferResized = false;
            swapchain.Recreate(context);
            // A new present mode (VSync) or surface can come with more images; each needs
            // its own present semaphore. Extra ones from before are harmless.
            while (m_presentFinishedSemaphores.size() < swapchain.GetImages().size()) {
                m_presentFinishedSemaphores.emplace_back(context.GetDevice(), vk::SemaphoreCreateInfo());
            }
        }
        else {
            // There are no other success codes than eSuccess; on any error code, presentKHR already threw an exception.
            assert(result == vk::Result::eSuccess);
        }

        // Move on to the next frame-in-flight slot. Without this the index stayed 0 forever: only
        // one frame was ever in flight, and BeginFrame() always waited for the previous frame's
        // GPU work to finish before the CPU could record the next one. Every per-frame resource
        // (uniform, light and debug buffers, descriptor sets, command buffers) is indexed by this,
        // and BeginFrame() waits on the slot's own fence before any of them is rewritten.
        m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;

    }   // VulkanFrameManager::EndFrame()


}   // NAMESPACE CRUCIBLE