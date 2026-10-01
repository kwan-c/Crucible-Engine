#pragma once
/*************************************************************************
* \file:         VulkanSwapchain.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Owns the swapchain, its color image views, and the shared
*                depth buffer.
*
* \details
*                Declares VulkanSwapchain: swapchain creation/recreation
*                (window resize), color image view creation, and the one
*                depth image shared across all frames in flight.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

// Crucible Engine
#include "VulkanContext.h"
#include "VulkanResource.h"
#include "Systems/WindowManager.h"

namespace Crucible {

    /*************************************************************************
     \brief
        Owns the swapchain, its per-image color views, and the shared
        depth buffer.
     *************************************************************************/
    class VulkanSwapchain {

    public:
        /*********************************************************************
         \brief  Creates the swapchain, its image views, and the depth
                 resources.
         \param[in] context The VulkanContext to create resources through.
         \param[in] window  The window to query surface capabilities from.
         *********************************************************************/
        VulkanSwapchain(VulkanContext& context, WindowManager& window);

        /*********************************************************************
         \brief  Tears down the swapchain and its dependent resources (see
                 CleanUp()).
         *********************************************************************/
        ~VulkanSwapchain();

        /*********************************************************************
         \brief  Waits out a minimized window, waits for the device to go
                 idle, tears down the current swapchain, and recreates it
                 (and its image views/depth resources) at the new size.
         \param[in] context The VulkanContext to recreate resources
                             through.
         *********************************************************************/
        void Recreate(VulkanContext& context);

        /*********************************************************************
         \brief  Releases the depth image view, color image views, and the
                 swapchain itself.
         *********************************************************************/
        void CleanUp();

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the owned swapchain.
         *********************************************************************/
        const vk::raii::SwapchainKHR&           GetSwapchain() const;

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the swapchain's color images.
         *********************************************************************/
        const std::vector<vk::Image>&           GetImages() const;

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the swapchain's color image views.
         *********************************************************************/
        const std::vector<vk::raii::ImageView>& GetImageViews() const;

        /*********************************************************************
         \brief  Accessor.
         \return The current swapchain extent.
         *********************************************************************/
        vk::Extent2D                            GetExtent() const;

        /*********************************************************************
         \brief  Accessor.
         \return The chosen surface format.
         *********************************************************************/
        vk::SurfaceFormatKHR                    GetSurfaceFormat() const;

        /*********************************************************************
         \brief  Accessor.
         \return The color attachment format, as a raw VkFormat (for
                 ImGui's C-style Vulkan backend - see ImGuiContext.h).
         *********************************************************************/
        VkFormat                                GetColorAttachmentFormat() const;

        /*********************************************************************
         \brief
            Accessor for the shared depth buffer.

         \return
            The depth image.

         \details
            One depth image is shared across all frames in flight: since
            each frame fully finishes (or is fenced) before its commands
            are reused, there is no need for a depth image per frame in
            flight the way there is for the swapchain's color images.
         *********************************************************************/
        vk::Image                               GetDepthImage() const;

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the shared depth image view.
         *********************************************************************/
        const vk::raii::ImageView&              GetDepthImageView() const;

        /*********************************************************************
         \brief  Accessor.
         \return The chosen depth format.
         *********************************************************************/
        vk::Format                              GetDepthFormat() const;

    private:
        void CreateSwapchain(VulkanContext& context);
        void CreateImageViews(VulkanContext& context);
        void CreateDepthResources(VulkanContext& context);

        vk::SurfaceFormatKHR    ChooseSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& formats);
        vk::PresentModeKHR      ChoosePresentMode(std::vector<vk::PresentModeKHR> const& modes);
        vk::Extent2D            ChooseExtent(vk::SurfaceCapabilitiesKHR const& capabilities);
        vk::Format              ChooseDepthFormat(VulkanContext& context);

        uint32_t                ChooseMinImageCount(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities);
    
    private:
        WindowManager&                      m_window;

        vk::raii::SwapchainKHR              m_swapchain = nullptr;
        std::vector<vk::Image>              m_images;
        std::vector<vk::raii::ImageView>    m_imageViews;

        vk::Extent2D                        m_extent{};
        vk::SurfaceFormatKHR                m_surfaceFormat{};
        VkFormat                            m_colorAttachmentFormat = VK_FORMAT_UNDEFINED;

        VulkanImage                         m_depthImage;
        vk::raii::ImageView                 m_depthImageView = nullptr;
        vk::Format                          m_depthFormat = vk::Format::eUndefined;

    };  // VULKAN SWAPCHAIN CLASS

}   // NAMESPACE CRUCIBLE
