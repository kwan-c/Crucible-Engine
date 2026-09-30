/*************************************************************************
* \file:         VulkanSwapchain.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                See VulkanSwapchain.h for what this class owns and why.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "VulkanSwapchain.h"

#include <array>

namespace Crucible {

    // ------------------------------------------------------------------------------------------------------------- //
    // ACCESSORS
    // ------------------------------------------------------------------------------------------------------------- //
    
    const vk::raii::SwapchainKHR&           VulkanSwapchain::GetSwapchain()             const { return m_swapchain; }
    const std::vector<vk::Image>&           VulkanSwapchain::GetImages()                const { return m_images; }
    const std::vector<vk::raii::ImageView>& VulkanSwapchain::GetImageViews()            const { return m_imageViews; }

    vk::Extent2D                            VulkanSwapchain::GetExtent()                const { return m_extent; }
    vk::SurfaceFormatKHR                    VulkanSwapchain::GetSurfaceFormat()         const { return m_surfaceFormat; }
    VkFormat                                VulkanSwapchain::GetColorAttachmentFormat() const { return m_colorAttachmentFormat; }

    vk::Image                                VulkanSwapchain::GetDepthImage()             const { return m_depthImage.GetImage(); }
    const vk::raii::ImageView&              VulkanSwapchain::GetDepthImageView()        const { return m_depthImageView; }
    vk::Format                              VulkanSwapchain::GetDepthFormat()           const { return m_depthFormat; }

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN SWAPCHAIN
    // ------------------------------------------------------------------------------------------------------------- //

    VulkanSwapchain::VulkanSwapchain(VulkanContext& context, WindowManager& window) : m_window { window } {

        CreateSwapchain(context);
        CreateImageViews(context);
        CreateDepthResources(context);

    }   // VulkanSwapchain::VulkanSwapchain()

    VulkanSwapchain::~VulkanSwapchain() { CleanUp(); }

    void VulkanSwapchain::Recreate(VulkanContext& context) {

        // Handle minimization.
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window.GetNativeHandle(), &width, &height);
        while ((width == 0 || height == 0) && !glfwWindowShouldClose(m_window.GetNativeHandle())) {
            glfwGetFramebufferSize(m_window.GetNativeHandle(), &width, &height);
            glfwWaitEvents();
        }
        if (glfwWindowShouldClose(m_window.GetNativeHandle())) {
            return;
        }

        context.GetDevice().waitIdle();
        CleanUp();

        CreateSwapchain(context);
        CreateImageViews(context);
        CreateDepthResources(context);

    }   // VulkanSwapchain::Recreate()

    void VulkanSwapchain::CleanUp() {

        m_depthImageView = nullptr;
        m_imageViews.clear();
        m_swapchain = nullptr;

    }   // VulkanSwapchain::CleanUp()

    void VulkanSwapchain::CreateSwapchain(VulkanContext& context) {

        vk::SurfaceCapabilitiesKHR surfaceCapabilities = context.GetPhysicalDevice().getSurfaceCapabilitiesKHR(context.GetSurface());
        m_extent = ChooseExtent(surfaceCapabilities);
        uint32_t minImageCount = ChooseMinImageCount(surfaceCapabilities);

        std::vector<vk::SurfaceFormatKHR> availableFormats = context.GetPhysicalDevice().getSurfaceFormatsKHR(context.GetSurface());
        m_surfaceFormat = ChooseSurfaceFormat(availableFormats);
        m_colorAttachmentFormat = static_cast<VkFormat>(m_surfaceFormat.format);

        std::vector<vk::PresentModeKHR> availablePresentModes = context.GetPhysicalDevice().getSurfacePresentModesKHR(context.GetSurface());

        vk::SwapchainCreateInfoKHR swapChainCreateInfo{
            .surface = context.GetSurface(),
            .minImageCount = minImageCount,
            .imageFormat = m_surfaceFormat.format,
            .imageColorSpace = m_surfaceFormat.colorSpace,
            .imageExtent = m_extent,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = ChoosePresentMode(availablePresentModes),
            .clipped = true
        };

        m_swapchain = vk::raii::SwapchainKHR(context.GetDevice(), swapChainCreateInfo);
        m_images = m_swapchain.getImages();
        
    }   // VulkanSwapchain::CreateSwapchain()

    void VulkanSwapchain::CreateImageViews(VulkanContext& context) {

        assert(m_imageViews.empty());

        vk::ImageViewCreateInfo imageViewCreateInfo{
            .viewType = vk::ImageViewType::e2D,
            .format = m_surfaceFormat.format,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        for (auto& image : m_images) {
            imageViewCreateInfo.image = image;
            m_imageViews.emplace_back(context.GetDevice(), imageViewCreateInfo);
        }

    }   // VulkanSwapchain::CreateImageViews()

    void VulkanSwapchain::CreateDepthResources(VulkanContext& context) {

        m_depthFormat = ChooseDepthFormat(context);

        // One depth image sized to the current extent; recreated alongside the swapchain on resize.
        m_depthImage.Create(context, m_extent.width, m_extent.height,
            m_depthFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        vk::ImageViewCreateInfo viewInfo{
            .image = m_depthImage.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = m_depthFormat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        m_depthImageView = vk::raii::ImageView(context.GetDevice(), viewInfo);

    }   // VulkanSwapchain::CreateDepthResources()

    vk::SurfaceFormatKHR VulkanSwapchain::ChooseSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& formats) {

        assert(!formats.empty());

        const auto formatIt = std::ranges::find_if(formats, [](const auto& format) {
            return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });

        return formatIt != formats.end() ? *formatIt : formats[0];

    }   // VulkanSwapchain::ChooseSurfaceFormat()

    vk::PresentModeKHR VulkanSwapchain::ChoosePresentMode(std::vector<vk::PresentModeKHR> const& modes) {

        // Assert that at least Fifo is present.
        assert(std::ranges::any_of(modes, [](auto presentMode) {
            return presentMode == vk::PresentModeKHR::eFifo; }));

        // Return eMailbox if available, else default to eFifo.
        return std::ranges::any_of(modes, [](const vk::PresentModeKHR value) {
            return vk::PresentModeKHR::eMailbox == value; }) ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;

    }   // VulkanSwapchain::ChoosePresentMode()

    vk::Extent2D VulkanSwapchain::ChooseExtent(vk::SurfaceCapabilitiesKHR const& capabilities) {

        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width{}, height{};
        glfwGetFramebufferSize(m_window.GetNativeHandle(), &width, &height);

        return {
            std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
        };

    }   // VulkanSwapchain::ChooseExtent()

    vk::Format VulkanSwapchain::ChooseDepthFormat(VulkanContext& context) {

        // Preference order: pure 32-bit depth first (no wasted stencil bits), then the packed
        // depth+stencil formats that are more broadly supported on older/mobile hardware.
        constexpr std::array<vk::Format, 3> candidates = {
            vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint
        };

        for (vk::Format format : candidates) {
            vk::FormatProperties props = context.GetPhysicalDevice().getFormatProperties(format);
            if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
                return format;
            }
        }
        throw std::runtime_error("[Renderer | Swapchain] Failed to find a supported depth format.");

    }   // VulkanSwapchain::ChooseDepthFormat()

    uint32_t VulkanSwapchain::ChooseMinImageCount(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities) {

        auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
        if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
            minImageCount = surfaceCapabilities.maxImageCount;
        }
        return minImageCount;

    }   // VulkanSwapchain::::ChooseSwapMinImageCount()

}   // NAMESPACE CRUCIBLE
