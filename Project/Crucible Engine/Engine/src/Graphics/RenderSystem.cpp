#include "RenderSystem.h"

// STL
#include <stdexcept>
#include <iostream>
#include <cstdint>
#include <cstring>      // memcpy
#include <limits>       // numeric_limits
#include <algorithm>    // clamp, stable_sort

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

        CreateUniformBuffers();

        CreateDescriptorPool();
        CreateDescriptorSets();

        CreateViewportSampler();
        CreateViewportDescriptorPool();

        // Built-in mesh so there is always something to draw / test with.
        m_cubeMesh = CreateMesh(Primitives::MakeCube());
        m_renderQueue.reserve(1024);
    }

    void Renderer::ShutDown() { m_context.ShutDown(); }

    void Renderer::Update(float dt) {

        m_time += dt;

        // Finish a few queued async mesh uploads (see CreateMeshAsync) before drawing this frame.
        ProcessPendingMeshUploads();

        // QUERY & CULL
        //   (Frustum culling would go here: m_camera.GetViewProjection() gives you the planes.)

        // POPULATE RENDER QUEUE
        //   The ECS extraction system calls Submit() before Update(). The demo scene is a stand-in.
        if (m_demoScene) { SubmitDemoScene(); }

        // SORT & BATCH
        //   Group by mesh so each mesh's vertex/index buffers are bound once per frame.
        std::stable_sort(m_renderQueue.begin(), m_renderQueue.end(),
            [](const RenderItem& a, const RenderItem& b) { return a.mesh < b.mesh; });

        // EXECUTE GRAPHICS CALLS
        DrawFrame();

        m_renderQueue.clear();

    }   // Renderer::Update()

    void Renderer::DrawFrame() {

        bool err = false;
        FrameContext fc = m_frameManager.BeginFrame(m_context, m_swapchain, err);
        if (err) { return; }

        UpdateFrameUniforms(m_frameManager.GetFrameIndex());

        // Record and submit command buffer.
        RecordCommandBuffer(fc);

        m_frameManager.EndFrame(m_context, m_swapchain, fc, m_window);

    }   // Renderer::DrawFrame()

    void Renderer::Submit(MeshHandle mesh, const glm::mat4& model) {

        m_renderQueue.push_back(RenderItem{ mesh, model });

    }   // Renderer::Submit()

    void Renderer::UpdateFrameUniforms(uint32_t frameIndex) {

        // Keep the projection in sync with the swapchain (handles window resize; skips minimised 0-height).
        const vk::Extent2D extent = m_swapchain.GetExtent();
        m_camera.SetAspect(static_cast<float>(extent.width), static_cast<float>(extent.height));

        FrameUniforms ubo{};
        ubo.view = m_camera.GetView();
        ubo.proj = m_camera.GetProjection();

        memcpy(m_uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));

    }   // Renderer::UpdateFrameUniforms()

    void Renderer::SubmitDemoScene() {

        // 7x7 grid of cubes on the XZ plane, each spinning at a slightly different phase.
        constexpr int   half    = 3;
        constexpr float spacing = 2.0f;

        for (int x = -half; x <= half; ++x) {
            for (int z = -half; z <= half; ++z) {
                const float phase = static_cast<float>(x * 7 + z * 3);
                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x * spacing, 0.0f, z * spacing));
                model = glm::rotate(model, (m_time + phase) * glm::radians(20.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::rotate(model, (m_time + phase) * glm::radians(10.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                Submit(m_cubeMesh, model);
            }
        }

    }   // Renderer::SubmitDemoScene()

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
            .depthFormat    = static_cast<VkFormat>(m_swapchain.GetDepthFormat()),
            .minImageCount  = static_cast<uint32_t>(2),
            .imageCount     = static_cast<uint32_t>(m_swapchain.GetImages().size()),
        };

        return context;

    }   // Renderer::GetImguiContext()

    vk::CommandBuffer Renderer::GetCurrentCommandBuff() const {

        return *m_frameManager.GetCurrentFrame().commandBuffer;

    }   // Renderer::GetCurrentCommandBuff()

    void Renderer::RecordCommandBuffer(FrameContext& fc) {

        const uint32_t frameIndex = m_frameManager.GetFrameIndex();

        fc.frame.commandBuffer.begin({});

        // ---- Offscreen editor viewports -------------------------------------------------------
        // Empty in a standalone Engine/Game build: this loop simply doesn't run, so the cost of
        // supporting viewports at all is the one `m_viewports.empty()` check further down.
        for (auto& [id, vp] : m_viewports) {

            vp.camera.SetAspect(static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height));
            UpdateViewportUniforms(vp, frameIndex);

            TransitionImageLayout(
                fc, vp.colorImage.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                {}, vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eColorAttachmentOutput
            );
            TransitionImageLayout(
                fc, vp.depthImage.GetImage(), vk::ImageAspectFlagBits::eDepth,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
                {}, vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests
            );

            vk::RenderingAttachmentInfo color{
                .imageView = vp.colorView,
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .clearValue = vk::ClearColorValue(0.05f, 0.05f, 0.06f, 1.0f)
            };
            vk::RenderingAttachmentInfo depth{
                .imageView = vp.depthView,
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eDontCare,
                .clearValue = vk::ClearDepthStencilValue(1.0f, 0)
            };
            vk::RenderingInfo renderingInfo{
                .renderArea = {.offset = {0, 0}, .extent = vp.extent},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &color,
                .pDepthAttachment = &depth
            };

            fc.frame.commandBuffer.beginRendering(renderingInfo);
            DrawRenderQueue(*fc.frame.commandBuffer, vp.extent, *vp.descriptorSets[frameIndex]);
            fc.frame.commandBuffer.endRendering();

            // Hand off to the Editor as a sampled texture (ImGui_ImplVulkan_AddTexture expects this layout).
            TransitionImageLayout(
                fc, vp.colorImage.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader
            );
        }

        // ---- Swapchain (window) pass -----------------------------------------------------------
        TransitionImageLayout(
            fc,
            m_swapchain.GetImages()[fc.imageIndex],
            vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            {},                                                        // srcAccessMask (no need to wait for previous operations)
            vk::AccessFlagBits2::eColorAttachmentWrite,                // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,        // srcStage
            vk::PipelineStageFlagBits2::eColorAttachmentOutput         // dstStage
        );

        // Depth content is fully discarded and re-cleared every frame, so there's nothing to
        // preserve; go straight from eUndefined to eDepthAttachmentOptimal.
        TransitionImageLayout(
            fc,
            m_swapchain.GetDepthImage(),
            vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal,
            {},
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests
        );

        vk::ClearValue              clearColor = vk::ClearColorValue(0.5f, 0.0f, 0.1f, 1.0f);
        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = m_swapchain.GetImageViews()[fc.imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor
        };

        vk::ClearValue              clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
        vk::RenderingAttachmentInfo depthAttachmentInfo = {
            .imageView = m_swapchain.GetDepthImageView(),
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,   // depth isn't needed after this frame
            .clearValue = clearDepth
        };

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = m_swapchain.GetExtent()},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo,
            .pDepthAttachment = &depthAttachmentInfo
        };

        fc.frame.commandBuffer.beginRendering(renderingInfo);

        // Only draw the scene straight to the window when nobody has registered an offscreen
        // viewport for it. In the Editor, the Scene/Game panels already drew the scene above and
        // display it themselves via ImGui::Image(); this pass then holds only the ImGui overlay.
        if (m_viewports.empty()) {
            DrawRenderQueue(*fc.frame.commandBuffer, m_swapchain.GetExtent(), *m_descriptorSets[frameIndex]);
        }

        // Overlay layers (Editor ImGui, etc.) draw here, inside the open render pass.
        if (m_overlayCallback) m_overlayCallback();
        
        fc.frame.commandBuffer.endRendering();

        // After rendering, transition the swapchain image to vk::ImageLayout::ePresentSrcKHR
        TransitionImageLayout(
            fc,
            m_swapchain.GetImages()[fc.imageIndex],
            vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite,             // srcAccessMask
            {},                                                     // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,     // srcStage
            vk::PipelineStageFlagBits2::eBottomOfPipe               // dstStage
        );
        fc.frame.commandBuffer.end();

    }   // Renderer::RecordCommandBuffer()

    void Renderer::DrawRenderQueue(vk::CommandBuffer cmd, vk::Extent2D extent, vk::DescriptorSet cameraSet) {

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetPipeline());
        cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(extent.height), static_cast<float>(extent.width), -static_cast<float>(extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));

        // Camera data (view/proj) is bound once for the whole pass.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline.GetLayout(), 0, cameraSet, nullptr);

        // Draw every submitted object. Queue is sorted by mesh, so buffers rebind only on change.
        MeshHandle boundMesh;   // starts Invalid
        for (const RenderItem& item : m_renderQueue) {

            if (!item.mesh.IsValid() || item.mesh.id >= m_meshes.size()) { continue; }
            const GpuMesh& mesh = m_meshes[item.mesh.id];
            if (mesh.indexCount == 0) { continue; }   // still uploading (CreateMeshAsync); skip for now

            if (!(item.mesh == boundMesh)) {
                cmd.bindVertexBuffers(0, *mesh.vertexBuffer.GetBuffer(), {0});
                cmd.bindIndexBuffer(*mesh.indexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);
                boundMesh = item.mesh;
            }

            // Per-object model matrix goes in a push constant (recorded into the command buffer,
            // so there are no per-frame-in-flight synchronisation concerns).
            PushConstants pc{ item.model };
            cmd.pushConstants<PushConstants>(m_pipeline.GetLayout(), vk::ShaderStageFlagBits::eVertex, 0, pc);
            cmd.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
        }

    }   // Renderer::DrawRenderQueue()

    MeshHandle Renderer::CreateMesh(const MeshData& data) {

        MeshHandle handle{ static_cast<uint32_t>(m_meshes.size()) };
        m_meshes.emplace_back();       // reserve slot (indexCount == 0 => not yet renderable)
        UploadMesh(handle, data);      // ... filled in synchronously, right here
        return handle;

    }   // Renderer::CreateMesh()

    MeshHandle Renderer::CreateMeshAsync(MeshData data) {

        MeshHandle handle{ static_cast<uint32_t>(m_meshes.size()) };
        m_meshes.emplace_back();       // reserve slot; RecordCommandBuffer skips it until uploaded
        m_pendingMeshUploads.push_back({ handle, std::move(data) });
        return handle;

    }   // Renderer::CreateMeshAsync()

    void Renderer::ProcessPendingMeshUploads() {

        for (uint32_t n = 0; n < kMaxMeshUploadsPerFrame && !m_pendingMeshUploads.empty(); ++n) {
            PendingMeshUpload pending = std::move(m_pendingMeshUploads.front());
            m_pendingMeshUploads.pop_front();
            UploadMesh(pending.handle, pending.data);
        }

    }   // Renderer::ProcessPendingMeshUploads()

    void Renderer::UploadMesh(MeshHandle handle, const MeshData& data) {

        if (data.vertices.empty() || data.indices.empty()) {
            throw std::runtime_error("[Renderer] UploadMesh: mesh has no vertices or indices");
        }

        GpuMesh mesh;

        vk::DeviceSize vbSize = sizeof(Vertex) * data.vertices.size();
        mesh.vertexBuffer.Create(m_context, vbSize,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        mesh.vertexBuffer.UploadStaging(m_context, data.vertices.data(), vbSize, m_frameManager.GetCommandPool());

        vk::DeviceSize ibSize = sizeof(uint32_t) * data.indices.size();
        mesh.indexBuffer.Create(m_context, ibSize,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        mesh.indexBuffer.UploadStaging(m_context, data.indices.data(), ibSize, m_frameManager.GetCommandPool());

        mesh.indexCount = static_cast<uint32_t>(data.indices.size());

        m_meshes[handle.id] = std::move(mesh);

    }   // Renderer::UploadMesh()

    // ------------------------------------------------------------------------------------------------------------- //
    //  VIEWPORTS (offscreen render targets for the Editor)
    // ------------------------------------------------------------------------------------------------------------- //

    ViewportHandle Renderer::CreateViewport(uint32_t width, uint32_t height) {

        if (m_viewports.size() >= kMaxViewports) {
            throw std::runtime_error("[Renderer] CreateViewport: kMaxViewports exceeded; raise the limit if you genuinely need more.");
        }

        ViewportHandle handle{ m_nextViewportId++ };
        Viewport& vp = m_viewports[handle.id];   // default-constructs in place
        CreateViewportImages(vp, width, height);
        CreateViewportUniformsAndDescriptors(vp);
        return handle;

    }   // Renderer::CreateViewport()

    void Renderer::ResizeViewport(ViewportHandle handle, uint32_t width, uint32_t height) {

        Viewport& vp = GetViewportOrThrow(handle);
        if (vp.extent.width == width && vp.extent.height == height) { return; }   // no-op: avoids rebuilding on every identical layout pass
        if (width == 0 || height == 0) { return; }                                // panel fully collapsed; keep the last valid image

        // The old images might still be read by an in-flight frame (e.g. ImGui sampling last
        // frame's texture while this frame is being recorded). Resizing is a rare, interactive-
        // only event (dragging a dock splitter), so a full wait here is the simplest correct
        // option; it is not something that happens on the per-frame hot path.
        m_context.GetDevice().waitIdle();

        // Only the images change size; the uniform buffers/descriptor sets are resolution-
        // independent (they just hold view/proj), so keep reusing the ones allocated at
        // CreateViewport time instead of re-allocating from the (fixed-capacity) descriptor pool.
        CreateViewportImages(vp, width, height);

    }   // Renderer::ResizeViewport()

    void Renderer::DestroyViewport(ViewportHandle handle) {

        auto it = m_viewports.find(handle.id);
        if (it == m_viewports.end()) { return; }

        // Same reasoning as ResizeViewport: don't tear down GPU resources a frame in flight
        // might still be using.
        m_context.GetDevice().waitIdle();
        m_viewports.erase(it);

    }   // Renderer::DestroyViewport()

    Camera& Renderer::GetViewportCamera(ViewportHandle handle) {

        return GetViewportOrThrow(handle).camera;

    }   // Renderer::GetViewportCamera()

    vk::ImageView Renderer::GetViewportColorView(ViewportHandle handle) const {

        return *GetViewportOrThrow(handle).colorView;

    }   // Renderer::GetViewportColorView()

    Renderer::Viewport& Renderer::GetViewportOrThrow(ViewportHandle handle) {

        auto it = m_viewports.find(handle.id);
        if (it == m_viewports.end()) {
            throw std::runtime_error("[Renderer] Unknown or already-destroyed ViewportHandle.");
        }
        return it->second;

    }   // Renderer::GetViewportOrThrow()

    const Renderer::Viewport& Renderer::GetViewportOrThrow(ViewportHandle handle) const {

        auto it = m_viewports.find(handle.id);
        if (it == m_viewports.end()) {
            throw std::runtime_error("[Renderer] Unknown or already-destroyed ViewportHandle.");
        }
        return it->second;

    }   // Renderer::GetViewportOrThrow()

    void Renderer::CreateViewportSampler() {

        vk::SamplerCreateInfo samplerInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .anisotropyEnable = vk::False,
            .compareEnable = vk::False,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = vk::BorderColor::eIntOpaqueBlack,
            .unnormalizedCoordinates = vk::False
        };
        m_viewportSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

    }   // Renderer::CreateViewportSampler()

    void Renderer::CreateViewportDescriptorPool() {

        // Sized up front for the worst case (kMaxViewports simultaneously alive, each with its
        // own set per frame in flight) so viewport creation never has to touch pool capacity.
        uint32_t maxSets = kMaxViewports * m_frameManager.MaxFramesInFlight();
        vk::DescriptorPoolSize poolSize{
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = maxSets };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = maxSets, .poolSizeCount = 1, .pPoolSizes = &poolSize };

        m_viewportDescriptorPool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

    }   // Renderer::CreateViewportDescriptorPool()

    void Renderer::CreateViewportImages(Viewport& vp, uint32_t width, uint32_t height) {

        vp.extent = { width, height };

        // Must match the pipeline's baked-in color/depth attachment formats exactly (dynamic
        // rendering requires this), so reuse whatever the swapchain already settled on.
        const vk::Format colorFormat = m_swapchain.GetSurfaceFormat().format;
        const vk::Format depthFormat = m_swapchain.GetDepthFormat();

        vp.colorImage.Create(m_context, width, height, colorFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::ImageViewCreateInfo colorViewInfo{
            .image = vp.colorImage.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = colorFormat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 }
        };
        vp.colorView = vk::raii::ImageView(m_context.GetDevice(), colorViewInfo);

        vp.depthImage.Create(m_context, width, height, depthFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::ImageViewCreateInfo depthViewInfo{
            .image = vp.depthImage.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = depthFormat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1 }
        };
        vp.depthView = vk::raii::ImageView(m_context.GetDevice(), depthViewInfo);

    }   // Renderer::CreateViewportImages()

    void Renderer::CreateViewportUniformsAndDescriptors(Viewport& vp) {

        uint32_t maxFrames = m_frameManager.MaxFramesInFlight();
        for (uint32_t i = 0; i < maxFrames; ++i) {
            vp.uniformBuffers.emplace_back();
            vp.uniformBuffers.back().Create(m_context, sizeof(FrameUniforms),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            vp.uniformBuffersMapped.emplace_back(vp.uniformBuffers.back().GetMemory().mapMemory(0, sizeof(FrameUniforms)));
        }

        std::vector<vk::DescriptorSetLayout> layouts(maxFrames, *m_pipeline.GetDescriptorSetLayout());
        vk::DescriptorSetAllocateInfo allocInfo{
            .descriptorPool     = m_viewportDescriptorPool,
            .descriptorSetCount = maxFrames,
            .pSetLayouts        = layouts.data()
        };
        vp.descriptorSets = m_context.GetDevice().allocateDescriptorSets(allocInfo);

        for (uint32_t i = 0; i < maxFrames; ++i) {
            vk::DescriptorBufferInfo bufferInfo{ .buffer = vp.uniformBuffers[i].GetBuffer(), .offset = 0, .range = sizeof(FrameUniforms) };
            vk::WriteDescriptorSet   write{
                .dstSet = vp.descriptorSets[i], .dstBinding = 0, .dstArrayElement = 0,
                .descriptorCount = 1, .descriptorType = vk::DescriptorType::eUniformBuffer, .pBufferInfo = &bufferInfo };
            m_context.GetDevice().updateDescriptorSets(write, {});
        }

    }   // Renderer::CreateViewportUniformsAndDescriptors()

    void Renderer::UpdateViewportUniforms(Viewport& vp, uint32_t frameIndex) {

        FrameUniforms ubo{};
        ubo.view = vp.camera.GetView();
        ubo.proj = vp.camera.GetProjection();
        memcpy(vp.uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));

    }   // Renderer::UpdateViewportUniforms()

    void Renderer::CreateUniformBuffers() {

        for (uint32_t i = 0; i < m_frameManager.MaxFramesInFlight(); ++i) {
            vk::DeviceSize bufferSize = sizeof(FrameUniforms);
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
                .range  = sizeof(FrameUniforms) };

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

    void Renderer::TransitionImageLayout(FrameContext fc, vk::Image image, vk::ImageAspectFlags aspectMask,
        vk::ImageLayout old_layout, vk::ImageLayout new_layout,
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
            .image = image,
            .subresourceRange = {
                   .aspectMask = aspectMask,
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
