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
#include <iostream>

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
        // Transfer source: the scene pass copies it for the water (PostTargets::depthCopy).
        m_depthImage.Create(context, m_extent.width, m_extent.height,
            m_depthFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc,
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

        // Preference order: both are 8-bit sRGB formats and differ only in channel order, which
        // nothing in the engine depends on (pipelines, viewport images and ImGui all take the
        // format from here). BGRA is what Windows drivers offer; RGBA is the usual one elsewhere.
        constexpr std::array<vk::Format, 2> candidates = {
            vk::Format::eB8G8R8A8Srgb, vk::Format::eR8G8B8A8Srgb
        };

        for (vk::Format candidate : candidates) {
            const auto formatIt = std::ranges::find_if(formats, [candidate](const auto& format) {
                return format.format == candidate && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });

            if (formatIt != formats.end()) {
                return *formatIt;
            }
        }

        // The shaders output linear colour and rely on an sRGB target to encode it for the
        // display. Without one the image still renders, but visibly too dark - say why.
        std::cerr << "[Renderer | Swapchain] No 8-bit sRGB surface format available; falling back to "
            << to_string(formats[0].format) << ". Colours will look too dark." << std::endl;

        return formats[0];

    }   // VulkanSwapchain::ChooseSurfaceFormat()

    vk::PresentModeKHR VulkanSwapchain::ChoosePresentMode(std::vector<vk::PresentModeKHR> const& modes) {

        // FIFO is the one mode every GPU must have: waits for the monitor, never tears.
        assert(std::ranges::any_of(modes, [](auto presentMode) {
            return presentMode == vk::PresentModeKHR::eFifo; }));

        if (m_vsync) {
            return vk::PresentModeKHR::eFifo;
        }

        // VSync off: MAILBOX replaces the waiting image with the newest one, so the frame
        // rate is free and it still never tears. IMMEDIATE shows at once and may tear.
        const auto has = [&modes](vk::PresentModeKHR mode) { return std::ranges::find(modes, mode) != modes.end(); };
        if (has(vk::PresentModeKHR::eMailbox))   { return vk::PresentModeKHR::eMailbox; }
        if (has(vk::PresentModeKHR::eImmediate)) { return vk::PresentModeKHR::eImmediate; }
        return vk::PresentModeKHR::eFifo;

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

        // Pure 32-bit float depth, no stencil. This is the only depth format the renderer
        // supports: every depth view and layout transition uses the depth aspect alone (a packed
        // depth+stencil format would need the stencil aspect as well), and the shadow map is D32
        // regardless (Renderer::kShadowFormat). Desktop GPUs support it as a matter of course, so
        // a GPU without it gets a clear error here instead of invalid Vulkan usage later.
        constexpr vk::Format format = vk::Format::eD32Sfloat;

        const vk::FormatProperties props = context.GetPhysicalDevice().getFormatProperties(format);
        if (!(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)) {
            throw std::runtime_error("[Renderer | Swapchain] This GPU does not support D32_SFLOAT as a depth attachment, which the renderer requires.");
        }
        return format;

    }   // VulkanSwapchain::ChooseDepthFormat()

    uint32_t VulkanSwapchain::ChooseMinImageCount(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities) {

        auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
        if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
            minImageCount = surfaceCapabilities.maxImageCount;
        }
        return minImageCount;

    }   // VulkanSwapchain::::ChooseSwapMinImageCount()

}   // NAMESPACE CRUCIBLE
