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

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    Renderer::Renderer(GLFWwindow* window) { InitVulkan(window); }

    void Renderer::Update(float dt) {
        (void)dt;

        // QUERY & CULL

        // POPULATE RENDER QUEUE

        // SORT & BATCH

        // EXECUTE GRAPHICS CALLS

    }   // Renderer::Update()

    void Renderer::InitVulkan(GLFWwindow* window) {

        CreateInstance();
        SetupDebugMessenger();
        CreateSurface(window);

        PickPhysicalDevice();
        CreateLogicalDevice();

        CreateSwapChain(window);
        CreateImageViews();

        CreateGraphicsPipeline();

    }   // Renderer::InitVulkan()


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
    void Renderer::CreateSurface(GLFWwindow* window) {
        
        VkSurfaceKHR surface = nullptr;
        if (glfwCreateWindowSurface(*m_instance, window, nullptr, &surface) != 0) {
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
        uint32_t queueIdx = ~0u;
        for (uint32_t qfpIndex = 0; qfpIndex < qFamilyProps.size(); ++qfpIndex) {

            if ((qFamilyProps[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics)
                && m_physicalDevice.getSurfaceSupportKHR(qfpIndex, *m_surface)) {

                queueIdx = qfpIndex;
                break;
            }
        }
        if (queueIdx == ~0) {
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
                {.dynamicRendering = true},
                {.extendedDynamicState = true}
        };

        // Create the logical device.
        float queuePriority = 0.5f;
        vk::DeviceQueueCreateInfo deviceQCreateInfo{
            .queueFamilyIndex = queueIdx,
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
        m_graphicsQueue = vk::raii::Queue(m_device, queueIdx, 0);

    }   // Renderer::CreateLogicalDevice()

    void Renderer::CreateSwapChain(GLFWwindow* window) {

        vk::SurfaceCapabilitiesKHR surfaceCapabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);
        m_swapChainExtent = ChooseSwapExtent(surfaceCapabilities, window);
        uint32_t minImageCount = ChooseSwapMinImageCount(surfaceCapabilities);

        std::vector<vk::SurfaceFormatKHR> availableFormats = m_physicalDevice.getSurfaceFormatsKHR(*m_surface);
        m_swapChainSurfaceFormat = ChooseSwapSurfaceFormat(availableFormats);

        std::vector<vk::PresentModeKHR> availablePresentModes = m_physicalDevice.getSurfacePresentModesKHR(*m_surface);

        vk::SwapchainCreateInfoKHR swapChainCreateInfo{
            .surface            = *m_surface,
            .minImageCount      = minImageCount,
            .imageFormat        = m_swapChainSurfaceFormat.format,
            .imageColorSpace    = m_swapChainSurfaceFormat.colorSpace,
            .imageExtent        = m_swapChainExtent,
            .imageArrayLayers   = 1,
            .imageUsage         = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode   = vk::SharingMode::eExclusive,
            .preTransform       = surfaceCapabilities.currentTransform,
            .compositeAlpha     = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode        = ChooseSwapPresentMode(availablePresentModes),
            .clipped            = true
        };

        m_swapChain = vk::raii::SwapchainKHR(m_device, swapChainCreateInfo);
        m_swapChainImages = m_swapChain.getImages();

    }   // Renderer::CreateSwapChain()

    void Renderer::CreateImageViews() {

        assert(m_swapChainImageViews.empty());
        vk::ImageViewCreateInfo imageViewCreateInfo{
            .viewType           = vk::ImageViewType::e2D,
            .format             = m_swapChainSurfaceFormat.format,
            .subresourceRange   = { 
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
            Graphics::Utils::ReadFile("../../../Assets/Shaders/slang.spv"));

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
            .stage  = vk::ShaderStageFlagBits::eVertex,
            .module = shaderModule,
            .pName  = "vertMain"
        };
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
            .stage  = vk::ShaderStageFlagBits::eFragment,
            .module = shaderModule,
            .pName  = "fragMain"
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
                .stageCount             = 2,
                .pStages                = shaderStages,
                .pVertexInputState      = &vertexInputInfo,
                .pInputAssemblyState    = &inputAssembly,
                .pViewportState         = &viewportState,
                .pRasterizationState    = &rasterizer,
                .pMultisampleState      = &multisampling,
                .pColorBlendState       = &colorBlending,
                .pDynamicState          = &dynamicState,
                .layout                 = m_pipelineLayout,
                .renderPass             = nullptr
            },
            { .colorAttachmentCount = 1, .pColorAttachmentFormats = &m_swapChainSurfaceFormat.format }
        };
        m_graphicsPipeline = vk::raii::Pipeline(m_device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());

    }   // Renderer::CreateGraphicsPipeline()

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
        bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
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
                                                          const vk::DebugUtilsMessengerCallbackDataEXT*    pCallbackData,
                                                          void*                                            pUserData) {
        (void)pUserData;
        // Only print warnings or errors.
        if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
            || severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {

            std::cerr << "[Renderer] Validation layer: type " << to_string(type)
                << ", msg: " << pCallbackData->pMessage << std::endl;
        }
        return vk::False;

    }   // Renderer::DebugCallback()

}   // NAMESPACE CRUCIBLE
