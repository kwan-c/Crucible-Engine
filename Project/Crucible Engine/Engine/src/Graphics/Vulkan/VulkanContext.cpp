#include "VulkanContext.h"

#include <iostream>

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

    // ------------------------------------------------------------------------------------------------------------- //
    // ACCESSORS
    // ------------------------------------------------------------------------------------------------------------- //

    const vk::raii::Instance&       VulkanContext::GetInstance()            const { return m_instance; }
    const vk::raii::PhysicalDevice& VulkanContext::GetPhysicalDevice()      const { return m_physicalDevice; }
    const vk::raii::Device&         VulkanContext::GetDevice()              const { return m_device; }
    const vk::raii::Queue&          VulkanContext::GetGraphicsQueue()       const { return m_graphicsQueue; }

    vk::SurfaceKHR                  VulkanContext::GetSurface()             const { return m_surface; }
    uint32_t                        VulkanContext::GetGraphicsQueueFamily() const { return m_queueIndex; }

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN CONTEXT
    // ------------------------------------------------------------------------------------------------------------- //

    VulkanContext::VulkanContext(WindowManager& window) {

        CreateInstance();
        SetupDebugMessenger();
        CreateSurface(window);
        PickPhysicalDevice();
        CreateLogicalDevice();

    }   // VulkanContext::VulkanContext()

    void VulkanContext::ShutDown() {

        m_device.waitIdle();

    }   // VulkanContext::~VulkanContext()

    void VulkanContext::CreateInstance() {

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

    }   // VulkanContext::CreateInstance()

    void VulkanContext::SetupDebugMessenger() {

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

    }   // VulkanContext::SetupDebugMessenger()

    void VulkanContext::CreateSurface(WindowManager& window) {

        VkSurfaceKHR surface = nullptr;
        if (glfwCreateWindowSurface(*m_instance, window.GetNativeHandle(), nullptr, &surface) != 0) {
            throw std::runtime_error("[Renderer] Failed to create window surface.");
        }
        m_surface = vk::raii::SurfaceKHR(m_instance, surface);

    }   // VulkanContext::CreateSurface()

    /*!
        \brief
        Selects graphics card device in the system that supports features required,
        and assigns it to VulkanContext::m_physicalDevice data member.
    */
    void VulkanContext::PickPhysicalDevice() {

        std::vector<vk::raii::PhysicalDevice> devices = m_instance.enumeratePhysicalDevices();
        const auto deviceIt = std::ranges::find_if(devices, [&](const auto& device) {
            return IsDeviceSuitable(device); });

        if (deviceIt == devices.end()) {
            throw std::runtime_error("[Renderer] Failed to find suitable GPU for program.");
        }
        m_physicalDevice = *deviceIt;

    }   // VulkanContext::PickPhysicalDevice()

    /*!
        \brief
        Creates logical device that interfaces with physical device,
        and assigns it to VulkanContext::m_device data member.
        Creates graphics queue and assigns it to VulkanContext::m_graphicsQueue data member.
    */
    void VulkanContext::CreateLogicalDevice() {

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

    }   // VulkanContext::CreateLogicalDevice()

    // ------------------------------------------------------------------------------------------------------------- //
    // HELPER FUNCTIONS
    // ------------------------------------------------------------------------------------------------------------- //
    
    /*!
        \brief
        Compiles the names of all required extensions. (Additionally adds DebugUtilsExtension if validation layers are enabled)
        \return
        Returns a vector of Extension names. (string literal)
    */
    std::vector<const char*> VulkanContext::GetRequiredInstanceExtensions() {

        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
        if (enableValidationLayers) {
            extensions.push_back(vk::EXTDebugUtilsExtensionName);
        }
        return extensions;

    }   // VulkanContext::GetRequiredInstanceExtensions()

    /*!
        \brief
        Checks if a device is suitable by checking against requirements.
        \return
        Returns true if device passes all requirements, else returns false.
    */
    bool VulkanContext::IsDeviceSuitable(const vk::raii::PhysicalDevice& device) {

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

    }   // VulkanContext::IsDeviceSuitable()

    /*!
        \brief
        Debug messenger callback function that prints warning and error messages.
    */
    VKAPI_ATTR vk::Bool32 VKAPI_CALL VulkanContext::DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT         severity,
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

    }   // VulkanContext::DebugCallback()

}   // NAMESPACE CRUCIBLE