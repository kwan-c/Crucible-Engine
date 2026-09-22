#include "RenderSystem.h"

// STL
#include <stdexcept>
#include <iostream>
#include <cstdint>
#include <limits>   // numeric_limits
#include <algorithm>    // clamp
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace Crucible {

    Renderer::Renderer(WindowManager& window) 
        : m_window{ window }, m_context{ m_window }, m_swapchain{ m_context, m_window } {

        m_pipeline.CreateDescriptorSetLayout(m_context);
        m_pipeline.CreatePipeline(m_context, m_swapchain);
        m_frameManager.Init(m_context, m_swapchain);

        CreateTextureImage("../../../Assets/Textures/Orange_Milk.jpg");

        CreateVertexBuffer();
        CreateIndexBuffer();
        CreateUniformBuffers();

        CreateDescriptorPool();
        CreateDescriptorSets();
    }

    void Renderer::ShutDown() { m_context.ShutDown(); }

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

        UpdateUniformBuffer(m_frameManager.GetFrameIndex());

        // Record and submit command buffer.
        RecordCommandBuffer(fc);

        m_frameManager.EndFrame(m_context, m_swapchain, fc, m_window);

    }   // Renderer::DrawFrame()

    void Renderer::UpdateUniformBuffer(uint32_t frameIndex) {

        static auto startTime   = std::chrono::high_resolution_clock::now();
        auto currentTime        = std::chrono::high_resolution_clock::now();
        float time              = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo{};
        ubo.model = rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.model = rotate(ubo.model, time * glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubo.view = lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(m_swapchain.GetExtent().width) / static_cast<float>(m_swapchain.GetExtent().height), 0.1f, 10.0f);

        memcpy(m_uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));

    }   // Renderer::UpdateUniformBuffer()

    // ------------------------------------------------------------------------------------------------------------- //
    //  VULKAN SET-UP FUNCTIONS
    // ------------------------------------------------------------------------------------------------------------- //

    ImGuiVulkanContext Renderer::GetImguiContext() const {

        ImGuiVulkanContext context = {
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

    vk::CommandBuffer Renderer::GetCurrentCommandBuff() const {

        return *m_frameManager.GetCurrentFrame().commandBuffer;

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

        vk::ClearValue              clearColor = vk::ClearColorValue(0.5f, 0.0f, 0.1f, 1.0f);
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
        fc.frame.commandBuffer.setViewport(0, vk::Viewport(0.0f, static_cast<float>(m_swapchain.GetExtent().height), static_cast<float>(m_swapchain.GetExtent().width), -static_cast<float>(m_swapchain.GetExtent().height), 0.0f, 1.0f));
        fc.frame.commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_swapchain.GetExtent()));
        
        fc.frame.commandBuffer.bindVertexBuffers(0, *m_vertexBuffer.GetBuffer(), {0});
        fc.frame.commandBuffer.bindIndexBuffer(*m_indexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);
        fc.frame.commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline.GetLayout(), 0, *m_descriptorSets[m_frameManager.GetFrameIndex()], nullptr);
        fc.frame.commandBuffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

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

        vk::DeviceSize bufferSize = sizeof(vertices[0])* vertices.size();
        m_vertexBuffer.Create(m_context, bufferSize,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, 
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_vertexBuffer.UploadStaging(m_context, vertices.data(), bufferSize, m_frameManager.GetCommandPool());

    }   // Renderer::CreateVertexBuffer()

    void Renderer::CreateIndexBuffer() {

        vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();
        m_indexBuffer.Create(m_context, bufferSize, 
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, 
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_indexBuffer.UploadStaging(m_context, indices.data(), bufferSize, m_frameManager.GetCommandPool());

    }   // Renderer::CreateIndexBuffer()

    void Renderer::CreateUniformBuffers() {

        for (uint32_t i = 0; i < m_frameManager.MaxFramesInFlight(); ++i) {
            vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
            m_uniformBuffers.emplace_back();
            m_uniformBuffers.back().Create(m_context, bufferSize,
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_uniformBuffersMapped.emplace_back(m_uniformBuffers.back().GetMemory().mapMemory(0, bufferSize));
        }

    }   // Renderer::CreateUniformBuffers()

    void Renderer::CreateDescriptorPool() {

        uint32_t maxFrames = m_frameManager.MaxFramesInFlight();
        vk::DescriptorPoolSize poolSize{ 
            .type = vk::DescriptorType::eUniformBuffer, 
            .descriptorCount = maxFrames };
        vk::DescriptorPoolCreateInfo poolInfo{ 
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 
            .maxSets = maxFrames, .poolSizeCount = 1, .pPoolSizes = &poolSize };

        m_descriptorPool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

    }   // Renderer::CreateDescriptorPool()

    void Renderer::CreateDescriptorSets() {

        uint32_t maxFrames = m_frameManager.MaxFramesInFlight();
        std::vector<vk::DescriptorSetLayout> layouts(maxFrames, *m_pipeline.GetDescriptorSetLayout());
        vk::DescriptorSetAllocateInfo allocInfo { 
            .descriptorPool     = m_descriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
            .pSetLayouts        = layouts.data() 
        };
        m_descriptorSets = m_context.GetDevice().allocateDescriptorSets(allocInfo);

        for (size_t i = 0; i < maxFrames; i++) {

            vk::DescriptorBufferInfo bufferInfo{ 
                .buffer = m_uniformBuffers[i].GetBuffer(),
                .offset = 0, 
                .range  = sizeof(UniformBufferObject) };

            vk::WriteDescriptorSet   descriptorWrite{ 
                .dstSet             = m_descriptorSets[i],
                .dstBinding         = 0,
                .dstArrayElement    = 0,
                .descriptorCount    = 1,
                .descriptorType     = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo        = &bufferInfo };

            m_context.GetDevice().updateDescriptorSets(descriptorWrite, {});
        }

    }   // Renderer::CreateDescriptorSets()

    void Renderer::CreateTextureImage(const std::string& path) {

        int texWidth{}, texHeight{}, texChannels{};
        stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        
        vk::DeviceSize imageSize = texWidth * texHeight * 4;
        if (!pixels)
        {
            throw std::runtime_error("[Renderer] Failed to load texture image: (" + path + ")");
        }

        VulkanBuffer stagingBuff;
        stagingBuff.Create(m_context, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        void* data = stagingBuff.GetMemory().mapMemory(0, imageSize);
        memcpy(data, pixels, imageSize);
        stagingBuff.GetMemory().unmapMemory();

        stbi_image_free(pixels);

        m_image.Create(m_context, texWidth, texHeight, 
            vk::Format::eR8G8B8A8Srgb,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        vk::raii::CommandBuffer commandBuffer = BeginSingleTimeCommand();

        TransitionImageLayout_I(commandBuffer, m_image.GetImage(), vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
        CopyBufferToImage(commandBuffer, stagingBuff.GetBuffer(), m_image.GetImage(), static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
        TransitionImageLayout_I(commandBuffer, m_image.GetImage(), vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
        
        EndSingleTimeCommand(std::move(commandBuffer));

    }   // Renderer::CreateTextureImage()

    void Renderer::CopyBufferToImage(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height) {

        vk::BufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, 1} 
        };
        commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);

    }   // Renderer::CopyBufferToImage()


    vk::raii::CommandBuffer Renderer::BeginSingleTimeCommand() {

        vk::CommandBufferAllocateInfo allocInfo{ .commandPool = m_frameManager.GetCommandPool(), .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1};
        vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(m_context.GetDevice(), allocInfo).front());

        vk::CommandBufferBeginInfo beginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
        commandBuffer.begin(beginInfo);

        return std::move(commandBuffer);

    }   // Renderer::BeginSingleTimeCommand()

    void Renderer::EndSingleTimeCommand(vk::raii::CommandBuffer&& cmdBuff) {

        cmdBuff.end();

        vk::SubmitInfo submitInfo{ .commandBufferCount = 1, .pCommandBuffers = &*cmdBuff };
        m_context.GetGraphicsQueue().submit(submitInfo, nullptr);
        m_context.GetGraphicsQueue().waitIdle();

    }   // Renderer::EndSingleTimeCommand()

    void Renderer::TransitionImageLayout_I(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Image& image,
        vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {

        vk::ImageMemoryBarrier barrier{ .oldLayout = oldLayout,
                                       .newLayout = newLayout,
                                       .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                                       .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                                       .image = image,
                                       .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1} };

        vk::PipelineStageFlags sourceStage;
        vk::PipelineStageFlags destinationStage;

        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
        {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            sourceStage = vk::PipelineStageFlagBits::eTransfer;
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }
        commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, {}, {}, barrier);

    }   // Renderer::TransitionImageLayout_I()

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
