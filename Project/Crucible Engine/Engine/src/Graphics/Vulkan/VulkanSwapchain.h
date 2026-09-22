#pragma once

// Crucible Engine
#include "VulkanContext.h"
#include "Systems/WindowManager.h"

namespace Crucible {

    class VulkanSwapchain {

    public:
        VulkanSwapchain(VulkanContext& context, WindowManager& window);
        ~VulkanSwapchain();

        void Recreate(VulkanContext& context);
        void CleanUp();

        const vk::raii::SwapchainKHR&           GetSwapchain() const;
        const std::vector<vk::Image>&           GetImages() const;
        const std::vector<vk::raii::ImageView>& GetImageViews() const;

        vk::Extent2D                            GetExtent() const;
        vk::SurfaceFormatKHR                    GetSurfaceFormat() const;
        VkFormat                                GetColorAttachmentFormat() const;

    private:
        void CreateSwapchain(VulkanContext& context);
        void CreateImageViews(VulkanContext& context);

        vk::SurfaceFormatKHR    ChooseSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& formats);
        vk::PresentModeKHR      ChoosePresentMode(std::vector<vk::PresentModeKHR> const& modes);
        vk::Extent2D            ChooseExtent(vk::SurfaceCapabilitiesKHR const& capabilities);

        uint32_t                ChooseMinImageCount(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities);
    
    private:
        WindowManager&                      m_window;

        vk::raii::SwapchainKHR              m_swapchain = nullptr;
        std::vector<vk::Image>              m_images;
        std::vector<vk::raii::ImageView>    m_imageViews;

        vk::Extent2D                        m_extent{};
        vk::SurfaceFormatKHR                m_surfaceFormat{};
        VkFormat                            m_colorAttachmentFormat = VK_FORMAT_UNDEFINED;

    };  // VULKAN SWAPCHAIN CLASS

}   // NAMESPACE CRUCIBLE