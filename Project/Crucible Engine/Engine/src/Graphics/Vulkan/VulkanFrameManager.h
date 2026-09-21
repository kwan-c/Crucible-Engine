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
        
        void            Init(VulkanContext& context, VulkanSwapchain& swapchain);
        FrameContext    BeginFrame(VulkanContext& context, VulkanSwapchain& swapchain, bool& err);
        void            EndFrame(VulkanContext& context, VulkanSwapchain& swapchain, FrameContext& fc, WindowManager& window);
        
        FrameData&      GetCurrentFrame();
        uint32_t        GetFrameIndex() const;

    private:
        vk::raii::CommandPool   m_commandPool = nullptr;
        std::vector<FrameData>  m_frames;
        uint32_t                m_frameIndex = 0;

        // Separate vector of semaphores for syncing with Swapchain images.
        std::vector<vk::raii::Semaphore> m_presentFinished;

    };  // VULKAN FRAME MANAGER CLASS

}   // NAMESPACE CRUCIBLE