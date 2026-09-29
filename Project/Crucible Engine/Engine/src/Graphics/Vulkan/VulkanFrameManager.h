#pragma once

#include "VulkanContext.h"
#include "VulkanSwapchain.h"

namespace Crucible {

    struct FrameData {

        vk::raii::CommandBuffer commandBuffer = nullptr;
        vk::raii::Semaphore     imageAvailable = nullptr;
        vk::raii::Fence         inFlight = nullptr;
    };

    struct FrameContext {

        FrameData& frame;
        uint32_t imageIndex;
    };

    class VulkanFrameManager {

    public:
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
        
        void                    Init(VulkanContext& context, VulkanSwapchain& swapchain);
        FrameContext            BeginFrame(VulkanContext& context, VulkanSwapchain& swapchain, bool& err);
        void                    EndFrame(VulkanContext& context, VulkanSwapchain& swapchain, FrameContext& fc, WindowManager& window);
        
        const FrameData&        GetCurrentFrame() const;
        uint32_t                GetFrameIndex() const;
        uint32_t                MaxFramesInFlight() const;
        vk::raii::CommandPool&  GetCommandPool();

    private:
        vk::raii::CommandPool   m_commandPool = nullptr;
        std::vector<FrameData>  m_frames;
        uint32_t                m_frameIndex = 0;

        // Separate vector of semaphores for syncing with Swapchain images.
        std::vector<vk::raii::Semaphore> m_presentFinishedSemaphores;

    };  // VULKAN FRAME MANAGER CLASS

}   // NAMESPACE CRUCIBLE