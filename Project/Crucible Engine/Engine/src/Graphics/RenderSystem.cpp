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
        m_context.ShutDown();
        m_swapchain.CleanUp();
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

        m_context.Init(m_window);
        m_swapchain.Init(m_context, m_window);
        
        CreateGraphicsPipeline();

        CreateCommandPool();
        CreateVertexBuffer();

        CreateCommandBuffers();
        CreateSyncObjects();

    }   // Renderer::InitVulkan()

    void Renderer::DrawFrame() {

        // Blocks CPU until previous frame is complete.
        auto fenceResult = m_context.GetDevice().waitForFences(*m_inFlightFences[m_frameIndex], vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("[Renderer] Failed to wait for fence.");
        }

        // Obtain image from swap chain.
        auto [result, imageIndex] = m_swapchain.GetSwapchain().acquireNextImage(UINT64_MAX, *m_presentCompleteSemaphores[m_frameIndex], nullptr);

        // Handle out-of-date or suboptimal swap chains.
        if (result == vk::Result::eErrorOutOfDateKHR) {
            m_swapchain.Recreate(m_context, m_window);
            return;
        }
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
            assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
            throw std::runtime_error("[Renderer] Failed to acquire swap chain image.");
        }
        m_context.GetDevice().resetFences(*m_inFlightFences[m_frameIndex]);

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
        m_context.GetGraphicsQueue().submit(submitInfo, *m_inFlightFences[m_frameIndex]);

        // Submit result back to swap chain.
        const vk::PresentInfoKHR presentInfoKHR{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*m_renderFinishedSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &*m_swapchain.GetSwapchain(),
            .pImageIndices = &imageIndex
        };
        result = m_context.GetGraphicsQueue().presentKHR(presentInfoKHR);
        if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR) || m_window.m_framebufferResized) {
            m_window.m_framebufferResized = false;
            m_swapchain.Recreate(m_context, m_window);
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

    /*!
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
        auto bindingDescription = Vertex::GetBindingDescription();
        auto attributeDescriptions = Vertex::GetAttributeDescriptions();
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount      = 1,
            .pVertexBindingDescriptions         = &bindingDescription,
            .vertexAttributeDescriptionCount    = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions       = attributeDescriptions.data()
        };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

        // Set viewport to entire swapchain extent, make them dynamic more more flexibility in future.
        vk::Viewport viewport{ 0.0f, 0.0f, static_cast<float>(m_swapchain.GetExtent().width), static_cast<float>(m_swapchain.GetExtent().height), 0.0f, 1.0f};
        vk::Rect2D scissor{ vk::Offset2D{0,0}, m_swapchain.GetExtent() };
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
        m_pipelineLayout = vk::raii::PipelineLayout(m_context.GetDevice(), pipelineLayoutInfo);

        auto swapchainFormat = m_swapchain.GetSurfaceFormat().format;

        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapchainFormat };

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
            {.colorAttachmentCount = 1, .pColorAttachmentFormats = &swapchainFormat }
        };
        m_graphicsPipeline = vk::raii::Pipeline(m_context.GetDevice(), nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());

    }   // Renderer::CreateGraphicsPipeline()

    void Renderer::CreateCommandPool() {

        vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = m_context.GetGraphicsQueueFamily()
        };
        m_commandPool = vk::raii::CommandPool(m_context.GetDevice(), poolInfo);

    }   // Renderer::CreateCommandPool()

    void Renderer::CreateCommandBuffers() {

        vk::CommandBufferAllocateInfo allocInfo{
            .commandPool        = m_commandPool,
            .level              = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT
        };
        m_commandBuffers = vk::raii::CommandBuffers(m_context.GetDevice(), allocInfo);

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
            .imageView = m_swapchain.GetImageViews()[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor
        };

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = m_swapchain.GetExtent()},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachmentInfo
        };

        commandBuffer.beginRendering(renderingInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_graphicsPipeline);
        commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(m_swapchain.GetExtent().width), static_cast<float>(m_swapchain.GetExtent().height), 0.0f, 1.0f));
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_swapchain.GetExtent()));

        commandBuffer.bindVertexBuffers(0, *m_vertexBuffer, { 0 });
        commandBuffer.draw(static_cast<uint32_t>(vertices.size()), 1, 0, 0);

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
        for (size_t i = 0; i < m_swapchain.GetImages().size(); ++i) {
            m_renderFinishedSemaphores.emplace_back(m_context.GetDevice(), vk::SemaphoreCreateInfo());
        }
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            m_presentCompleteSemaphores.emplace_back(m_context.GetDevice(), vk::SemaphoreCreateInfo());
            m_inFlightFences.emplace_back(m_context.GetDevice(), vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
        }

    }   // Renderer::CreateSyncObjects()

    void Renderer::CreateVertexBuffer() {

        vk::BufferCreateInfo bufferInfo{
            .size           = sizeof(vertices[0]) * vertices.size(),
            .usage          = vk::BufferUsageFlagBits::eVertexBuffer,
            .sharingMode    = vk::SharingMode::eExclusive
        };

        m_vertexBuffer = vk::raii::Buffer(m_context.GetDevice(), bufferInfo);

        vk::MemoryRequirements memRequirements = m_vertexBuffer.getMemoryRequirements();

        vk::MemoryAllocateInfo memoryAllocateInfo = {
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
        };

        m_vertexBufferMemory = vk::raii::DeviceMemory(m_context.GetDevice(), memoryAllocateInfo);
        m_vertexBuffer.bindMemory(*m_vertexBufferMemory, 0);

        void* data = m_vertexBufferMemory.mapMemory(0, bufferInfo.size);
        memcpy(data, vertices.data(), bufferInfo.size);
        m_vertexBufferMemory.unmapMemory();



    }   // Renderer::CreateVertexBuffer()

    uint32_t Renderer::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {

        vk::PhysicalDeviceMemoryProperties memProperties = m_context.GetPhysicalDevice().getMemoryProperties();

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }
        throw std::runtime_error("[Renderer] Failed to find suitable memory type.");

    }   // Renderer::FindMemoryType()

    [[nodiscard]] vk::raii::ShaderModule Renderer::CreateShaderModule(const std::vector<char>& code) const {

        vk::ShaderModuleCreateInfo createInfo{
            .codeSize = code.size() * sizeof(char),
            .pCode = reinterpret_cast<const uint32_t*>(code.data())
        };
        return { m_context.GetDevice(), createInfo };

    }   // Renderer::CreateShaderModule()

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
            .image = m_swapchain.GetImages()[imageIndex],
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
