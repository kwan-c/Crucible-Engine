#pragma once
/*************************************************************************
* \file:         VulkanFrameManager.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Owns per-frame-in-flight command buffers/sync objects and
*                drives the acquire/submit/present loop.
*
* \details
*                Declares FrameData (one frame-in-flight's command buffer +
*                sync primitives), FrameContext (a frame's data plus its
*                acquired swapchain image index), and VulkanFrameManager
*                itself, which owns the command pool, the array of
*                FrameData, and the present-side semaphores.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "VulkanContext.h"
#include "VulkanSwapchain.h"

namespace Crucible {

    /*************************************************************************
     \brief
        One frame-in-flight's command buffer and synchronization
        primitives.
     *************************************************************************/
    struct FrameData {

        vk::raii::CommandBuffer commandBuffer = nullptr;
        vk::raii::Semaphore     imageAvailable = nullptr;
        vk::raii::Fence         inFlight = nullptr;
    };

    /*************************************************************************
     \brief
        A frame's FrameData plus the swapchain image index BeginFrame()
        acquired for it.
     *************************************************************************/
    struct FrameContext {

        FrameData& frame;
        uint32_t imageIndex;
    };

    /*************************************************************************
     \brief
        Owns the command pool and per-frame-in-flight command
        buffers/sync objects, and drives the acquire/submit/present loop.
     *************************************************************************/
    class VulkanFrameManager {

    public:
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

        /*********************************************************************
         \brief  Creates the command pool, allocates one command buffer per
                 frame in flight, creates each frame's sync objects, and
                 creates one present-finished semaphore per swapchain
                 image.
         \param[in] context   The VulkanContext to create resources through.
         \param[in] swapchain The swapchain whose image count determines
                               how many present-finished semaphores are
                               created.
         *********************************************************************/
        void                    Init(VulkanContext& context, VulkanSwapchain& swapchain);

        /*********************************************************************
         \brief
            Waits on the current frame's in-flight fence, acquires the
            next swapchain image, and resets the frame's command buffer.

         \param[in] context   The VulkanContext to wait/acquire through.
         \param[in] swapchain The swapchain to acquire an image from.
         \param[out] err      Set true if the swapchain was out of date and
                               had to be recreated (in which case the
                               returned FrameContext should not be used to
                               record/submit a frame).
         \return A FrameContext for the caller to record commands into.
         *********************************************************************/
        FrameContext            BeginFrame(VulkanContext& context, VulkanSwapchain& swapchain, bool& err);

        /*********************************************************************
         \brief  Submits the frame's recorded command buffer and presents
                 the acquired image, recreating the swapchain if it came
                 back suboptimal/out-of-date or the window was resized.
         \param[in] context   The VulkanContext to submit/present through.
         \param[in] swapchain The swapchain to present to.
         \param[in] fc        The FrameContext returned by BeginFrame().
         \param[in] window    The window, for checking a pending resize.
         *********************************************************************/
        void                    EndFrame(VulkanContext& context, VulkanSwapchain& swapchain, FrameContext& fc, WindowManager& window);

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the current frame's FrameData.
         *********************************************************************/
        const FrameData&        GetCurrentFrame() const;

        /*********************************************************************
         \brief  Accessor.
         \return The current frame-in-flight index.
         *********************************************************************/
        uint32_t                GetFrameIndex() const;

        /*********************************************************************
         \brief  Accessor.
         \return MAX_FRAMES_IN_FLIGHT.
         *********************************************************************/
        uint32_t                MaxFramesInFlight() const;

        /*********************************************************************
         \brief  Accessor.
         \return Reference to the owned command pool.
         *********************************************************************/
        vk::raii::CommandPool&  GetCommandPool();

    private:
        vk::raii::CommandPool   m_commandPool = nullptr;
        std::vector<FrameData>  m_frames;
        uint32_t                m_frameIndex = 0;

        // Separate vector of semaphores for syncing with Swapchain images.
        std::vector<vk::raii::Semaphore> m_presentFinishedSemaphores;

    };  // VULKAN FRAME MANAGER CLASS

}   // NAMESPACE CRUCIBLE
