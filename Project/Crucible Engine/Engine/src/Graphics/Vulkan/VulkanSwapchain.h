#pragma once

#define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS  // To handle swap chain recreation.
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
    #include <vulkan/vulkan_raii.hpp>
#else
    import vulkan_hpp;
#endif

namespace Crucible
{
    class VulkanContext;
    class WindowManager;

    class VulkanSwapchain
    {
    public:
        void Initialize(
            VulkanContext& context,
            WindowManager& window);

        void Recreate(
            VulkanContext& context,
            WindowManager& window);

        void Cleanup();

        vk::raii::SwapchainKHR const& GetSwapchain() const;

        std::vector<vk::Image> const& GetImages() const;
        std::vector<vk::raii::ImageView> const& GetImageViews() const;

        vk::Extent2D GetExtent() const;
        vk::SurfaceFormatKHR GetSurfaceFormat() const;

        VkFormat GetColorAttachmentFormat() const;

    private:
        void CreateSwapchain(
            VulkanContext& context,
            WindowManager& window);

        void CreateImageViews(
            VulkanContext& context);

        vk::SurfaceFormatKHR ChooseSurfaceFormat(
            std::vector<vk::SurfaceFormatKHR> const& formats);

        vk::PresentModeKHR ChoosePresentMode(
            std::vector<vk::PresentModeKHR> const& modes);

        vk::Extent2D ChooseExtent(
            vk::SurfaceCapabilitiesKHR const& capabilities,
            WindowManager& window);

    private:
        vk::raii::SwapchainKHR m_swapchain = nullptr;

        std::vector<vk::Image> m_images;
        std::vector<vk::raii::ImageView> m_imageViews;

        vk::Extent2D m_extent{};
        vk::SurfaceFormatKHR m_surfaceFormat{};

        VkFormat m_colorAttachmentFormat =
            VK_FORMAT_UNDEFINED;
    };
}