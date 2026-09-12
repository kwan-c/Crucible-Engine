#include "RenderSystem.h"

// STL
#include <stdexcept>
#include <iostream>
#include <map>      // PickPhysicalDevice()

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

    void Renderer::Init() {}

    void Renderer::Update(float dt) {
        (void)dt;

        // QUERY & CULL

        // POPULATE RENDER QUEUE

        // SORT & BATCH

        // EXECUTE GRAPHICS CALLS

    }   // Renderer::Update()

    void Renderer::InitVulkan() {

        CreateInstance();
        SetupDebugMessenger();
        PickPhysicalDevice();
        CreateLogicalDevice();

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
        auto graphicsQFamilyProp = std::ranges::find_if(qFamilyProps, [](const auto& qfp) {
            return (qfp.queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0);
            });
        auto graphicsIdx = static_cast<uint32_t>(std::distance(qFamilyProps.begin(), graphicsQFamilyProp));
        float qPriority = 0.5f;
        vk::DeviceQueueCreateInfo deviceQCreateInfo{
            .queueFamilyIndex = graphicsIdx,
            .queueCount = 1,
            .pQueuePriorities = &qPriority
        };

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

        // Specifying device extensions.
        std::vector<const char*> requiredDeviceExtension = { vk::KHRSwapchainExtensionName };

        // Create the logical device.
        vk::DeviceCreateInfo deviceCreateInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &deviceQCreateInfo,
            .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
            .ppEnabledExtensionNames = requiredDeviceExtension.data()
        };

        m_device = vk::raii::Device(m_physicalDevice, deviceCreateInfo);
        m_graphicsQueue = vk::raii::Queue(m_device, graphicsIdx, 0);

    }   // Renderer::CreateLogicalDevice()

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
