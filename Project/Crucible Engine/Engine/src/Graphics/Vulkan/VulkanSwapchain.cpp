#include "VulkanSwapchain.h"

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

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN SWAPCHAIN
    // ------------------------------------------------------------------------------------------------------------- //

    VulkanSwapchain::VulkanSwapchain(VulkanContext& context, WindowManager& window) : m_window { window } {

        CreateSwapchain(context);
        CreateImageViews(context);

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

    }   // VulkanSwapchain::Recreate()

    void VulkanSwapchain::CleanUp() {

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

    uint32_t VulkanSwapchain::ChooseMinImageCount(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities) {

        auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
        if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
            minImageCount = surfaceCapabilities.maxImageCount;
        }
        return minImageCount;

    }   // VulkanSwapchain::::ChooseSwapMinImageCount()

}   // NAMESPACE CRUCIBLE