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

namespace Crucible {

    Renderer::Renderer(WindowManager& window) 
        : m_window{ window }, m_context{ m_window }, m_swapchain{ m_context, m_window } {

        m_pipeline.CreatePipeline(m_context, m_swapchain);
        m_frameManager.Init(m_context, m_swapchain);

        CreateVertexBuffer();
    }

    Renderer::~Renderer() { m_context.ShutDown(); }

    void Renderer::Update(float dt) {
        (void)dt;

        // QUERY & CULL

        // POPULATE RENDER QUEUE

        // SORT & BATCH

        // EXECUTE GRAPHICS CALLS
        DrawFrame();

    }   // Renderer::Update()

    void Renderer::DrawFrame() {

        bool err = false;
        FrameContext fc = m_frameManager.BeginFrame(m_context, m_swapchain, err);
        if (err) { return; }

        // Record and submit command buffer.
        RecordCommandBuffer(fc);

        m_frameManager.EndFrame(m_context, m_swapchain, fc, m_window);

    }   // Renderer::DrawFrame()

    // ------------------------------------------------------------------------------------------------------------- //
    //  VULKAN SET-UP FUNCTIONS
    // ------------------------------------------------------------------------------------------------------------- //

    const ImGuiVulkanContext& Renderer::GetImguiContext() const {

        const ImGuiVulkanContext context = {
            .window         = m_window.GetNativeHandle(),
            .instance       = *m_context.GetInstance(),
            .physicalDevice = *m_context.GetPhysicalDevice(),
            .device         = *m_context.GetDevice(),
            .graphicsQueue  = *m_context.GetGraphicsQueue(),
            .graphicsQueueFamily = m_context.GetGraphicsQueueFamily(),
            //.descriptorPool = *m_descriptorPool,
            //.pipelineCache = 
            .colorFormat    = m_swapchain.GetColorAttachmentFormat(),
            .minImageCount  = static_cast<uint32_t>(2),
            .imageCount     = static_cast<uint32_t>(m_swapchain.GetImages().size()),
        };

        return context;

    }   // Renderer::GetImguiContext()

    vk::raii::CommandBuffer& Renderer::GetCurrentCommandBuff() {

        return m_frameManager.GetCurrentFrame().commandBuffer;

    }   // Renderer::GetCurrentCommandBuff()

    void Renderer::RecordCommandBuffer(FrameContext& fc) {

        fc.frame.commandBuffer.begin({});

        // Before starting rendering, transition the swapchain image to vk::ImageLayout::eColorAttachmentOptimal
        TransitionImageLayout(
            fc,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            {},                                                        // srcAccessMask (no need to wait for previous operations)
            vk::AccessFlagBits2::eColorAttachmentWrite,                // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,        // srcStage
            vk::PipelineStageFlagBits2::eColorAttachmentOutput         // dstStage
        );

        vk::ClearValue              clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::RenderingAttachmentInfo attachmentInfo = {
            .imageView = m_swapchain.GetImageViews()[fc.imageIndex],
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

        fc.frame.commandBuffer.beginRendering(renderingInfo);

        fc.frame.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetPipeline());
        fc.frame.commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(m_swapchain.GetExtent().width), static_cast<float>(m_swapchain.GetExtent().height), 0.0f, 1.0f));
        fc.frame.commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_swapchain.GetExtent()));
        
        fc.frame.commandBuffer.bindVertexBuffers(0, *m_vertexBuffer, { 0 });
        fc.frame.commandBuffer.draw(static_cast<uint32_t>(vertices.size()), 1, 0, 0);

        // Overlay layers (Editor ImGui, etc.) draw here, inside the open render pass.
        if (m_overlayCallback) m_overlayCallback();
        
        fc.frame.commandBuffer.endRendering();

        // After rendering, transition the swapchain image to vk::ImageLayout::ePresentSrcKHR
        TransitionImageLayout(
            fc,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite,             // srcAccessMask
            {},                                                     // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,     // srcStage
            vk::PipelineStageFlagBits2::eBottomOfPipe               // dstStage
        );

        fc.frame.commandBuffer.end();

    }   // Renderer::RecordCommandBuffer()

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

    void Renderer::TransitionImageLayout(FrameContext fc, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
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
            .image = m_swapchain.GetImages()[fc.imageIndex],
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
        fc.frame.commandBuffer.pipelineBarrier2(dependency_info);

    }   // Renderer::TransitionImageLayout()

}   // NAMESPACE CRUCIBLE
