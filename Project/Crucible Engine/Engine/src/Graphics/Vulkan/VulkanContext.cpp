/*************************************************************************
* \file:         VulkanContext.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          16 November 2026
* \version       1.1
* \brief
*                See VulkanContext.h for what this class owns and why.
*
* \history
*    -v1.0       initial implementation
*    -v1.1       added support for pipeline caching (shader graph editor)
*
**************************************************************************/

#include "VulkanContext.h"
#include "Systems/ExecutablePath.h"   // where the pipeline cache file lives

// Compile VMA's implementation exactly once, here. VulkanContext.h already pulled in
// <vk_mem_alloc.h> for declarations (no VMA_IMPLEMENTATION defined there); the library's own
// implementation block sits OUTSIDE that header's include guard specifically so a second,
// macro-guarded #include like this one re-triggers it in one chosen translation unit, same idea
// as the usual single-header "#define ..._IMPLEMENTATION" pattern.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <iostream>

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

    // ------------------------------------------------------------------------------------------------------------- //
    // ACCESSORS
    // ------------------------------------------------------------------------------------------------------------- //

    const vk::raii::Instance&       VulkanContext::GetInstance()            const { return m_instance; }
    const vk::raii::PhysicalDevice& VulkanContext::GetPhysicalDevice()      const { return m_physicalDevice; }
    const vk::raii::Device&         VulkanContext::GetDevice()              const { return m_device; }
    const vk::raii::Queue&          VulkanContext::GetGraphicsQueue()       const { return m_graphicsQueue; }

    vk::SurfaceKHR                  VulkanContext::GetSurface()             const { return m_surface; }
    uint32_t                        VulkanContext::GetGraphicsQueueFamily() const { return m_queueIndex; }
    VmaAllocator                    VulkanContext::GetAllocator()           const { return m_allocator; }
    const vk::raii::PipelineCache&  VulkanContext::GetPipelineCache()       const { return m_pipelineCache.Get(); }
    void                            VulkanContext::SavePipelineCache()            { m_pipelineCache.Save(); }

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN CONTEXT
    // ------------------------------------------------------------------------------------------------------------- //

    VulkanContext::VulkanContext(WindowManager& window) {

        CreateInstance();
        SetupDebugMessenger();
        CreateSurface(window);
        PickPhysicalDevice();
        CreateLogicalDevice();
        CreateAllocator();

        // Beside the program: one cache per machine and per build folder, never in the repository.
        m_pipelineCache.Load(m_physicalDevice, m_device, ExecutableDirectory() / "Cache" / "PipelineCache.bin");

    }   // VulkanContext::VulkanContext()

    VulkanContext::~VulkanContext() {

        // Every VulkanBuffer/VulkanImage suballocated from m_allocator has already freed its
        // allocation by this point (see the declaration-order note on m_allocator in the header),
        // so it's safe to tear the allocator itself down now, before the implicit destruction of
        // m_device/m_instance/etc. below runs.
        if (m_allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_allocator);
            m_allocator = VK_NULL_HANDLE;
        }

    }   // VulkanContext::~VulkanContext()

    void VulkanContext::ShutDown() {

        m_device.waitIdle();
        m_pipelineCache.Save();

    }   // VulkanContext::ShutDown()

    void VulkanContext::CreateInstance() {

        constexpr vk::ApplicationInfo appInfo{
            .pApplicationName = "Mekanomachia",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "Crucible Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = vk::ApiVersion13
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
        Selects the best graphics card device in the system that supports the features required,
        and assigns it to VulkanContext::m_physicalDevice data member.
        Every suitable device (see IsDeviceSuitable()) is given a score (see ScoreDevice()) and
        the highest one wins. On a tie the first one enumerated is kept, which is the order the
        OS/driver prefers.
    */
    void VulkanContext::PickPhysicalDevice() {

        std::vector<vk::raii::PhysicalDevice> devices = m_instance.enumeratePhysicalDevices();

        const vk::raii::PhysicalDevice* bestDevice = nullptr;
        uint64_t bestScore = 0;

        for (const auto& device : devices) {

            if (!IsDeviceSuitable(device)) { continue; }

            const uint64_t score = ScoreDevice(device);
            if (bestDevice == nullptr || score > bestScore) {
                bestDevice = &device;
                bestScore = score;
            }
        }

        if (bestDevice == nullptr) {
            throw std::runtime_error("[Renderer] Failed to find suitable GPU for program.");
        }
        m_physicalDevice = *bestDevice;

        const vk::PhysicalDeviceProperties properties = m_physicalDevice.getProperties();
        std::cout << "[Renderer] Using GPU: " << properties.deviceName.data()
            << " (" << to_string(properties.deviceType) << ")" << std::endl;

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

        // Get the first index in qFamilyProps that supports graphics, compute (the volumetric fog
        // runs compute passes on the same queue, between draws) and surface presentation.
        for (uint32_t qfpIndex = 0; qfpIndex < qFamilyProps.size(); ++qfpIndex) {

            if ((qFamilyProps[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics)
                && (qFamilyProps[qfpIndex].queueFlags & vk::QueueFlagBits::eCompute)
                && m_physicalDevice.getSurfaceSupportKHR(qfpIndex, *m_surface)) {

                m_queueIndex = qfpIndex;
                break;
            }
        }
        if (m_queueIndex == ~0) {
            throw std::runtime_error("[Renderer] Failed to find queue for graphics, compute and surface presentation.\n");
        }

        // Specify device features (Currently leaving everything to vk::False).
        vk::PhysicalDeviceFeatures deviceFeatures;

        // Enabling additional features.
        vk::StructureChain<vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = {
                // Anisotropic texture filtering is optional: switched on whenever the GPU has it
                // (Renderer::CreateMaterialSampler() checks the same flag before using it).
                // So are BC compressed textures (.bill, every desktop GPU has them):
                // RenderResources::CreateTextureCompressed() checks the format is usable.
                {.features = {.samplerAnisotropy = m_physicalDevice.getFeatures().samplerAnisotropy,
                              .textureCompressionBC = m_physicalDevice.getFeatures().textureCompressionBC}},
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

    /*!
        \brief
        Creates the single VmaAllocator that every VulkanBuffer/VulkanImage suballocates its GPU
        memory from (see VulkanResource.h/.cpp), replacing what used to be one individual
        vkAllocateMemory call per buffer/image. Must run after CreateLogicalDevice(): VMA needs a
        live instance/physical device/device to query memory properties and (if used) wire up
        extension function pointers.
    */
    void VulkanContext::CreateAllocator() {

        VmaAllocatorCreateInfo allocatorInfo{
            .physicalDevice   = *m_physicalDevice,
            .device           = *m_device,
            .instance         = *m_instance,
            .vulkanApiVersion = VK_API_VERSION_1_3,   // matches vk::ApiVersion13 in CreateInstance()
        };

        if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] Failed to create VmaAllocator.");
        }

    }   // VulkanContext::CreateAllocator()

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

        // Check if any single queue family supports graphics and compute operations and presenting
        // to our surface. This is exactly what CreateLogicalDevice() needs: without the present half
        // of the check, a GPU that cannot show images in this window could be picked here and
        // then fail there, instead of being skipped in favour of another GPU. (Every desktop GPU's
        // graphics family also does compute; the check just makes the requirement explicit.)
        auto queueFamilies = device.getQueueFamilyProperties();
        bool supportsGraphics = false;
        for (uint32_t qfpIndex = 0; qfpIndex < queueFamilies.size(); ++qfpIndex) {

            if ((queueFamilies[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics)
                && (queueFamilies[qfpIndex].queueFlags & vk::QueueFlagBits::eCompute)
                && device.getSurfaceSupportKHR(qfpIndex, *m_surface)) {

                supportsGraphics = true;
                break;
            }
        }

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
        Rates a device that already passed IsDeviceSuitable(), so PickPhysicalDevice() can choose
        between several suitable GPUs (e.g. a laptop's integrated and discrete ones).
        The device type decides first: discrete > integrated > virtual > anything else.
        Between two devices of the same type, the one with more dedicated memory wins.
        \return
        Returns the score; higher is better.
    */
    uint64_t VulkanContext::ScoreDevice(const vk::raii::PhysicalDevice& device) {

        uint64_t typeRank = 0;
        switch (device.getProperties().deviceType) {
            case vk::PhysicalDeviceType::eDiscreteGpu:   typeRank = 3; break;
            case vk::PhysicalDeviceType::eIntegratedGpu: typeRank = 2; break;
            case vk::PhysicalDeviceType::eVirtualGpu:    typeRank = 1; break;
            default:                                     typeRank = 0; break;   // eCpu (software renderer), eOther
        }

        // Total size of the device-local heaps, i.e. the GPU's own memory (VRAM). An integrated
        // GPU reports a share of system RAM here, which is why this is only a tie-break.
        const vk::PhysicalDeviceMemoryProperties memoryProperties = device.getMemoryProperties();
        vk::DeviceSize deviceLocalBytes = 0;
        for (uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex) {

            if (memoryProperties.memoryHeaps[heapIndex].flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
                deviceLocalBytes += memoryProperties.memoryHeaps[heapIndex].size;
            }
        }

        // Type rank in the upper 32 bits, memory in MiB in the lower 32: any difference in type
        // always outweighs any difference in memory.
        return (typeRank << 32) + (deviceLocalBytes >> 20);

    }   // VulkanContext::ScoreDevice()

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