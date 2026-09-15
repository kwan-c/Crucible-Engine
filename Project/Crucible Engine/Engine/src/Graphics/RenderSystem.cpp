#include "RenderSystem.h"
#include "GraphicsUtils.h"

// STL
#include <stdexcept>
#include <iostream>
#include <cstdint>
#include <limits>   // numeric_limits
#include <algorithm>    // clamp

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace {

    std::vector<const char*> requiredDeviceExtension = { vk::KHRSwapchainExtensionName };
    const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };

    constexpr int MAX_FRAMES_IN_FLIGHT = 2;

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    Renderer::Renderer(WindowManager& window) : m_window{ window } { InitVulkan(); }
    Renderer::~Renderer() {
        m_device.waitIdle();
        CleanupSwapChain();
    }

    void Renderer::Update(float dt) {
        (void)dt;

        // QUERY & CULL

        // POPULATE RENDER QUEUE

        // SORT & BATCH

        // EXECUTE GRAPHICS CALLS
        DrawFrame();

    }   // Renderer::Update()

    void Renderer::InitVulkan() {

        CreateInstance();
        SetupDebugMessenger();
        CreateSurface();

        PickPhysicalDevice();
        CreateLogicalDevice();

        CreateSwapChain();
        CreateImageViews();
        CreateGraphicsPipeline();

        CreateCommandPool();
        CreateCommandBuffers();
        CreateSyncObjects();

    }   // Renderer::InitVulkan()

    void Renderer::DrawFrame() {

        // Blocks CPU until previous frame is complete.
        auto fenceResult = m_device.waitForFences(*m_inFlightFences[m_frameIndex], vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("[Renderer] Failed to wait for fence.");
        }

        // Obtain image from swap chain.
        auto [result, imageIndex] = m_swapChain.acquireNextImage(UINT64_MAX, *m_presentCompleteSemaphores[m_frameIndex], nullptr);

        // Handle out-of-date or suboptimal swap chains.
        if (result == vk::Result::eErrorOutOfDateKHR) {
            RecreateSwapChain();
            return;
        }
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
            assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
            throw std::runtime_error("[Renderer] Failed to acquire swap chain image.");
        }
        m_device.resetFences(*m_inFlightFences[m_frameIndex]);

        // Record and submit command buffer.
        RecordCommandBuffer(imageIndex);
        vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        const vk::SubmitInfo   submitInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*m_presentCompleteSemaphores[m_frameIndex],
            .pWaitDstStageMask = &waitDestinationStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &*m_commandBuffers[m_frameIndex],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*m_renderFinishedSemaphores[imageIndex]
        };
        m_graphicsQueue.submit(submitInfo, *m_inFlightFences[m_frameIndex]);

        // Submit result back to swap chain.
        const vk::PresentInfoKHR presentInfoKHR{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*m_renderFinishedSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &*m_swapChain,
            .pImageIndices = &imageIndex
        };
        result = m_graphicsQueue.presentKHR(presentInfoKHR);
        if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR) || m_window.m_framebufferResized) {
            m_window.m_framebufferResized = false;
            RecreateSwapChain();
        }
        else {
            // There are no other success codes than eSuccess; on any error code, presentKHR already threw an exception.
            assert(result == vk::Result::eSuccess);
        }

        // Advance to next frame.
        m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;

    }   // Renderer::DrawFrame()

    // ------------------------------------------------------------------------------------------------------------- //
    //  VULKAN SET-UP FUNCTIONS
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::CreateInstance() {

        constexpr vk::ApplicationInfo appInfo{
            .pApplicationName = "Mekanomachia",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "Crucible Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = vk::ApiVersion14
        };

        // REQUIRED LAYERS --------------------------------------------------------------------- //

        // Get the required layers
        std::vector<const char*> requiredLayers;
        if (enableValidationLayers) {
            requiredLayers.assign(validationLayers.begin(), validationLayers.end());
        }

        // Check if the required layers are supported by the Vulkan implementation.
        auto layerProperties = m_context.enumerateInstanceLayerProperties();

        auto unsupportedLayerIt = std::ranges::find_if(requiredLayers, [&layerProperties](const auto& requiredLayer) {
            return std::ranges::none_of(layerProperties, [requiredLayer](const auto& layerProperty) {
                return strcmp(layerProperty.layerName, requiredLayer) == 0; });
            });

        if (unsupportedLayerIt != requiredLayers.end()) {
            throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
        }

        // REQUIRED EXTENSIONS ----------------------------------------------------------------- //

        // Get the required instance extensions
        auto requiredExtensions = GetRequiredInstanceExtensions();

        // Check if required extensions are supported by the Vulkan implementation.
        auto extensionProperties = m_context.enumerateInstanceExtensionProperties();
        auto unsupportedPropertyIt = std::ranges::find_if(requiredExtensions, [&extensionProperties](const auto& requiredExtension) {
            return std::ranges::none_of(extensionProperties, [&requiredExtension](const auto& extensionProperty) {
                return strcmp(extensionProperty.extensionName, requiredExtension) == 0; });
            });

        if (unsupportedPropertyIt != requiredExtensions.end()) {
            throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
        }

        // CREATING INSTANCE ------------------------------------------------------------------- //

        vk::InstanceCreateInfo createInfo{
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
            .ppEnabledLayerNames = requiredLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
            .ppEnabledExtensionNames = requiredExtensions.data(),
        };

        m_instance = vk::raii::Instance(m_context, createInfo);

    }   // Renderer::CreateInstance()

    /*!
        \brief
        Set up parameters for m_debugMessenger. Does not do anything when validation layers are disabled.
    */
    void Renderer::SetupDebugMessenger() {

        if (!enableValidationLayers) return;

        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
            | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);

        vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
            | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
            | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

        vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
            .messageSeverity = severityFlags,
            .messageType = messageTypeFlags,
            .pfnUserCallback = &DebugCallback
        };

        m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);

    }   // Renderer::SetupDebugMessenger()

    /*
        \brief
        Creates a VkSurfaceKHR object that represents an abstract type of surface to present rendered images to,
        which will be backed by "window" already opened with GLFW.
        \param
        Pointer to a GLFWwindow already opened with GLFW.
    */
    void Renderer::CreateSurface() {

        VkSurfaceKHR surface = nullptr;
        if (glfwCreateWindowSurface(*m_instance, m_window.GetNativeHandle(), nullptr, &surface) != 0) {
            throw std::runtime_error("[Renderer] Failed to create window surface.");
        }
        m_surface = vk::raii::SurfaceKHR(m_instance, surface);
    }

    /*
        \brief
        Selects graphics card device in the system that supports features required,
        and assigns it to Renderer.m_physicalDevice data member.
    */
    void Renderer::PickPhysicalDevice() {

        std::vector<vk::raii::PhysicalDevice> devices = m_instance.enumeratePhysicalDevices();
        const auto deviceIt = std::ranges::find_if(devices, [&](const auto& device) {
            return IsDeviceSuitable(device); });

        if (deviceIt == devices.end()) {
            throw std::runtime_error("[Renderer] Failed to find suitable GPU for program.");
        }
        m_physicalDevice = *deviceIt;

    }   // Renderer::PickPhysicalDevice()

    /*
        \brief
        Creates logical device that interfaces with physical device,
        and assigns it to Renderer::m_device data member.
        Creates graphics queue and assigns it to Renderer::m_graphicsQueue data member.
    */
    void Renderer::CreateLogicalDevice() {

        // Specify queue to be created.
        std::vector<vk::QueueFamilyProperties> qFamilyProps = m_physicalDevice.getQueueFamilyProperties();

        // Get the first index in qFamilyProps that supports both graphics and surface presentation.
        for (uint32_t qfpIndex = 0; qfpIndex < qFamilyProps.size(); ++qfpIndex) {

            if ((qFamilyProps[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics)
                && m_physicalDevice.getSurfaceSupportKHR(qfpIndex, *m_surface)) {

                m_queueIndex = qfpIndex;
                break;
            }
        }
        if (m_queueIndex == ~0) {
            throw std::runtime_error("[Renderer] Failed to find queue for graphics and surface presentation.\n");
        }

        // Specify device features (Currently leaving everything to vk::False).
        vk::PhysicalDeviceFeatures deviceFeatures;

        // Enabling additional features.
        vk::StructureChain<vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = {
                {},
                {.shaderDrawParameters = true},
                {.synchronization2 = true, .dynamicRendering = true},
                {.extendedDynamicState = true}
        };

        // Create the logical device.
        float queuePriority = 0.5f;
        vk::DeviceQueueCreateInfo deviceQCreateInfo{
            .queueFamilyIndex = m_queueIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority
        };

        vk::DeviceCreateInfo deviceCreateInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &deviceQCreateInfo,
            .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
            .ppEnabledExtensionNames = requiredDeviceExtension.data()
        };

        m_device = vk::raii::Device(m_physicalDevice, deviceCreateInfo);
        m_graphicsQueue = vk::raii::Queue(m_device, m_queueIndex, 0);

    }   // Renderer::CreateLogicalDevice()

    void Renderer::CreateSwapChain() {

        vk::SurfaceCapabilitiesKHR surfaceCapabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);
        m_swapChainExtent = ChooseSwapExtent(surfaceCapabilities, m_window.GetNativeHandle());
        uint32_t minImageCount = ChooseSwapMinImageCount(surfaceCapabilities);

        std::vector<vk::SurfaceFormatKHR> availableFormats = m_physicalDevice.getSurfaceFormatsKHR(*m_surface);
        m_swapChainSurfaceFormat = ChooseSwapSurfaceFormat(availableFormats);

        std::vector<vk::PresentModeKHR> availablePresentModes = m_physicalDevice.getSurfacePresentModesKHR(*m_surface);

        vk::SwapchainCreateInfoKHR swapChainCreateInfo{
            .surface = *m_surface,
            .minImageCount = minImageCount,
            .imageFormat = m_swapChainSurfaceFormat.format,
            .imageColorSpace = m_swapChainSurfaceFormat.colorSpace,
            .imageExtent = m_swapChainExtent,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = ChooseSwapPresentMode(availablePresentModes),
            .clipped = true
        };

        m_swapChain = vk::raii::SwapchainKHR(m_device, swapChainCreateInfo);
        m_swapChainImages = m_swapChain.getImages();

    }   // Renderer::CreateSwapChain()

    void Renderer::RecreateSwapChain() {

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

        m_device.waitIdle();
        CleanupSwapChain();

        CreateSwapChain();
        CreateImageViews();

    }   // Renderer::RecreateSwapChain()

    void Renderer::CleanupSwapChain() {

        m_swapChainImageViews.clear();
        m_swapChain = nullptr;

    }   // Renderer::CleanupSwapChain()

    void Renderer::CreateImageViews() {

        assert(m_swapChainImageViews.empty());
        vk::ImageViewCreateInfo imageViewCreateInfo{
            .viewType = vk::ImageViewType::e2D,
            .format = m_swapChainSurfaceFormat.format,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        for (auto& image : m_swapChainImages) {
            imageViewCreateInfo.image = image;
            m_swapChainImageViews.emplace_back(m_device, imageViewCreateInfo);
        }

    }   // Renderer::CreateImageViews()

    /*
        \brief
        Creates shader module, sets parameters for fixed functions, then consolidates the information
        structures to create the graphics pipeline which is required for all draw commands.
    */
    void Renderer::CreateGraphicsPipeline() {

        // Programmable stages (Vertex and Fragment shaders).
        vk::raii::ShaderModule shaderModule = CreateShaderModule(
            Graphics::Utils::ReadFile("../../../Assets/Shaders/DefaultShader.spv"));

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = shaderModule,
            .pName = "vertMain"
        };
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = shaderModule,
            .pName = "fragMain"
        };
        vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        // Fixed functions.
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo; // Currently hardcoding vertices in shader.
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

        // Set viewport to entire swapchain extent, make them dynamic more more flexibility in future.
        vk::Viewport viewport{ 0.0f, 0.0f, static_cast<float>(m_swapChainExtent.width), static_cast<float>(m_swapChainExtent.height), 0.0f, 1.0f };
        vk::Rect2D scissor{ vk::Offset2D{0,0}, m_swapChainExtent };
        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState = {
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eClockwise,
            .depthBiasEnable = vk::False,
            .lineWidth = 1.0f
        };

        // Disable multisampling and color blending for now...
        vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };
        vk::PipelineColorBlendStateCreateInfo colorBlending{
            .logicOpEnable = vk::False,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };

        // Pipeline layout object (for uniform values).
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = 0, .pushConstantRangeCount = 0 };
        m_pipelineLayout = vk::raii::PipelineLayout(m_device, pipelineLayoutInfo);

        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &m_swapChainSurfaceFormat.format };

        // Consolidate all information to create Graphics Pipeline.
        vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain =
        {
            {
                .stageCount = 2,
                .pStages = shaderStages,
                .pVertexInputState = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = m_pipelineLayout,
                .renderPass = nullptr
            },
            {.colorAttachmentCount = 1, .pColorAttachmentFormats = &m_swapChainSurfaceFormat.format }
        };
        m_graphicsPipeline = vk::raii::Pipeline(m_device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());

    }   // Renderer::CreateGraphicsPipeline()

    void Renderer::CreateCommandPool() {

        vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = m_queueIndex
        };
        m_commandPool = vk::raii::CommandPool(m_device, poolInfo);

    }   // Renderer::CreateCommandPool()

    void Renderer::CreateCommandBuffers() {

        vk::CommandBufferAllocateInfo allocInfo{
            .commandPool = m_commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT
        };
        m_commandBuffers = vk::raii::CommandBuffers(m_device, allocInfo);

    }   // Renderer::CreateCommandBuffer()

    void Renderer::RecordCommandBuffer(uint32_t imageIndex) {

        auto& commandBuffer = m_commandBuffers[m_frameIndex];

        commandBuffer.begin({});

        // Before starting rendering, transition the swapchain image to vk::ImageLayout::eColorAttachmentOptimal
        TransitionImageLayout(
            imageIndex,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            {},                                                        // srcAccessMask (no need to wait for previous operations)
            vk::AccessFlagBits2::eColorAttachmentWrite,                // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,        // srcStage
            vk::PipelineStageFlagBits2::eColorAttachmentOutput         // dstStage
        );

        vk::ClearValue              clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::RenderingAttachmentInfo attachmentInfo = {
            .imageView = m_swapChainImageViews[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor
        };

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = m_swapChainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachmentInfo
        };

        commandBuffer.beginRendering(renderingInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_graphicsPipeline);
        commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(m_swapChainExtent.width), static_cast<float>(m_swapChainExtent.height), 0.0f, 1.0f));
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_swapChainExtent));
        commandBuffer.draw(3, 1, 0, 0);

        commandBuffer.endRendering();

        // After rendering, transition the swapchain image to vk::ImageLayout::ePresentSrcKHR
        TransitionImageLayout(
            imageIndex,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite,             // srcAccessMask
            {},                                                     // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,     // srcStage
            vk::PipelineStageFlagBits2::eBottomOfPipe               // dstStage
        );

        commandBuffer.end();

    }   // Renderer::RecordCommandBuffer()

    void Renderer::CreateSyncObjects() {

        assert(m_presentCompleteSemaphores.empty() && m_renderFinishedSemaphores.empty() && m_inFlightFences.empty());

        for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
            m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo());
        }
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            m_presentCompleteSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo());
            m_inFlightFences.emplace_back(m_device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
        }

    }   // Renderer::CreateSyncObjects()

    /*!
        \brief
        Compiles the names of all required extensions. (Additionally adds DebugUtilsExtension if validation layers are enabled)
        \return
        Returns a vector of Extension names. (string literal)
    */
    std::vector<const char*> Renderer::GetRequiredInstanceExtensions() {

        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
        if (enableValidationLayers) {
            extensions.push_back(vk::EXTDebugUtilsExtensionName);
        }
        return extensions;

    }   // Renderer::GetRequiredInstanceExtensions()

    vk::SurfaceFormatKHR Renderer::ChooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats) {

        assert(!availableFormats.empty());

        const auto formatIt = std::ranges::find_if(availableFormats, [](const auto& format) {
            return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });

        return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];

    }   // Renderer::ChooseSwapSurfaceFormat()

    vk::PresentModeKHR Renderer::ChooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes) {

        // Assert that at least Fifo is present.
        assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
            return presentMode == vk::PresentModeKHR::eFifo; }));

        // Return eMailbox if available, else default to eFifo.
        return std::ranges::any_of(availablePresentModes, [](const vk::PresentModeKHR value) {
            return vk::PresentModeKHR::eMailbox == value; }) ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;

    }   // Renderer::ChooseSwapPresentMode()

    vk::Extent2D Renderer::ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, GLFWwindow* window) {

        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return surfaceCapabilities.currentExtent;
        }
        int width{}, height{};
        glfwGetFramebufferSize(window, &width, &height);

        return {
            std::clamp<uint32_t>(width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height)
        };

    }   // Renderer::ChooseSwapExtent()

    uint32_t Renderer::ChooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities) {

        auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
        if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
            minImageCount = surfaceCapabilities.maxImageCount;
        }
        return minImageCount;

    }   // Renderer::ChooseSwapMinImageCount()

    [[nodiscard]] vk::raii::ShaderModule Renderer::CreateShaderModule(const std::vector<char>& code) const {

        vk::ShaderModuleCreateInfo createInfo{
            .codeSize = code.size() * sizeof(char),
            .pCode = reinterpret_cast<const uint32_t*>(code.data())
        };
        return { m_device, createInfo };

    }   // Renderer::CreateShaderModule()

    /*
        \brief
        Checks if a device is suitable by checking against requirements.
        \return
        Returns true if device passes all requirements, else returns false.
    */
    bool Renderer::IsDeviceSuitable(const vk::raii::PhysicalDevice& device) {

        // Check if device supports Vulkan 1.3 API version.
        bool supportsVulkan1_3 = device.getProperties().apiVersion >= vk::ApiVersion13;

        // Check if any queue families support graphics operations.
        auto queueFamilies = device.getQueueFamilyProperties();
        bool supportsGraphics = std::ranges::any_of(queueFamilies, [](const auto& qfp) {
            return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

        // Check if all required device extensions are available.
        auto availableDeviceExtensions = device.enumerateDeviceExtensionProperties();
        bool supportsAllRequiredExtensions = std::ranges::all_of(requiredDeviceExtension, [&availableDeviceExtensions](const auto& requiredDeviceExtension) {
            return std::ranges::any_of(availableDeviceExtensions, [requiredDeviceExtension](const auto& availableDeviceExtension) {
                return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0; });
            });

        // Check if device supports required features (shader draw parameters, dynamic rendering and extended dynamic state).
        auto features = device.template getFeatures2<vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        bool supportsRequiredFeatures =
            features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
            features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        // Return true if the device meets all criteria.
        return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;

    }   // Renderer::IsDeviceSuitable

    /*
        \brief
        Debug messenger callback function that prints warning and error messages.
    */
    VKAPI_ATTR vk::Bool32 VKAPI_CALL Renderer::DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT         severity,
        vk::DebugUtilsMessageTypeFlagsEXT                type,
        const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData) {
        (void)pUserData;
        // Only print warnings or errors.
        if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
            || severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {

            std::cerr << "[Renderer] Validation layer: type " << to_string(type)
                << ", msg: " << pCallbackData->pMessage << std::endl;
        }
        return vk::False;

    }   // Renderer::DebugCallback()

    void Renderer::TransitionImageLayout(uint32_t imageIndex, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
        vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
        vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask) {

        vk::ImageMemoryBarrier2 barrier = {
            .srcStageMask = src_stage_mask,
            .srcAccessMask = src_access_mask,
            .dstStageMask = dst_stage_mask,
            .dstAccessMask = dst_access_mask,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_swapChainImages[imageIndex],
            .subresourceRange = {
                   .aspectMask = vk::ImageAspectFlagBits::eColor,
                   .baseMipLevel = 0,
                   .levelCount = 1,
                   .baseArrayLayer = 0,
                   .layerCount = 1}
        };
        vk::DependencyInfo dependency_info = {
            .dependencyFlags = {},
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };
        m_commandBuffers[m_frameIndex].pipelineBarrier2(dependency_info);
    }   // Renderer::TransitionImageLayout()

}   // NAMESPACE CRUCIBLE
