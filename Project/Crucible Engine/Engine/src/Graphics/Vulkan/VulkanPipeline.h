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
    class VulkanSwapchain;

    class VulkanPipeline
    {
    public:
        void Initialize(
            VulkanContext& context,
            VulkanSwapchain& swapchain);

        void Cleanup();

        vk::raii::Pipeline const& GetPipeline() const;
        vk::raii::PipelineLayout const& GetLayout() const;

    private:
        vk::raii::ShaderModule CreateShaderModule(
            VulkanContext& context,
            std::vector<char> const& code);

        std::vector<char> ReadFile(
            std::string const& filename);

    private:
        vk::raii::PipelineLayout m_pipelineLayout = nullptr;
        vk::raii::Pipeline m_graphicsPipeline = nullptr;
    };
}