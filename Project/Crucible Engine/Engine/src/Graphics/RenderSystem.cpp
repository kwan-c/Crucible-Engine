/*************************************************************************
* \file:         RenderSystem.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                See RenderSystem.h for what Renderer owns and its full
*                public API documentation.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "RenderSystem.h"

// STL
#include <stdexcept>
#include <iostream>
#include <cstdint>
#include <cstring>      // memcpy
#include <limits>       // numeric_limits
#include <algorithm>    // clamp, stable_sort
#include <filesystem>   // path::stem - fallback mesh name when a .glb doesn't set mesh.name
#include <array>
#include <chrono>
#include <cmath>
#include <memory>       // unique_ptr (stb_image pixel ownership in CreateCubemap)

#include <glm/gtc/packing.hpp>   // packHalf4x16 - RGBA16F cubemap texels

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Primitives.h"             // Primitives::MakeCube() and the rest of the whitebox set
#include "Diagnostics/Logger.h"

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace Crucible {

    Renderer::Renderer(WindowManager& window) 
        : m_window{ window }, m_context{ m_window }, m_swapchain{ m_context, m_window } {

        m_pipeline.CreateDescriptorSetLayout(m_context);
        m_pipeline.CreateMaterialDescriptorSetLayout(m_context);
        m_pipeline.CreateSkyboxDescriptorSetLayout(m_context);
        m_pipeline.CreatePipeline(m_context, m_swapchain);
        m_pipeline.CreateSkyboxPipeline(m_context, m_swapchain);
        m_pipeline.CreateDebugLinePipelines(m_context, m_swapchain);
        m_pipeline.CreateShadowPipeline(m_context, kShadowFormat);
        m_pipeline.CreateOutlinePipelines(m_context, m_swapchain);
        m_pipeline.CreateParticlePipeline(m_context, m_swapchain);
        m_pipeline.CreateDepthPrepassPipeline(m_context);
        m_pipeline.CreatePostPipelines(m_context, m_swapchain);
        m_frameManager.Init(m_context, m_swapchain);
        m_uploads.Init(m_context, m_frameManager.GetCommandPool());

        CreateUniformBuffers();
        // Before CreateDescriptorSets(): set 0's shared bindings point at these.
        CreateLightBuffers();
        CreateInstanceBuffers();
        CreateShadowResources();
        CreateClusterBuffers();
        CreateParticleResources();

        CreateDescriptorPool();
        CreateDescriptorSets();

        CreateViewportSampler();
        CreateViewportDescriptorPool();
        CreatePostDescriptorPool();

        CreateMaterialSampler();
        CreateMaterialDescriptorPool();
        CreateDefaultTextures();
        m_defaultMaterial = CreateMaterial(MaterialData{});   // Invalid textures -> both placeholders, neutral factors
        GetParticleTextureSet(m_defaultWhiteTexture);         // the particle pipeline's fallback texture set

        CreateSkyboxSampler();
        CreateCubemapDescriptorPool();
        CreateDefaultSkybox();
        m_defaultEnvironment.skybox = m_defaultSkybox;
        m_environment = m_defaultEnvironment;
        m_lightQueue.reserve(kMaxLights);

        CreatePrimitiveMeshes();
        CreateTimestampPool();

        m_renderQueue.reserve(1024);
    }

    void Renderer::ShutDown() { m_context.ShutDown(); }

    void Renderer::Update(float dt) {

        // Shader clock (FrameUniforms::time). A float holds seconds accurately enough for
        // animation for many hours of play; wrap it if sessions ever get longer than that.
        m_deltaTime = std::max(dt, 0.0f);
        m_time     += m_deltaTime;

        UpdateStressTest();

        // Finish a few queued async mesh uploads (see CreateMeshAsync) before drawing this frame.
        ProcessPendingMeshUploads();

        // POPULATE
        //   Persistent proxies were updated by RenderExtractSystem (SetRenderProxy) during the ECS
        //   update; immediate Submit() items were queued by whoever called Submit().

        // Proxies whose mesh was still uploading last frame: try to get their bounds now.
        if (!m_proxiesAwaitingBounds.empty()) {
            std::vector<uint32_t> pending;
            pending.swap(m_proxiesAwaitingBounds);
            for (uint32_t id : pending) {
                if (id < m_proxies.size() && m_proxies[id].alive && m_proxies[id].awaitingBounds) { RefreshProxyBounds(id); }
            }
        }

        if (DebugDraw::ShouldDraw(DebugCategory::Bounds)) { DrawProxyBoundsDebug(); }

        // CULL, SORT & DRAW
        //   Per view, inside RecordCommandBuffer: each camera gets its own visible list
        //   (BuildVisibleItems), so the Scene and Game views never cull with each other's camera.
        DrawFrame();

        // Everything submitted for this frame is consumed; lights and environment are per-frame
        // state exactly like the render queue (see SubmitLight/SetEnvironment).
        m_renderQueue.clear();
        m_particleQueue.clear();
        m_particleSubmits.clear();
        m_lightQueue.clear();
        m_shadowLightIndex = -1;
        m_environment = m_defaultEnvironment;

        // Age/clear debug lines, and tell DebugDraw whether anything will show them next frame -
        // when nothing does, every DebugDraw call becomes a cheap early-out.
        bool debugVisible = m_viewports.empty() && m_debugInWindow;
        for (const auto& [id, vp] : m_viewports) { debugVisible = debugVisible || vp.debugDraw; }
        DebugDraw::EndFrame(dt, debugVisible);

        // Lets objects that have stopped moving settle back into the spatial index's tree.
        m_spatial.EndFrame();

    }   // Renderer::Update()

    void Renderer::DrawFrame() {

        // Timings (RenderFrameStats): each value is blended into a running average.
        const auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
        const auto smooth = [](float& value, double seconds) { value += (static_cast<float>(seconds * 1000.0) - value) * 0.1f; };
        const double tStart = now();
        if (m_lastFrameStart > 0.0) { smooth(m_frameStats.frameMs, tStart - m_lastFrameStart); }
        m_lastFrameStart = tStart;

        bool err = false;
        FrameContext fc = m_frameManager.BeginFrame(m_context, m_swapchain, err);
        if (err) { return; }

        const uint32_t frameIndex = m_frameManager.GetFrameIndex();

        // BeginFrame() waited for the frame that last used this index, so its timestamps are in.
        ReadGpuTimings(frameIndex);
        const double tPrepare = now();
        smooth(m_frameStats.cpuWaitMs, tPrepare - tStart);

        // BeginFrame() has waited on this frame index's fence, so the GPU is done with the staging
        // buffers of the uploads recorded the last time this index was used: free them.
        m_uploads.ReleaseFrame(frameIndex);

        ApplyMsaaSetting();

        // Lights first: every view's uniforms read the light count and shadow light this picks.
        UpdateLightBuffer(frameIndex);
        UpdateDebugBuffer(frameIndex);

        // PREPARE: cull and batch every view (main pass, shadow cascades, selection mask) before
        // recording anything, so the instance buffer's final size is known up front and it never
        // has to grow while commands already reference it.
        m_instanceScratch.clear();
        m_particleScratch.clear();
        m_clusterGridScratch.clear();
        m_clusterIndexScratch.clear();
        m_preparedViews = 0;
        m_windowDraws.Clear();
        if (m_viewports.empty()) {
            // No camera entity in the scene: nothing to look through, so the window shows only
            // its clear colour (plus any overlay).
            if (m_hasMainCamera) {
                const vk::Extent2D extent = m_swapchain.GetExtent();
                // The window's HDR image and bloom chain follow the swapchain's size.
                if (m_windowPost.extent != extent && extent.width > 0 && extent.height > 0) {
                    m_context.GetDevice().waitIdle();
                    CreatePostTargets(m_windowPost, extent.width, extent.height);
                }
                PrepareView(m_camera, extent, m_windowCullStats, m_windowDraws, false, m_uniformBuffersMapped[frameIndex]);
                WriteViewTextures(*m_descriptorSets[frameIndex], m_windowPost);
            } else {
                m_windowCullStats = RenderCullStats{};
            }
        } else {
            for (auto& [id, vp] : m_viewports) {
                if (!vp.active) { vp.draws.Clear(); continue; }
                PrepareView(vp.camera, vp.extent, vp.cullStats, vp.draws, vp.outline, vp.uniformBuffersMapped[frameIndex]);
                WriteViewTextures(*vp.descriptorSets[frameIndex], vp.post);
            }
        }
        const double tUpload = now();
        smooth(m_frameStats.cpuPrepareMs, tUpload - tPrepare);

        UploadInstances(frameIndex);
        UploadParticles(frameIndex);
        UploadClusters(frameIndex);
        const double tRecord = now();
        smooth(m_frameStats.cpuUploadMs, tRecord - tUpload);

        // Record and submit command buffer.
        RecordCommandBuffer(fc);
        const double tSubmit = now();
        smooth(m_frameStats.cpuRecordMs, tSubmit - tRecord);

        m_frameManager.EndFrame(m_context, m_swapchain, fc, m_window);
        smooth(m_frameStats.cpuSubmitMs, now() - tSubmit);

    }   // Renderer::DrawFrame()

    void Renderer::Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model, const glm::vec4& params) {

        m_renderQueue.push_back(RenderItem{ mesh, material, model, params });

    }   // Renderer::Submit()

    void Renderer::CreatePrimitiveMeshes() {

        m_primitiveMeshes.emplace_back(CreateMesh(Primitives::MakeCube(), "Cube"));
        m_primitiveMeshes.emplace_back(CreateMesh(Primitives::MakeCapsule(), "Capsule"));
        m_primitiveMeshes.emplace_back(CreateMesh(Primitives::MakeCone(), "Cone"));
        m_primitiveMeshes.emplace_back(CreateMesh(Primitives::MakeCylinder(), "Cylinder"));
        m_primitiveMeshes.emplace_back(CreateMesh(Primitives::MakeDisc(), "Disc"));
        m_primitiveMeshes.emplace_back(CreateMesh(Primitives::MakePlane(), "Plane"));
        m_primitiveMeshes.emplace_back(CreateMesh(Primitives::MakePyramid(), "Pyramid"));
        m_primitiveMeshes.emplace_back(CreateMesh(Primitives::MakeSphere(), "Sphere"));

    }   // Renderer::CreatePrimitiveMeshes()

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

        // GPU timings: clear this frame's block of timestamps, then mark the start of each pass.
        m_markFrame = frameIndex;
        if (*m_timestampPool) {
            m_gpuMarks[frameIndex].clear();
            fc.frame.commandBuffer.resetQueryPool(*m_timestampPool, frameIndex * kMaxGpuMarks, kMaxGpuMarks);
        }
        MarkGpu(*fc.frame.commandBuffer, GpuSection::Uploads);

        // Mesh/texture/cubemap copies queued since the last frame go first, ahead of every pass,
        // so anything created before this point is filled by the time a draw below reads it.
        m_uploads.Record(*fc.frame.commandBuffer, frameIndex);

        // ---- Offscreen editor viewports -------------------------------------------------------
        // Empty in a standalone Engine/Game build: this loop simply doesn't run, so the cost of
        // supporting viewports at all is the one `m_viewports.empty()` check further down.
        for (auto& [id, vp] : m_viewports) {

            if (!vp.active) { continue; }   // paused (SetViewportActive): keeps its last image

            const vk::DescriptorSet frameSet = *vp.descriptorSets[frameIndex];

            // Passes that feed this view's main pass: its shadow cascades into the shared shadow
            // map, and the selected object into its outline mask.
            MarkGpu(*fc.frame.commandBuffer, GpuSection::Shadows);
            DrawShadowCascades(fc, frameSet, vp.draws);
            if (vp.outline && !vp.draws.outline.empty()) {
                MarkGpu(*fc.frame.commandBuffer, GpuSection::Outline);
                DrawOutlineMask(fc, vp, frameSet);
            }

            // The scene itself: meshes, sky and particles into this view's HDR image, then bloom.
            MarkGpu(*fc.frame.commandBuffer, GpuSection::AmbientOcclusion);
            DrawAmbientOcclusion(fc, vp.post, frameSet, vp.draws);
            MarkGpu(*fc.frame.commandBuffer, GpuSection::Scene);
            DrawSceneHdr(fc, vp.post, vp.depthImage.GetImage(), *vp.depthView, vp.extent, frameSet, vp.draws);   // marks Bloom itself
            MarkGpu(*fc.frame.commandBuffer, GpuSection::Final);

            // The final pass: tonemap the HDR image into the viewport's (sRGB) colour image, then
            // the debug lines on top - still depth-tested against the scene's depth.
            TransitionImageLayout(
                fc, vp.colorImage.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                {}, vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eColorAttachmentOutput
            );

            vk::RenderingAttachmentInfo color{
                .imageView = vp.colorView,
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eDontCare,   // the tonemap triangle covers every pixel
                .storeOp = vk::AttachmentStoreOp::eStore
            };
            vk::RenderingAttachmentInfo depth{
                .imageView = vp.depthView,
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eLoad,
                .storeOp = vk::AttachmentStoreOp::eDontCare
            };
            vk::RenderingInfo renderingInfo{
                .renderArea = {.offset = {0, 0}, .extent = vp.extent},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &color,
                .pDepthAttachment = &depth
            };

            fc.frame.commandBuffer.beginRendering(renderingInfo);
            DrawTonemap(*fc.frame.commandBuffer, vp.post, vp.extent);
            if (vp.debugDraw) { DrawDebugLines(*fc.frame.commandBuffer, vp.extent, frameSet); }
            fc.frame.commandBuffer.endRendering();

            if (vp.outline && !vp.draws.outline.empty()) { DrawOutlineComposite(fc, vp); }

            // Hand off to the Editor as a sampled texture (ImGui_ImplVulkan_AddTexture expects this layout).
            TransitionImageLayout(
                fc, vp.colorImage.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader
            );
        }

        // ---- Swapchain (window) pass -----------------------------------------------------------
        // The window's shadow cascades (only drawn when no viewports exist - see below).
        // Standalone build with a camera: the scene goes through the same HDR pass + bloom as a
        // viewport, using the swapchain's depth image; the swapchain pass below then tonemaps it.
        const bool windowScene = m_viewports.empty() && m_hasMainCamera && *m_windowPost.hdrView;
        if (windowScene) {
            MarkGpu(*fc.frame.commandBuffer, GpuSection::Shadows);
            DrawShadowCascades(fc, *m_descriptorSets[frameIndex], m_windowDraws);
            MarkGpu(*fc.frame.commandBuffer, GpuSection::AmbientOcclusion);
            DrawAmbientOcclusion(fc, m_windowPost, *m_descriptorSets[frameIndex], m_windowDraws);
            MarkGpu(*fc.frame.commandBuffer, GpuSection::Scene);
            DrawSceneHdr(fc, m_windowPost, m_swapchain.GetDepthImage(), m_swapchain.GetDepthImageView(),
                         m_swapchain.GetExtent(), *m_descriptorSets[frameIndex], m_windowDraws);
        }

        MarkGpu(*fc.frame.commandBuffer, GpuSection::Interface);

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

        // With no scene drawn above, depth is discarded and re-cleared: go straight from eUndefined
        // to eDepthAttachmentOptimal. (With a scene, DrawSceneHdr already left it ready to load.)
        if (!windowScene) TransitionImageLayout(
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
            .loadOp = windowScene ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
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
        if (windowScene) {
            DrawTonemap(*fc.frame.commandBuffer, m_windowPost, m_swapchain.GetExtent());
            if (m_debugInWindow) { DrawDebugLines(*fc.frame.commandBuffer, m_swapchain.GetExtent(), *m_descriptorSets[frameIndex]); }
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
        MarkGpu(*fc.frame.commandBuffer, GpuSection::End);
        fc.frame.commandBuffer.end();

    }   // Renderer::RecordCommandBuffer()

    void Renderer::DrawRenderQueue(vk::CommandBuffer cmd, vk::Extent2D extent, vk::DescriptorSet cameraSet, const ViewDrawList& draws) {

        const std::vector<DrawBatch>& batches = draws.main;

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetPipeline(m_drawSamples));
        cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(extent.height), static_cast<float>(extent.width), -static_cast<float>(extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));

        // Per-view data (camera, lights, instances, shadow map) is bound once for the whole pass.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline.GetLayout(), 0, cameraSet, nullptr);

        // One instanced draw per batch (PrepareView/AppendBatches). Batches are in (material, mesh)
        // order, so the material set (set 1) and the mesh's buffers rebind only on change.
        MeshHandle     boundMesh;       // starts Invalid
        MaterialHandle boundMaterial;   // starts Invalid
        for (const DrawBatch& batch : batches) {

            const GpuMesh&     mesh = m_meshes[batch.mesh.id];          // validated in AppendBatches
            const GpuMaterial& mat  = m_materials[batch.material.id];   // default-substituted there too

            if (!(batch.material == boundMaterial)) {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline.GetLayout(), 1, *mat.descriptorSet, nullptr);
                // The material's scalar factors, recorded into the command buffer (so an Inspector
                // slider change needs no GPU synchronization). Constant across the batch.
                const PushConstants pc{ mat.data.factors };
                cmd.pushConstants<PushConstants>(m_pipeline.GetLayout(), vk::ShaderStageFlagBits::eFragment, 0, pc);
                boundMaterial = batch.material;
            }

            if (!(batch.mesh == boundMesh)) {
                cmd.bindVertexBuffers(0, *mesh.vertexBuffer.GetBuffer(), {0});
                cmd.bindIndexBuffer(*mesh.indexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);
                boundMesh = batch.mesh;
            }

            // SV_VulkanInstanceID = firstInstance + i, which indexes the instance buffer directly.
            cmd.drawIndexed(mesh.indexCount, batch.instanceCount, 0, 0, batch.firstInstance);
        }

        // Sky last: it's depth-tested against everything drawn above, so only uncovered pixels
        // pay for it. Viewport/scissor are dynamic state and stay set across the pipeline switch.
        DrawSkybox(cmd, cameraSet);

        // Particles after everything opaque and the sky: they blend over what is behind them.
        DrawParticles(cmd, cameraSet, draws.particles);

    }   // Renderer::DrawRenderQueue()

    MeshHandle Renderer::CreateMesh(const MeshData& data, const std::string& debugName) {

        MeshHandle handle{ static_cast<uint32_t>(m_meshes.size()) };
        m_meshes.emplace_back();       // reserve slot (indexCount == 0 => not yet renderable)
        UploadMesh(handle, data, debugName);      // ... buffers created and their copies queued, right here
        return handle;

    }   // Renderer::CreateMesh()

    MeshHandle Renderer::CreateMeshAsync(MeshData data, const std::string& debugName) {

        MeshHandle handle{ static_cast<uint32_t>(m_meshes.size()) };
        m_meshes.emplace_back();       // reserve slot; RecordCommandBuffer skips it until uploaded
        m_pendingMeshUploads.push_back({ handle, std::move(data), debugName });
        return handle;

    }   // Renderer::CreateMeshAsync()

    void Renderer::ProcessPendingMeshUploads() {

        for (uint32_t n = 0; n < kMaxMeshUploadsPerFrame && !m_pendingMeshUploads.empty(); ++n) {
            PendingMeshUpload pending = std::move(m_pendingMeshUploads.front());
            m_pendingMeshUploads.pop_front();
            UploadMesh(pending.handle, pending.data, pending.debugName);
        }

    }   // Renderer::ProcessPendingMeshUploads()

    void Renderer::UploadMesh(MeshHandle handle, const MeshData& data, const std::string& debugName) {

        if (data.vertices.empty() || data.indices.empty()) {
            throw std::runtime_error("[Renderer] UploadMesh: mesh has no vertices or indices");
        }

        GpuMesh mesh;
        mesh.debugName = debugName.empty() ? ("Mesh " + std::to_string(handle.id)) : debugName;

        vk::DeviceSize vbSize = sizeof(Vertex) * data.vertices.size();
        mesh.vertexBuffer.Create(m_context, vbSize,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_uploads.QueueBuffer(*mesh.vertexBuffer.GetBuffer(), data.vertices.data(), vbSize);

        vk::DeviceSize ibSize = sizeof(uint32_t) * data.indices.size();
        mesh.indexBuffer.Create(m_context, ibSize,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_uploads.QueueBuffer(*mesh.indexBuffer.GetBuffer(), data.indices.data(), ibSize);

        mesh.indexCount = static_cast<uint32_t>(data.indices.size());

        mesh.boundsMin = mesh.boundsMax = data.vertices.front().pos;
        mesh.cpuPositions.reserve(data.vertices.size());
        for (const Vertex& v : data.vertices) {
            mesh.boundsMin = glm::min(mesh.boundsMin, v.pos);
            mesh.boundsMax = glm::max(mesh.boundsMax, v.pos);
            mesh.cpuPositions.push_back(v.pos);
        }
        mesh.cpuIndices = data.indices;

        m_meshes[handle.id] = std::move(mesh);

    }   // Renderer::UploadMesh()

    bool Renderer::GetMeshBounds(MeshHandle handle, glm::vec3& outMin, glm::vec3& outMax) const {

        if (!handle.IsValid() || handle.id >= m_meshes.size() || m_meshes[handle.id].indexCount == 0) { return false; }
        outMin = m_meshes[handle.id].boundsMin;
        outMax = m_meshes[handle.id].boundsMax;
        return true;

    }   // Renderer::GetMeshBounds()

    const std::string& Renderer::GetMeshName(MeshHandle handle) const {

        static const std::string kUnknown = "(unknown mesh)";
        if (!handle.IsValid() || handle.id >= m_meshes.size()) { return kUnknown; }
        return m_meshes[handle.id].debugName;

    }   // Renderer::GetMeshName()

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

    void Renderer::SetViewportDebugDraw(ViewportHandle handle, bool enabled) {

        GetViewportOrThrow(handle).debugDraw = enabled;

    }   // Renderer::SetViewportDebugDraw()

    void Renderer::SetViewportOutline(ViewportHandle handle, bool enabled) {

        GetViewportOrThrow(handle).outline = enabled;

    }   // Renderer::SetViewportOutline()

    void Renderer::SetViewportActive(ViewportHandle handle, bool active) {

        GetViewportOrThrow(handle).active = active;

    }   // Renderer::SetViewportActive()

    void Renderer::SetOutlinedObject(std::uint64_t userData) {

        if (userData == m_outlineUserData) { return; }
        m_outlineUserData = userData;
        m_outlineProxy    = 0xFFFFFFFFu;   // re-resolved lazily (ResolveOutlineProxy)

    }   // Renderer::SetOutlinedObject()

    void Renderer::SetOutlineStyle(const glm::vec4& color, float thicknessPixels) {

        m_outlineStyle.color     = color;
        m_outlineStyle.thickness = glm::clamp(thicknessPixels, 1.0f, 3.0f);

    }   // Renderer::SetOutlineStyle()

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

        // The same without filtering, for reading depth images (which not every GPU can filter).
        samplerInfo.magFilter  = vk::Filter::eNearest;
        samplerInfo.minFilter  = vk::Filter::eNearest;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        m_pointSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

    }   // Renderer::CreateViewportSampler()

    void Renderer::CreateViewportDescriptorPool() {

        // Sized up front for the worst case (kMaxViewports simultaneously alive, each with its
        // own set per frame in flight) so viewport creation never has to touch pool capacity.
        uint32_t maxSets = kMaxViewports * m_frameManager.MaxFramesInFlight();
        // Each set 0 = FrameUniforms UBO (binding 0), five storage buffers (lights 1, instances 2,
        // particles 5, light-cluster grid 6 and indices 7), shadow map image + comparison sampler (3, 4).
        // ... plus two textures: the environment map (8) and the view's ambient occlusion (9).
        std::array<vk::DescriptorPoolSize, 5> poolSizes{ {
            { .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = maxSets * 2 },
            { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = maxSets },
            { .type = vk::DescriptorType::eStorageBuffer, .descriptorCount = maxSets * 5 },
            { .type = vk::DescriptorType::eSampledImage,  .descriptorCount = maxSets },
            { .type = vk::DescriptorType::eSampler,       .descriptorCount = maxSets }
        } };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = maxSets,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()), .pPoolSizes = poolSizes.data() };

        m_viewportDescriptorPool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

        // One outline-composite set per viewport (binding 0 = its mask image).
        vk::DescriptorPoolSize outlineSize{ .type = vk::DescriptorType::eSampledImage, .descriptorCount = kMaxViewports };
        vk::DescriptorPoolCreateInfo outlinePoolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = kMaxViewports,
            .poolSizeCount = 1, .pPoolSizes = &outlineSize };
        m_outlineDescriptorPool = vk::raii::DescriptorPool(m_context.GetDevice(), outlinePoolInfo);

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

        // Selection-outline mask, same size (cheap: one byte per pixel).
        vp.maskImage.Create(m_context, width, height, VulkanPipeline::kOutlineMaskFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::ImageViewCreateInfo maskViewInfo{
            .image = vp.maskImage.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = VulkanPipeline::kOutlineMaskFormat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 }
        };
        vp.maskView = vk::raii::ImageView(m_context.GetDevice(), maskViewInfo);
        WriteOutlineDescriptor(vp);   // no-op on first creation (the set doesn't exist yet)

        // The HDR image the scene is actually drawn into, and its bloom chain.
        CreatePostTargets(vp.post, width, height);

    }   // Renderer::CreateViewportImages()

    void Renderer::CreateViewportUniformsAndDescriptors(Viewport& vp) {

        uint32_t maxFrames = m_frameManager.MaxFramesInFlight();
        for (uint32_t i = 0; i < maxFrames; ++i) {
            vp.uniformBuffers.emplace_back();
            vp.uniformBuffers.back().Create(m_context, sizeof(FrameUniforms),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            vp.uniformBuffersMapped.emplace_back(vp.uniformBuffers.back().Map());
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

        // Bindings 1-4: the SAME per-frame light/instance buffers and shadow map the window's
        // own sets use.
        WriteSharedFrameDescriptors(vp.descriptorSets);

        // The outline composite's set (binding 0 = this viewport's mask).
        vk::DescriptorSetLayout outlineLayout = *m_pipeline.GetOutlineDescriptorSetLayout();
        vk::DescriptorSetAllocateInfo outlineAlloc{
            .descriptorPool     = m_outlineDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &outlineLayout
        };
        vp.outlineSet = std::move(m_context.GetDevice().allocateDescriptorSets(outlineAlloc).front());
        WriteOutlineDescriptor(vp);

    }   // Renderer::CreateViewportUniformsAndDescriptors()

    void Renderer::WriteOutlineDescriptor(Viewport& vp) {

        if (!*vp.outlineSet || !*vp.maskView) { return; }
        vk::DescriptorImageInfo imageInfo{ .imageView = *vp.maskView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::WriteDescriptorSet write{
            .dstSet = *vp.outlineSet, .dstBinding = 0, .dstArrayElement = 0,
            .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage, .pImageInfo = &imageInfo };
        m_context.GetDevice().updateDescriptorSets(write, {});

    }   // Renderer::WriteOutlineDescriptor()

    void Renderer::CreateUniformBuffers() {

        for (uint32_t i = 0; i < m_frameManager.MaxFramesInFlight(); ++i) {
            vk::DeviceSize bufferSize = sizeof(FrameUniforms);
            m_uniformBuffers.emplace_back();
            m_uniformBuffers.back().Create(m_context, bufferSize,
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_uniformBuffersMapped.emplace_back(m_uniformBuffers.back().Map());
        }

    }   // Renderer::CreateUniformBuffers()

    void Renderer::CreateDescriptorPool() {

        uint32_t maxFrames = m_frameManager.MaxFramesInFlight();
        // Each set 0 = FrameUniforms UBO (binding 0), five storage buffers (lights 1, instances 2,
        // particles 5, light-cluster grid 6 and indices 7), shadow map image + comparison sampler (3, 4).
        // ... plus two textures: the environment map (8) and the view's ambient occlusion (9).
        std::array<vk::DescriptorPoolSize, 5> poolSizes{ {
            { .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = maxFrames * 2 },
            { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = maxFrames },
            { .type = vk::DescriptorType::eStorageBuffer, .descriptorCount = maxFrames * 5 },
            { .type = vk::DescriptorType::eSampledImage,  .descriptorCount = maxFrames },
            { .type = vk::DescriptorType::eSampler,       .descriptorCount = maxFrames }
        } };
        vk::DescriptorPoolCreateInfo poolInfo{ 
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 
            .maxSets = maxFrames,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()), .pPoolSizes = poolSizes.data() };

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

        WriteSharedFrameDescriptors(m_descriptorSets);

    }   // Renderer::CreateDescriptorSets()

    // ------------------------------------------------------------------------------------------------------------- //
    //  TEXTURES
    // ------------------------------------------------------------------------------------------------------------- //

    namespace {

        // sRGB <-> linear tables for mipmap generation. Colour textures store sRGB-encoded
        // values; averaging those directly would make the smaller levels too dark.
        struct SrgbTables {
            std::array<float, 256>    toLinear{};
            std::array<uint8_t, 4096> toSrgb{};
            SrgbTables() {
                for (int i = 0; i < 256; ++i) {
                    const float c = static_cast<float>(i) / 255.0f;
                    toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                }
                for (int i = 0; i < 4096; ++i) {
                    const float l = static_cast<float>(i) / 4095.0f;
                    const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
                    toSrgb[i] = static_cast<uint8_t>(std::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f));
                }
            }
        };

        // The next mip level of an RGBA8 image: each texel is the average of the (up to) 2x2
        // texels it covers. Alpha, and every channel of a non-colour texture, is averaged as is.
        std::vector<unsigned char> HalveRGBA8(const unsigned char* src, uint32_t w, uint32_t h, uint32_t dw, uint32_t dh, bool isSRGB) {

            static const SrgbTables tables;

            std::vector<unsigned char> dst(static_cast<size_t>(dw) * dh * 4);
            for (uint32_t y = 0; y < dh; ++y) {
                const uint32_t y0 = std::min(y * 2, h - 1);
                const uint32_t y1 = std::min(y * 2 + 1, h - 1);
                for (uint32_t x = 0; x < dw; ++x) {
                    const uint32_t x0 = std::min(x * 2, w - 1);
                    const uint32_t x1 = std::min(x * 2 + 1, w - 1);
                    const unsigned char* p[4] = {
                        src + (static_cast<size_t>(y0) * w + x0) * 4, src + (static_cast<size_t>(y0) * w + x1) * 4,
                        src + (static_cast<size_t>(y1) * w + x0) * 4, src + (static_cast<size_t>(y1) * w + x1) * 4 };
                    unsigned char* out = dst.data() + (static_cast<size_t>(y) * dw + x) * 4;
                    for (int c = 0; c < 4; ++c) {
                        if (isSRGB && c < 3) {
                            const float sum = tables.toLinear[p[0][c]] + tables.toLinear[p[1][c]] + tables.toLinear[p[2][c]] + tables.toLinear[p[3][c]];
                            out[c] = tables.toSrgb[static_cast<size_t>(sum * 0.25f * 4095.0f + 0.5f)];
                        } else {
                            out[c] = static_cast<unsigned char>((p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
                        }
                    }
                }
            }
            return dst;
        }

    }   // namespace

    TextureHandle Renderer::UploadTexturePixels(const unsigned char* pixels, uint32_t width, uint32_t height, bool isSRGB) {

        const vk::DeviceSize imageSize = vk::DeviceSize{ width } * height * 4;

        // Mipmaps: a chain of half-size copies down to 1x1. The sampler picks (and blends) the
        // levels that match how big the texture is on screen, which stops distant surfaces
        // shimmering and is cheaper to sample.
        uint32_t mipLevels = 1;
        for (uint32_t size = std::max(width, height); size > 1; size >>= 1) { ++mipLevels; }

        GpuTexture tex;
        tex.width = width;
        tex.height = height;
        const vk::Format format = isSRGB ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
        tex.image.Create(m_context, width, height, format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal, 1, {}, mipLevels);

        // Copies `pixels` into a staging buffer now; the GPU-side copy and the transition to
        // eShaderReadOnlyOptimal are recorded at the start of the next frame (m_uploads).
        m_uploads.QueueImage(*tex.image.GetImage(), pixels, imageSize, width, height);

        // The smaller levels are made here on the CPU, each from the one before, and queued the
        // same way (one upload per level).
        std::vector<unsigned char> previous;
        const unsigned char* source = pixels;
        uint32_t w = width, h = height;
        for (uint32_t level = 1; level < mipLevels; ++level) {
            const uint32_t nw = std::max(w / 2, 1u), nh = std::max(h / 2, 1u);
            std::vector<unsigned char> next = HalveRGBA8(source, w, h, nw, nh, isSRGB);
            m_uploads.QueueImage(*tex.image.GetImage(), next.data(), vk::DeviceSize{ nw } * nh * 4, nw, nh, 1, level);
            previous = std::move(next);
            source   = previous.data();
            w = nw; h = nh;
        }

        vk::ImageViewCreateInfo viewInfo{
            .image = tex.image.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = mipLevels, .layerCount = 1 }
        };
        tex.view = vk::raii::ImageView(m_context.GetDevice(), viewInfo);

        const TextureHandle handle{ static_cast<uint32_t>(m_textures.size()) };
        m_textures.push_back(std::move(tex));
        return handle;

    }   // Renderer::UploadTexturePixels()

    TextureHandle Renderer::CreateSolidColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool isSRGB) {

        const unsigned char pixels[4] = { r, g, b, a };
        return UploadTexturePixels(pixels, 1, 1, isSRGB);

    }   // Renderer::CreateSolidColorTexture()

    TextureHandle Renderer::CreateTexture(const std::string& path, bool isSRGB) {

        if (auto it = m_textureCacheByKey.find(path); it != m_textureCacheByKey.end()) {
            return TextureHandle{ it->second };
        }

        int texWidth{}, texHeight{}, texChannels{};
        stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) {
            throw std::runtime_error("[Renderer] Failed to load texture image: (" + path + ")");
        }

        TextureHandle handle = UploadTexturePixels(pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), isSRGB);
        stbi_image_free(pixels);

        m_textureCacheByKey.emplace(path, handle.id);
        return handle;

    }   // Renderer::CreateTexture()

    TextureHandle Renderer::CreateTextureFromMemory(const unsigned char* fileBytes, size_t byteCount, bool isSRGB, const std::string& cacheKey) {

        if (!cacheKey.empty()) {
            if (auto it = m_textureCacheByKey.find(cacheKey); it != m_textureCacheByKey.end()) {
                return TextureHandle{ it->second };
            }
        }

        int texWidth{}, texHeight{}, texChannels{};
        stbi_uc* pixels = stbi_load_from_memory(fileBytes, static_cast<int>(byteCount), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) {
            throw std::runtime_error("[Renderer] Failed to decode in-memory texture" + (cacheKey.empty() ? std::string() : " (" + cacheKey + ")"));
        }

        TextureHandle handle = UploadTexturePixels(pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), isSRGB);
        stbi_image_free(pixels);

        if (!cacheKey.empty()) { m_textureCacheByKey.emplace(cacheKey, handle.id); }
        return handle;

    }   // Renderer::CreateTextureFromMemory()

    const Renderer::GpuTexture& Renderer::GetTextureOrThrow(TextureHandle handle) const {

        if (!handle.IsValid() || handle.id >= m_textures.size()) {
            throw std::runtime_error("[Renderer] Unknown or invalid TextureHandle.");
        }
        return m_textures[handle.id];

    }   // Renderer::GetTextureOrThrow()

    void Renderer::CreateDefaultTextures() {

        m_defaultWhiteTexture  = CreateSolidColorTexture(255, 255, 255, 255, /*isSRGB*/ true);
        m_defaultNormalTexture = CreateSolidColorTexture(128, 128, 255, 255, /*isSRGB*/ false);   // flat tangent-space normal (0,0,1)

    }   // Renderer::CreateDefaultTextures()

    void Renderer::CreateMaterialSampler() {

        // One sampler shared by every material texture. Repeat wrap (unlike the viewport
        // sampler's clamp-to-edge - these are tiling surface textures, not a single displayed
        // framebuffer image). Trilinear across the mip levels UploadTexturePixels() builds, plus
        // anisotropic filtering - which keeps surfaces seen at a shallow angle (floors, long
        // walls) sharp - when the GPU has it (VulkanContext enables the feature if so).
        const bool  anisotropy    = m_context.GetPhysicalDevice().getFeatures().samplerAnisotropy == vk::True;
        const float maxAnisotropy = std::min(8.0f, m_context.GetPhysicalDevice().getProperties().limits.maxSamplerAnisotropy);
        vk::SamplerCreateInfo samplerInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .anisotropyEnable = anisotropy ? vk::True : vk::False,
            .maxAnisotropy = anisotropy ? maxAnisotropy : 1.0f,
            .compareEnable = vk::False,
            .minLod = 0.0f,
            .maxLod = vk::LodClampNone,
            .borderColor = vk::BorderColor::eIntOpaqueBlack,
            .unnormalizedCoordinates = vk::False
        };
        m_materialSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

    }   // Renderer::CreateMaterialSampler()

    void Renderer::CreateMaterialDescriptorPool() {

        // One block's worth (kMaterialsPerDescriptorPool materials, each a set of five
        // combined-image-samplers) - see CreateMaterial() for what happens when it fills up.
        vk::DescriptorPoolSize poolSize{
            .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = kMaterialsPerDescriptorPool * 5 };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = kMaterialsPerDescriptorPool, .poolSizeCount = 1, .pPoolSizes = &poolSize };
        m_materialDescriptorPools.emplace_back(m_context.GetDevice(), poolInfo);
        m_materialsInCurrentPool = 0;

    }   // Renderer::CreateMaterialDescriptorPool()

    void Renderer::WriteMaterialDescriptorSet(vk::DescriptorSet set, const MaterialData& data) {

        // Binding order must match VulkanPipeline::CreateMaterialDescriptorSetLayout AND the
        // shader's [[vk::binding(n,1)]] declarations exactly: 0=albedo, 1=normal,
        // 2=metallicRoughness, 3=emissive, 4=occlusion.
        auto resolve = [&](TextureHandle h, bool isNormalSlot) -> vk::ImageView {
            const TextureHandle fallback = isNormalSlot ? m_defaultNormalTexture : m_defaultWhiteTexture;
            return *GetTextureOrThrow(h.IsValid() ? h : fallback).view;
        };

        vk::DescriptorImageInfo imageInfos[5] = {
            { .sampler = *m_materialSampler, .imageView = resolve(data.albedo, false),            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal },
            { .sampler = *m_materialSampler, .imageView = resolve(data.normal, true),              .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal },
            { .sampler = *m_materialSampler, .imageView = resolve(data.metallicRoughness, false),  .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal },
            { .sampler = *m_materialSampler, .imageView = resolve(data.emissive, false),           .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal },
            { .sampler = *m_materialSampler, .imageView = resolve(data.occlusion, false),          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal },
        };

        vk::WriteDescriptorSet writes[5];
        for (uint32_t i = 0; i < 5; ++i) {
            writes[i] = vk::WriteDescriptorSet{
                .dstSet = set, .dstBinding = i, .dstArrayElement = 0,
                .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &imageInfos[i]
            };
        }
        m_context.GetDevice().updateDescriptorSets(writes, {});

    }   // Renderer::WriteMaterialDescriptorSet()

    // ------------------------------------------------------------------------------------------------------------- //
    //  MATERIALS
    // ------------------------------------------------------------------------------------------------------------- //

    MaterialHandle Renderer::CreateMaterial(const MaterialData& data) {

        // A VkDescriptorPool's maxSets is fixed at creation and can't grow, so once the current
        // block is full (or there isn't one yet, on the very first call) allocate a fresh block
        // instead of ever trying to over-allocate from a full pool - that would throw a raw
        // vk::OutOfPoolMemoryError with no indication of what actually went wrong. This makes
        // material count effectively unbounded (limited only by actual GPU memory/descriptor
        // budget, same as every other resource here), in blocks of kMaterialsPerDescriptorPool.
        if (m_materialDescriptorPools.empty() || m_materialsInCurrentPool >= kMaterialsPerDescriptorPool) {
            CreateMaterialDescriptorPool();
        }

        vk::DescriptorSetLayout layout = *m_pipeline.GetMaterialDescriptorSetLayout();
        vk::DescriptorSetAllocateInfo allocInfo{
            .descriptorPool = m_materialDescriptorPools.back(),
            .descriptorSetCount = 1,
            .pSetLayouts = &layout
        };
        std::vector<vk::raii::DescriptorSet> sets = m_context.GetDevice().allocateDescriptorSets(allocInfo);
        ++m_materialsInCurrentPool;

        GpuMaterial mat;
        mat.data = data;
        mat.descriptorSet = std::move(sets.front());
        WriteMaterialDescriptorSet(*mat.descriptorSet, data);

        const MaterialHandle handle{ static_cast<uint32_t>(m_materials.size()) };
        m_materials.push_back(std::move(mat));
        return handle;

    }   // Renderer::CreateMaterial()

    void Renderer::UpdateMaterial(MaterialHandle handle, const MaterialData& data) {

        if (!handle.IsValid() || handle.id >= m_materials.size()) {
            throw std::runtime_error("[Renderer] Unknown or invalid MaterialHandle.");
        }
        GpuMaterial& mat = m_materials[handle.id];

        const bool texturesChanged =
            !(mat.data.albedo == data.albedo) || !(mat.data.normal == data.normal) ||
            !(mat.data.metallicRoughness == data.metallicRoughness) ||
            !(mat.data.emissive == data.emissive) || !(mat.data.occlusion == data.occlusion);

        mat.data = data;

        if (texturesChanged) {
            // A command buffer from a still-in-flight frame may currently be executing a draw
            // that references this exact descriptor set's OLD image bindings; rewriting it while
            // that's true is undefined behaviour. This wait-idle makes the swap safe at the cost
            // of a full GPU stall - acceptable for an editor-triggered "designer changed a
            // texture" edit (rare, human-paced), not something to do from a hot gameplay path.
            // (Factor-only edits, the common case for a live-tweaked slider, skip this entirely -
            // see the early-continue-free structure above: texturesChanged is false and we fall
            // straight through without ever calling WriteMaterialDescriptorSet.)
            m_context.GetDevice().waitIdle();
            WriteMaterialDescriptorSet(*mat.descriptorSet, data);
        }

    }   // Renderer::UpdateMaterial()

    const MaterialData& Renderer::GetMaterialData(MaterialHandle handle) const {

        if (!handle.IsValid() || handle.id >= m_materials.size()) {
            throw std::runtime_error("[Renderer] Unknown or invalid MaterialHandle.");
        }
        return m_materials[handle.id].data;

    }   // Renderer::GetMaterialData()

    // ------------------------------------------------------------------------------------------------------------- //
    //  LIGHTS & ENVIRONMENT
    // ------------------------------------------------------------------------------------------------------------- //

    namespace {

        // CPU-side LightData -> the shader's GpuLight (see both structs in RenderTypes.h).
        GpuLight ToGpuLight(const LightData& light) {

            glm::vec3 direction = light.direction;
            const float length = glm::length(direction);
            direction = (length > 1e-6f) ? direction / length : glm::vec3(0.0f, -1.0f, 0.0f);

            // smoothstep(cosOuter, cosInner, x) in the shader needs cosOuter < cosInner strictly,
            // i.e. outer > inner - keep at least a sliver of falloff even if someone sets them
            // equal (or backwards) in the Inspector.
            const float outerDeg = glm::clamp(light.outerConeDeg, 0.1f, 89.9f);
            const float innerDeg = glm::clamp(light.innerConeDeg, 0.0f, outerDeg - 0.1f);

            GpuLight gpu;
            gpu.positionRange   = glm::vec4(light.position, std::max(light.range, 1e-3f));
            gpu.directionType   = glm::vec4(direction, static_cast<float>(light.type));
            gpu.colourIntensity = glm::vec4(light.colour, std::max(light.intensity, 0.0f));
            gpu.spotParams      = glm::vec4(std::cos(glm::radians(innerDeg)), std::cos(glm::radians(outerDeg)), 0.0f, 0.0f);
            return gpu;
        }

        // The light used when nothing was submitted: the old shader's one hardcoded light
        // (towards (0.2, 0.8, 0.5), radiance 3.0), so scenes without Light entities look as they
        // did before the lighting system existed.
        const glm::vec3 kFallbackSunTowards = glm::normalize(glm::vec3(0.2f, 0.8f, 0.5f));

        LightData MakeFallbackSun() {
            LightData sun;
            sun.type      = LightType::Directional;
            sun.direction = -kFallbackSunTowards;
            sun.intensity = 3.0f;
            return sun;
        }

        // The reverse of CubeFaceTexelDirection below: which face, and which texel of it, a
        // direction points at (same face selection table).
        void DirectionToCubeTexel(const glm::vec3& d, uint32_t size, uint32_t& face, uint32_t& x, uint32_t& y) {

            const glm::vec3 a = glm::abs(d);
            float u, v, major;
            if (a.x >= a.y && a.x >= a.z) {
                major = a.x;
                if (d.x > 0.0f) { face = 0; u = -d.z; v = -d.y; } else { face = 1; u = d.z; v = -d.y; }
            } else if (a.y >= a.z) {
                major = a.y;
                if (d.y > 0.0f) { face = 2; u = d.x; v = d.z; } else { face = 3; u = d.x; v = -d.z; }
            } else {
                major = a.z;
                if (d.z > 0.0f) { face = 4; u = d.x; v = -d.y; } else { face = 5; u = -d.x; v = -d.y; }
            }
            major = std::max(major, 1e-8f);
            const float fs = static_cast<float>(size);
            x = static_cast<uint32_t>(glm::clamp((u / major * 0.5f + 0.5f) * fs, 0.0f, fs - 1.0f));
            y = static_cast<uint32_t>(glm::clamp((v / major * 0.5f + 0.5f) * fs, 0.0f, fs - 1.0f));

        }

        // World-space direction through the centre of texel (x, y) of cube face `face`, using the
        // Vulkan/OpenGL cube-map face selection table (face order +X, -X, +Y, -Y, +Z, -Z; row 0 =
        // t near 0). Inverse of what the sampler does, so procedural/equirect faces generated with
        // this line up with SamplerCube lookups by construction.
        glm::vec3 CubeFaceTexelDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t size) {

            const float u = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
            const float v = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;

            glm::vec3 d;
            switch (face) {
                case 0:  d = glm::vec3( 1.0f,   -v,   -u); break;   // +X
                case 1:  d = glm::vec3(-1.0f,   -v,    u); break;   // -X
                case 2:  d = glm::vec3(    u, 1.0f,    v); break;   // +Y
                case 3:  d = glm::vec3(    u,-1.0f,   -v); break;   // -Y
                case 4:  d = glm::vec3(    u,   -v, 1.0f); break;   // +Z
                default: d = glm::vec3(   -u,   -v,-1.0f); break;   // -Z
            }
            return glm::normalize(d);
        }

        // RGBA float -> one RGBA16F texel. Clamped to half's finite range (an HDRI's sun can
        // exceed 65504) and NaN-scrubbed, so one bad source pixel can't poison the sky.
        uint64_t PackHalfTexel(glm::vec4 c) {
            for (int i = 0; i < 4; ++i) {
                if (!(c[i] == c[i])) { c[i] = 0.0f; }   // NaN
                c[i] = glm::clamp(c[i], 0.0f, 65504.0f);
            }
            return glm::packHalf4x16(c);
        }

        // The built-in sky's colour for a world direction: zenith-to-horizon gradient above,
        // darker ground below, and a soft glow + small disc around the fallback sun.
        glm::vec3 ProceduralSkyColour(const glm::vec3& d) {

            const glm::vec3 zenith  { 0.10f, 0.24f, 0.55f };
            const glm::vec3 horizon { 0.62f, 0.74f, 0.90f };
            const glm::vec3 ground  { 0.16f, 0.15f, 0.14f };

            glm::vec3 colour;
            if (d.y >= 0.0f) {
                colour = glm::mix(horizon, zenith, std::pow(d.y, 0.5f));
            } else {
                colour = glm::mix(horizon * 0.8f, ground, std::pow(std::min(-d.y * 4.0f, 1.0f), 0.6f));
            }

            const float s = std::max(glm::dot(d, kFallbackSunTowards), 0.0f);
            colour += glm::vec3(1.0f, 0.85f, 0.6f) * (std::pow(s, 64.0f) * 0.5f + std::pow(s, 1500.0f) * 6.0f);
            return colour;
        }

    }   // anonymous namespace

    void Renderer::SubmitLight(const LightData& light) {

        if (m_lightQueue.size() >= kMaxLights) {
            if (!m_warnedTooManyLights) {
                CRU_WARN("[Renderer] More than {} lights submitted in one frame; extras are ignored.", kMaxLights);
                m_warnedTooManyLights = true;
            }
            return;
        }
        // The first enabled directional light that asks for shadows gets the cascaded shadow map.
        if (m_shadowLightIndex < 0 && light.castShadows && light.type == LightType::Directional) {
            m_shadowLightIndex = static_cast<int32_t>(m_lightQueue.size());
        }
        m_lightQueue.push_back(ToGpuLight(light));

    }   // Renderer::SubmitLight()

    void Renderer::SetEnvironment(const EnvironmentSettings& settings) {

        m_environment = settings;

    }   // Renderer::SetEnvironment()

    void Renderer::CreateLightBuffers() {

        for (uint32_t i = 0; i < m_frameManager.MaxFramesInFlight(); ++i) {
            m_lightBuffers.emplace_back();
            m_lightBuffers.back().Create(m_context, sizeof(GpuLight) * kMaxLights,
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_lightBuffersMapped.emplace_back(m_lightBuffers.back().Map());
        }

    }   // Renderer::CreateLightBuffers()

    void Renderer::WriteSharedFrameDescriptors(std::vector<vk::raii::DescriptorSet>& sets) {

        for (size_t i = 0; i < sets.size() && i < m_lightBuffers.size() && i < m_instanceBuffers.size()
                           && i < m_particleBuffers.size() && i < m_clusterGridBuffers.size(); ++i) {
            vk::DescriptorBufferInfo particleInfo{ .buffer = *m_particleBuffers[i].GetBuffer(), .offset = 0, .range = vk::WholeSize };
            vk::DescriptorBufferInfo clusterGridInfo{ .buffer = *m_clusterGridBuffers[i].GetBuffer(), .offset = 0, .range = vk::WholeSize };
            vk::DescriptorBufferInfo clusterIndexInfo{ .buffer = *m_clusterIndexBuffers[i].GetBuffer(), .offset = 0, .range = vk::WholeSize };
            vk::DescriptorBufferInfo lightInfo{
                .buffer = *m_lightBuffers[i].GetBuffer(),
                .offset = 0,
                .range  = sizeof(GpuLight) * kMaxLights };
            vk::DescriptorBufferInfo instanceInfo{
                .buffer = *m_instanceBuffers[i].GetBuffer(),
                .offset = 0,
                .range  = vk::WholeSize };
            vk::DescriptorImageInfo shadowImageInfo{
                .imageView   = *m_shadowArrayView,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            vk::DescriptorImageInfo shadowSamplerInfo{ .sampler = *m_shadowSampler };

            const std::array<vk::WriteDescriptorSet, 7> writes{ {
                { .dstSet = *sets[i], .dstBinding = 5, .descriptorCount = 1,
                  .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &particleInfo },
                { .dstSet = *sets[i], .dstBinding = 6, .descriptorCount = 1,
                  .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &clusterGridInfo },
                { .dstSet = *sets[i], .dstBinding = 7, .descriptorCount = 1,
                  .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &clusterIndexInfo },
                { .dstSet = *sets[i], .dstBinding = 1, .descriptorCount = 1,
                  .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &lightInfo },
                { .dstSet = *sets[i], .dstBinding = 2, .descriptorCount = 1,
                  .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &instanceInfo },
                { .dstSet = *sets[i], .dstBinding = 3, .descriptorCount = 1,
                  .descriptorType = vk::DescriptorType::eSampledImage, .pImageInfo = &shadowImageInfo },
                { .dstSet = *sets[i], .dstBinding = 4, .descriptorCount = 1,
                  .descriptorType = vk::DescriptorType::eSampler, .pImageInfo = &shadowSamplerInfo }
            } };
            m_context.GetDevice().updateDescriptorSets(writes, {});
        }

    }   // Renderer::WriteSharedFrameDescriptors()

    void Renderer::UpdateLightBuffer(uint32_t frameIndex) {

        // m_frameLights = what is uploaded: directional lights first (the shader loops over those
        // directly and finds the rest through the light clusters), each group in submission order.
        m_frameLights.clear();

        if (m_lightQueue.empty()) {
            m_frameLights.push_back(ToGpuLight(MakeFallbackSun()));
            m_frameDirLightCount = 1;
            m_shadowLightIndex   = 0;   // the fallback sun always casts
        } else {
            const auto isDirectional = [](const GpuLight& l) { return static_cast<int>(l.directionType.w + 0.5f) == static_cast<int>(LightType::Directional); };
            int32_t shadowIndex = -1;
            for (size_t i = 0; i < m_lightQueue.size(); ++i) {
                if (!isDirectional(m_lightQueue[i])) { continue; }
                if (static_cast<int32_t>(i) == m_shadowLightIndex) { shadowIndex = static_cast<int32_t>(m_frameLights.size()); }
                m_frameLights.push_back(m_lightQueue[i]);
            }
            m_frameDirLightCount = static_cast<uint32_t>(m_frameLights.size());
            for (const GpuLight& l : m_lightQueue) { if (!isDirectional(l)) { m_frameLights.push_back(l); } }
            m_shadowLightIndex = shadowIndex;   // now an index into the reordered buffer
        }

        m_frameLightCount = static_cast<uint32_t>(m_frameLights.size());   // already capped by SubmitLight
        if (m_shadowLightIndex >= 0) { m_shadowLightDir = glm::vec3(m_frameLights[m_shadowLightIndex].directionType); }

        // Same frame-in-flight safety as the uniform buffers: BeginFrame() has already waited on
        // this frame index's fence, so the GPU is done reading m_lightBuffers[frameIndex].
        memcpy(m_lightBuffersMapped[frameIndex], m_frameLights.data(), sizeof(GpuLight) * m_frameLightCount);

    }   // Renderer::UpdateLightBuffer()

    void Renderer::FillEnvironmentUniforms(FrameUniforms& ubo) const {

        const EnvironmentSettings& env = m_environment;
        ubo.ambientSky    = glm::vec4(env.ambientSky * env.ambientIntensity, 0.0f);
        ubo.ambientGround = glm::vec4(env.ambientGround * env.ambientIntensity, 0.0f);
        ubo.renderParams  = glm::vec4(std::max(env.exposure, 0.0f), std::max(env.skyboxIntensity, 0.0f), 0.0f, 0.0f);
        ubo.lightParams   = glm::uvec4(m_frameLightCount, 0u, 0u, m_frameDirLightCount);

        ubo.fogColour = glm::vec4(glm::max(env.fogColour, glm::vec3(0.0f)), std::max(env.fogDensity, 0.0f));
        ubo.fogParams = glm::vec4(std::max(env.fogHeightFalloff, 0.0f), env.fogBaseHeight, std::max(env.fogStart, 0.0f),
                                  glm::clamp(env.fogMaxOpacity, 0.0f, 1.0f));

        // Light from the sky: as bright as the sky is drawn, times the ambient intensity.
        const GpuCubemap& sky = CurrentSkyCubemap();
        ubo.ambientParams = glm::vec4(env.ambientFromSky ? 1.0f : 0.0f, static_cast<float>(sky.envMipCount - 1),
                                      std::max(env.ambientIntensity, 0.0f) * std::max(env.skyboxIntensity, 0.0f), 0.0f);
        for (size_t i = 0; i < sky.sh.size(); ++i) { ubo.skySH[i] = sky.sh[i]; }

    }   // Renderer::FillEnvironmentUniforms()

    const Renderer::GpuCubemap& Renderer::CurrentSkyCubemap() const {

        const CubemapHandle handle = m_environment.skybox;
        if (handle.IsValid() && handle.id < m_cubemaps.size()) { return m_cubemaps[handle.id]; }
        return m_cubemaps[m_defaultSkybox.id];

    }   // Renderer::CurrentSkyCubemap()

    void Renderer::WriteViewTextures(vk::DescriptorSet frameSet, const PostTargets& targets) {

        if (m_cubemaps.empty() || !*targets.aoBlurView) { return; }

        vk::DescriptorImageInfo envInfo{
            .sampler = *m_envSampler, .imageView = *CurrentSkyCubemap().envView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::DescriptorImageInfo aoInfo{
            .sampler = *m_viewportSampler, .imageView = *targets.aoBlurView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        const std::array<vk::WriteDescriptorSet, 2> writes{ {
            { .dstSet = frameSet, .dstBinding = 8, .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &envInfo },
            { .dstSet = frameSet, .dstBinding = 9, .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &aoInfo }
        } };
        m_context.GetDevice().updateDescriptorSets(writes, {});

    }   // Renderer::WriteViewTextures()

    // ------------------------------------------------------------------------------------------------------------- //
    //  SKYBOX / CUBEMAPS
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::CreateSkyboxSampler() {

        // Clamp-to-edge so bilinear filtering at a face's border never wraps around to that
        // face's opposite edge (seamless cube filtering across faces is the default in Vulkan).
        vk::SamplerCreateInfo samplerInfo{
            .magFilter               = vk::Filter::eLinear,
            .minFilter               = vk::Filter::eLinear,
            .mipmapMode              = vk::SamplerMipmapMode::eLinear,
            .addressModeU            = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV            = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW            = vk::SamplerAddressMode::eClampToEdge,
            .anisotropyEnable        = vk::False,
            .compareEnable           = vk::False,
            .minLod                  = 0.0f,
            .maxLod                  = 0.0f,
            .borderColor             = vk::BorderColor::eIntOpaqueBlack,
            .unnormalizedCoordinates = vk::False
        };
        m_skyboxSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

        // Environment maps are read at a mip level chosen by roughness, blending between levels.
        samplerInfo.maxLod = vk::LodClampNone;
        m_envSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

    }   // Renderer::CreateSkyboxSampler()

    void Renderer::CreateCubemapDescriptorPool() {

        // eFreeDescriptorSet is required, not optional: each GpuCubemap's vk::raii::DescriptorSet
        // frees itself back to this pool in its destructor.
        vk::DescriptorPoolSize poolSize{
            .type            = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = kMaxCubemaps };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets       = kMaxCubemaps,
            .poolSizeCount = 1,
            .pPoolSizes    = &poolSize };
        m_cubemapDescriptorPool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

    }   // Renderer::CreateCubemapDescriptorPool()

    CubemapHandle Renderer::UploadCubemapFaces(const void* faceData, uint32_t faceSize, vk::Format format,
                                               uint32_t bytesPerTexel, const std::string& debugName) {

        if (m_cubemaps.size() >= kMaxCubemaps) {
            throw std::runtime_error("[Renderer] UploadCubemapFaces: kMaxCubemaps exceeded; raise the limit if you genuinely need more.");
        }

        const vk::DeviceSize faceBytes  = vk::DeviceSize{ faceSize } * faceSize * bytesPerTexel;
        const vk::DeviceSize totalBytes = faceBytes * 6;

        GpuCubemap cube;
        cube.faceSize  = faceSize;
        cube.debugName = debugName;
        cube.image.Create(m_context, faceSize, faceSize, format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            /*arrayLayers*/ 6, vk::ImageCreateFlagBits::eCubeCompatible);

        // Six faces, tightly packed one after another; see UploadTexturePixels for when the
        // GPU-side copy actually runs.
        m_uploads.QueueImage(*cube.image.GetImage(), faceData, totalBytes, faceSize, faceSize, 6);

        // The same sky, prepared as a light source for image-based lighting.
        BuildEnvironmentLighting(cube, faceData, faceSize, format);

        vk::ImageViewCreateInfo viewInfo{
            .image            = cube.image.GetImage(),
            .viewType         = vk::ImageViewType::eCube,
            .format           = format,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 6 }
        };
        cube.view = vk::raii::ImageView(m_context.GetDevice(), viewInfo);

        vk::DescriptorSetLayout layout = *m_pipeline.GetSkyboxDescriptorSetLayout();
        vk::DescriptorSetAllocateInfo allocInfo{
            .descriptorPool     = m_cubemapDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &layout
        };
        std::vector<vk::raii::DescriptorSet> sets = m_context.GetDevice().allocateDescriptorSets(allocInfo);
        cube.descriptorSet = std::move(sets.front());

        vk::DescriptorImageInfo imageInfo{
            .sampler     = *m_skyboxSampler,
            .imageView   = *cube.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::WriteDescriptorSet write{
            .dstSet          = *cube.descriptorSet,
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo      = &imageInfo };
        m_context.GetDevice().updateDescriptorSets(write, {});

        const CubemapHandle handle{ static_cast<uint32_t>(m_cubemaps.size()) };
        m_cubemaps.push_back(std::move(cube));
        return handle;

    }   // Renderer::UploadCubemapFaces()

    void Renderer::BuildEnvironmentLighting(GpuCubemap& cube, const void* faceData, uint32_t faceSize, vk::Format format) {

        // Done once per cubemap, on the CPU, at a small size (the result is blurry by nature):
        //   - an "environment map": level 0 is the sky shrunk to at most 128x128 per face, and
        //     each smaller level is that sky blurred as a rougher surface would reflect it;
        //   - nine spherical-harmonic coefficients describing the sky's overall diffuse light.

        // 1. Decode the faces to linear floating-point colour.
        const size_t facePixels = static_cast<size_t>(faceSize) * faceSize;
        std::vector<glm::vec3> source(facePixels * 6);
        if (format == vk::Format::eR16G16B16A16Sfloat) {
            const uint16_t* p = static_cast<const uint16_t*>(faceData);
            for (size_t i = 0; i < source.size(); ++i) {
                source[i] = glm::vec3(glm::unpackHalf1x16(p[i * 4]), glm::unpackHalf1x16(p[i * 4 + 1]), glm::unpackHalf1x16(p[i * 4 + 2]));
            }
        } else {
            const bool srgb = format == vk::Format::eR8G8B8A8Srgb;
            const unsigned char* p = static_cast<const unsigned char*>(faceData);
            for (size_t i = 0; i < source.size(); ++i) {
                glm::vec3 c(p[i * 4] / 255.0f, p[i * 4 + 1] / 255.0f, p[i * 4 + 2] / 255.0f);
                if (srgb) { c = glm::pow(c, glm::vec3(2.2f)); }
                source[i] = c;
            }
        }

        // 2. A chain of plain shrunk copies: base size (power of two, at most 128) down to 4.
        uint32_t baseSize = 4;
        while (baseSize * 2 <= std::min(faceSize, 128u)) { baseSize *= 2; }
        std::vector<uint32_t>               sizes;
        std::vector<std::vector<glm::vec3>> boxes;
        for (uint32_t s = baseSize; s >= 4 && sizes.size() < 6; s /= 2) { sizes.push_back(s); }

        boxes.emplace_back(static_cast<size_t>(baseSize) * baseSize * 6);
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t y = 0; y < baseSize; ++y) {
                const uint32_t y0 = y * faceSize / baseSize, y1 = std::max((y + 1) * faceSize / baseSize, y0 + 1);
                for (uint32_t x = 0; x < baseSize; ++x) {
                    const uint32_t x0 = x * faceSize / baseSize, x1 = std::max((x + 1) * faceSize / baseSize, x0 + 1);
                    glm::vec3 sum(0.0f);
                    for (uint32_t sy = y0; sy < y1; ++sy) {
                        for (uint32_t sx = x0; sx < x1; ++sx) { sum += source[face * facePixels + static_cast<size_t>(sy) * faceSize + sx]; }
                    }
                    boxes[0][(static_cast<size_t>(face) * baseSize + y) * baseSize + x] = sum / static_cast<float>((y1 - y0) * (x1 - x0));
                }
            }
        }
        for (size_t level = 1; level < sizes.size(); ++level) {
            const uint32_t s = sizes[level], ps = sizes[level - 1];
            boxes.emplace_back(static_cast<size_t>(s) * s * 6);
            for (uint32_t face = 0; face < 6; ++face) {
                for (uint32_t y = 0; y < s; ++y) {
                    for (uint32_t x = 0; x < s; ++x) {
                        const size_t row0 = (static_cast<size_t>(face) * ps + y * 2) * ps + x * 2;
                        const size_t row1 = row0 + ps;
                        boxes[level][(static_cast<size_t>(face) * s + y) * s + x] =
                            (boxes[level - 1][row0] + boxes[level - 1][row0 + 1] + boxes[level - 1][row1] + boxes[level - 1][row1 + 1]) * 0.25f;
                    }
                }
            }
        }
        const auto lookup = [&](size_t level, const glm::vec3& dir) {
            uint32_t face, x, y;
            DirectionToCubeTexel(dir, sizes[level], face, x, y);
            return boxes[level][(static_cast<size_t>(face) * sizes[level] + y) * sizes[level] + x];
        };

        // 3. Diffuse light: project the sky onto nine spherical harmonics (from a small level -
        //    the result only keeps the broadest features anyway), with the cosine falloff of a
        //    diffuse surface and the basis constants folded in, so the shader evaluates a plain
        //    polynomial of the normal (see SkyIrradiance in DefaultShader2.slang).
        {
            size_t level = 0;
            while (level + 1 < sizes.size() && sizes[level] > 32) { ++level; }
            const uint32_t s = sizes[level];
            std::array<glm::vec3, 9> sum{};
            float weightSum = 0.0f;
            for (uint32_t face = 0; face < 6; ++face) {
                for (uint32_t y = 0; y < s; ++y) {
                    for (uint32_t x = 0; x < s; ++x) {
                        const glm::vec3 d = CubeFaceTexelDirection(face, x, y, s);   // normalized
                        // A texel near a face's corner covers less of the sphere than one at its centre.
                        const float u = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(s) - 1.0f;
                        const float v = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(s) - 1.0f;
                        const float w = 1.0f / std::pow(1.0f + u * u + v * v, 1.5f);
                        const glm::vec3 c = boxes[level][(static_cast<size_t>(face) * s + y) * s + x] * w;
                        const float basis[9] = {
                            0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x,
                            1.092548f * d.x * d.y, 1.092548f * d.y * d.z, 0.315392f * (3.0f * d.z * d.z - 1.0f),
                            1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y) };
                        for (int i = 0; i < 9; ++i) { sum[i] += c * basis[i]; }
                        weightSum += w;
                    }
                }
            }
            const float norm = 4.0f * 3.14159265f / std::max(weightSum, 1e-6f);
            // (cosine-lobe factor / pi) * basis constant, per coefficient.
            const float scale[9] = {
                1.0f * 0.282095f,
                (2.0f / 3.0f) * 0.488603f, (2.0f / 3.0f) * 0.488603f, (2.0f / 3.0f) * 0.488603f,
                0.25f * 1.092548f, 0.25f * 1.092548f, 0.25f * 0.315392f, 0.25f * 1.092548f, 0.25f * 0.546274f };
            for (int i = 0; i < 9; ++i) { cube.sh[i] = glm::vec4(sum[i] * norm * scale[i], 0.0f); }
        }

        // 4. Reflections: level 0 is the shrunk sky as is (a mirror); each further level is what a
        //    rougher surface would reflect - the average of the sky over the spread of directions
        //    such a surface scatters into (GGX importance sampling, 32 directions per texel, read
        //    from the shrunk copy of the same size so bright spots do not turn into speckles).
        cube.envMipCount = static_cast<uint32_t>(sizes.size());
        cube.envImage.Create(m_context, baseSize, baseSize, vk::Format::eR16G16B16A16Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            /*arrayLayers*/ 6, vk::ImageCreateFlagBits::eCubeCompatible, cube.envMipCount);

        constexpr uint32_t kSamples = 32;
        std::vector<uint64_t> packed;
        for (size_t level = 0; level < sizes.size(); ++level) {
            const uint32_t s = sizes[level];
            packed.resize(static_cast<size_t>(s) * s * 6);
            const float roughness = sizes.size() > 1 ? static_cast<float>(level) / static_cast<float>(sizes.size() - 1) : 0.0f;
            const float a         = roughness * roughness;
            for (uint32_t face = 0; face < 6; ++face) {
                for (uint32_t y = 0; y < s; ++y) {
                    for (uint32_t x = 0; x < s; ++x) {
                        glm::vec3 colour;
                        if (level == 0) {
                            colour = boxes[0][(static_cast<size_t>(face) * s + y) * s + x];
                        } else {
                            const glm::vec3 n  = CubeFaceTexelDirection(face, x, y, s);
                            const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                            const glm::vec3 tx = glm::normalize(glm::cross(up, n));
                            const glm::vec3 ty = glm::cross(n, tx);
                            glm::vec3 sum(0.0f);
                            float     weight = 0.0f;
                            for (uint32_t i = 0; i < kSamples; ++i) {
                                // Hammersley point i: evenly spread pairs (u1, u2) in the unit square.
                                uint32_t bits = i;
                                bits = (bits << 16u) | (bits >> 16u);
                                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                                bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                                bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                                const float u1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(kSamples);
                                const float u2 = static_cast<float>(bits) * 2.3283064365386963e-10f;

                                const float phi      = 2.0f * 3.14159265f * u1;
                                const float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
                                const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
                                const glm::vec3 h = tx * (sinTheta * std::cos(phi)) + ty * (sinTheta * std::sin(phi)) + n * cosTheta;
                                const glm::vec3 l = 2.0f * glm::dot(n, h) * h - n;
                                const float nDotL = glm::dot(n, l);
                                if (nDotL <= 0.0f) { continue; }
                                sum    += lookup(level, l) * nDotL;
                                weight += nDotL;
                            }
                            colour = weight > 0.0f ? sum / weight : lookup(level, n);
                        }
                        // Half floats top out near 65504: keep an HDR sun from becoming infinity.
                        colour = glm::min(colour, glm::vec3(60000.0f));
                        packed[(static_cast<size_t>(face) * s + y) * s + x] = PackHalfTexel(glm::vec4(colour, 1.0f));
                    }
                }
            }
            m_uploads.QueueImage(*cube.envImage.GetImage(), packed.data(), vk::DeviceSize{ packed.size() } * 8, s, s, 6,
                                 static_cast<uint32_t>(level));
        }

        vk::ImageViewCreateInfo viewInfo{
            .image            = cube.envImage.GetImage(),
            .viewType         = vk::ImageViewType::eCube,
            .format           = vk::Format::eR16G16B16A16Sfloat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = cube.envMipCount, .layerCount = 6 }
        };
        cube.envView = vk::raii::ImageView(m_context.GetDevice(), viewInfo);

    }   // Renderer::BuildEnvironmentLighting()

    void Renderer::CreateDefaultSkybox() {

        // Small is fine: the procedural sky is a smooth gradient, and linear filtering hides the
        // texel size everywhere except the (deliberately soft) sun glow.
        constexpr uint32_t kSize = 128;
        std::vector<uint64_t> texels(static_cast<size_t>(kSize) * kSize * 6);

        size_t i = 0;
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t y = 0; y < kSize; ++y) {
                for (uint32_t x = 0; x < kSize; ++x) {
                    const glm::vec3 dir = CubeFaceTexelDirection(face, x, y, kSize);
                    texels[i++] = PackHalfTexel(glm::vec4(ProceduralSkyColour(dir), 1.0f));
                }
            }
        }

        m_defaultSkybox = UploadCubemapFaces(texels.data(), kSize, vk::Format::eR16G16B16A16Sfloat, 8, "Default Sky");

    }   // Renderer::CreateDefaultSkybox()

    CubemapHandle Renderer::CreateCubemap(const std::array<std::string, 6>& facePaths, bool isSRGB) {

        std::string cacheKey = isSRGB ? "faces[srgb]:" : "faces[unorm]:";
        for (const std::string& path : facePaths) { cacheKey += path + "|"; }
        if (auto it = m_cubemapCacheByKey.find(cacheKey); it != m_cubemapCacheByKey.end()) {
            return CubemapHandle{ it->second };
        }

        using StbPixels = std::unique_ptr<stbi_uc, void(*)(void*)>;
        std::vector<StbPixels> faces;
        faces.reserve(6);
        int faceSize = 0;

        for (size_t f = 0; f < 6; ++f) {
            int w{}, h{}, channels{};
            stbi_uc* pixels = stbi_load(facePaths[f].c_str(), &w, &h, &channels, STBI_rgb_alpha);
            if (!pixels) {
                throw std::runtime_error("[Renderer] CreateCubemap: failed to load face image (" + facePaths[f] + ")");
            }
            faces.emplace_back(pixels, stbi_image_free);

            if (w != h) {
                throw std::runtime_error("[Renderer] CreateCubemap: face is not square (" + facePaths[f] + ")");
            }
            if (f == 0) { faceSize = w; }
            else if (w != faceSize) {
                throw std::runtime_error("[Renderer] CreateCubemap: face size differs from face 0 (" + facePaths[f] + ")");
            }
        }

        const size_t faceBytes = static_cast<size_t>(faceSize) * faceSize * 4;
        std::vector<unsigned char> packed(faceBytes * 6);
        for (size_t f = 0; f < 6; ++f) {
            memcpy(packed.data() + f * faceBytes, faces[f].get(), faceBytes);
        }

        const vk::Format format = isSRGB ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
        const CubemapHandle handle = UploadCubemapFaces(packed.data(), static_cast<uint32_t>(faceSize), format, 4,
            std::filesystem::path(facePaths[0]).parent_path().filename().string());

        m_cubemapCacheByKey.emplace(cacheKey, handle.id);
        return handle;

    }   // Renderer::CreateCubemap()

    CubemapHandle Renderer::CreateCubemapFromEquirect(const std::string& path, uint32_t faceSize) {

        faceSize = std::max(faceSize, 1u);
        const std::string cacheKey = "equirect:" + path + "@" + std::to_string(faceSize);
        if (auto it = m_cubemapCacheByKey.find(cacheKey); it != m_cubemapCacheByKey.end()) {
            return CubemapHandle{ it->second };
        }

        // stbi_loadf returns linear floats for every format: .hdr as-is (true HDR), LDR images
        // converted through stb's default 2.2 gamma - so one path serves both.
        int w{}, h{}, channels{};
        float* pixels = stbi_loadf(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
        if (!pixels) {
            throw std::runtime_error("[Renderer] CreateCubemapFromEquirect: failed to load (" + path + ")");
        }
        std::unique_ptr<float, void(*)(void*)> owner(pixels, stbi_image_free);

        // Bilinear lookup, wrapping horizontally (longitude is periodic) and clamping vertically.
        auto fetch = [&](int x, int y) -> glm::vec4 {
            x = ((x % w) + w) % w;
            y = std::clamp(y, 0, h - 1);
            const float* p = pixels + (static_cast<size_t>(y) * w + x) * 4;
            return glm::vec4(p[0], p[1], p[2], 1.0f);
        };
        auto sample = [&](float u, float v) -> glm::vec4 {
            const float fx = u * static_cast<float>(w) - 0.5f;
            const float fy = v * static_cast<float>(h) - 0.5f;
            const int   x0 = static_cast<int>(std::floor(fx));
            const int   y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - static_cast<float>(x0);
            const float ty = fy - static_cast<float>(y0);
            const glm::vec4 top    = glm::mix(fetch(x0, y0),     fetch(x0 + 1, y0),     tx);
            const glm::vec4 bottom = glm::mix(fetch(x0, y0 + 1), fetch(x0 + 1, y0 + 1), tx);
            return glm::mix(top, bottom, ty);
        };

        constexpr float kInvTwoPi = 0.15915494309f;
        constexpr float kInvPi    = 0.31830988618f;

        std::vector<uint64_t> texels(static_cast<size_t>(faceSize) * faceSize * 6);
        size_t i = 0;
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t y = 0; y < faceSize; ++y) {
                for (uint32_t x = 0; x < faceSize; ++x) {
                    const glm::vec3 d = CubeFaceTexelDirection(face, x, y, faceSize);
                    // Longitude from +X towards +Z (turning right, in this right-handed Y-up
                    // world) runs left-to-right across the panorama; latitude runs top (+Y) to
                    // bottom (-Y).
                    const float u = std::atan2(d.z, d.x) * kInvTwoPi + 0.5f;
                    const float v = std::acos(glm::clamp(d.y, -1.0f, 1.0f)) * kInvPi;
                    texels[i++] = PackHalfTexel(sample(u, v));
                }
            }
        }

        const CubemapHandle handle = UploadCubemapFaces(texels.data(), faceSize, vk::Format::eR16G16B16A16Sfloat, 8,
            std::filesystem::path(path).stem().string());

        m_cubemapCacheByKey.emplace(cacheKey, handle.id);
        return handle;

    }   // Renderer::CreateCubemapFromEquirect()

    // ------------------------------------------------------------------------------------------------------------- //
    //  RENDER PROXIES, CULLING, PICKING
    // ------------------------------------------------------------------------------------------------------------- //

    RenderProxyHandle Renderer::CreateRenderProxy(std::uint64_t userData) {

        uint32_t id;
        if (!m_freeProxies.empty()) { id = m_freeProxies.back(); m_freeProxies.pop_back(); }
        else { id = static_cast<uint32_t>(m_proxies.size()); m_proxies.emplace_back(); }

        m_proxies[id] = RenderProxy{};
        m_proxies[id].alive = true;
        m_proxies[id].userData = userData;
        return RenderProxyHandle{ id };

    }   // Renderer::CreateRenderProxy()

    void Renderer::SetRenderProxy(RenderProxyHandle handle, MeshHandle mesh, MaterialHandle material, const glm::mat4& model, bool visible, const glm::vec4& params) {

        if (!handle.IsValid() || handle.id >= m_proxies.size() || !m_proxies[handle.id].alive) { return; }
        RenderProxy& p = m_proxies[handle.id];

        p.params = params;   // read straight from the proxy when the view is built; nothing else depends on it

        const bool sameMatrix = memcmp(&p.model, &model, sizeof(glm::mat4)) == 0;
        if (sameMatrix && p.mesh == mesh && p.material == material && p.visible == visible) { return; }   // the common case

        const bool boundsChange = !sameMatrix || !(p.mesh == mesh) || p.visible != visible;
        p.mesh     = mesh;
        p.material = material;
        p.model    = model;
        p.visible  = visible;
        if (boundsChange) { RefreshProxyBounds(handle.id); }   // a material-only change never touches the index

    }   // Renderer::SetRenderProxy()

    void Renderer::DestroyRenderProxy(RenderProxyHandle handle) {

        if (!handle.IsValid() || handle.id >= m_proxies.size() || !m_proxies[handle.id].alive) { return; }
        RenderProxy& p = m_proxies[handle.id];
        if (p.spatial != SpatialIndex::kInvalid) { m_spatial.Remove(p.spatial); }
        p = RenderProxy{};
        m_freeProxies.push_back(handle.id);

    }   // Renderer::DestroyRenderProxy()

    void Renderer::RefreshProxyBounds(uint32_t id) {

        RenderProxy& p = m_proxies[id];
        p.awaitingBounds = false;

        auto removeFromIndex = [&] {
            if (p.spatial != SpatialIndex::kInvalid) { m_spatial.Remove(p.spatial); p.spatial = SpatialIndex::kInvalid; }
        };

        if (!p.visible || !p.mesh.IsValid()) { removeFromIndex(); return; }

        glm::vec3 mn, mx;
        if (!GetMeshBounds(p.mesh, mn, mx)) {
            // Mesh not uploaded yet (CreateMeshAsync): not drawable anyway; retry next frame.
            removeFromIndex();
            p.awaitingBounds = true;
            m_proxiesAwaitingBounds.push_back(id);
            return;
        }

        const AABB world = AABB::Transform(AABB(mn, mx), p.model);
        if (p.spatial == SpatialIndex::kInvalid) { p.spatial = m_spatial.Insert(world, id); }
        else                                     { m_spatial.Update(p.spatial, world); }

    }   // Renderer::RefreshProxyBounds()

    void Renderer::BuildVisibleItems(const Camera& camera, RenderCullStats& stats) {

        m_visibleItems.clear();
        stats = RenderCullStats{};

        const Frustum frustum = Frustum::FromViewProjection(camera.GetProjection() * camera.GetView());

        // Proxies: tree branches / the moving-object array, per SpatialIndex.
        m_spatial.QueryFrustum(frustum, [&](SpatialIndex::Handle h) {
            const RenderProxy& p = m_proxies[static_cast<uint32_t>(m_spatial.GetUserData(h))];
            m_visibleItems.push_back(RenderItem{ p.mesh, p.material, p.model, p.params });
        });
        stats.proxies        = m_spatial.GetCount();
        stats.proxiesVisible = static_cast<uint32_t>(m_visibleItems.size());
        stats.movingProxies  = m_spatial.GetActiveCount();

        // Immediate items: brute force (there are few, and they have no persistent identity).
        glm::vec3 mn, mx;
        for (const RenderItem& item : m_renderQueue) {
            if (GetMeshBounds(item.mesh, mn, mx) && !frustum.Intersects(AABB::Transform(AABB(mn, mx), item.model))) { continue; }
            m_visibleItems.push_back(item);
        }
        stats.immediate        = static_cast<uint32_t>(m_renderQueue.size());
        stats.immediateVisible = static_cast<uint32_t>(m_visibleItems.size()) - stats.proxiesVisible;

        // Group by (material, mesh) so the material set and mesh buffers rebind as rarely as possible.
        std::sort(m_visibleItems.begin(), m_visibleItems.end(), [](const RenderItem& a, const RenderItem& b) {
            if (!(a.material == b.material)) { return a.material < b.material; }
            return a.mesh < b.mesh;
        });

    }   // Renderer::BuildVisibleItems()

    RenderCullStats Renderer::GetViewportCullStats(ViewportHandle handle) const {

        auto it = m_viewports.find(handle.id);
        return it == m_viewports.end() ? RenderCullStats{} : it->second.cullStats;

    }   // Renderer::GetViewportCullStats()

    // ------------------------------------------------------------------------------------------------------------- //
    //  FRAME PREPARATION & INSTANCING
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::PrepareView(Camera& camera, vk::Extent2D extent, RenderCullStats& stats, ViewDrawList& out,
                               bool outline, void* uniformDst) {

        out.Clear();
        camera.SetAspect(static_cast<float>(extent.width), static_cast<float>(extent.height));   // ignores 0 height

        FrameUniforms ubo{};
        ubo.time = glm::vec4(m_time, m_deltaTime, m_shadowSettings.debugCascades ? 1.0f : 0.0f, 0.0f);
        ubo.view        = camera.GetView();
        ubo.proj        = camera.GetProjection();
        out.projInfo    = glm::vec4(ubo.proj[0][0], ubo.proj[1][1], ubo.proj[2][2], ubo.proj[3][2]);
        ubo.cameraPosWS = glm::vec4(camera.position, 0.0f);
        FillEnvironmentUniforms(ubo);

        // ---- Main pass: what the camera sees, one instanced draw per (material, mesh) run ----
        BuildVisibleItems(camera, stats);
        AppendBatches(m_visibleItems, out.main, true);
        stats.drawCalls = static_cast<uint32_t>(out.main.size());

        // ---- Shadow cascades: what each cascade's light-space box contains ----
        const ShadowSettings& settings = m_shadowSettings;
        if (settings.enabled && m_shadowLightIndex >= 0 && static_cast<uint32_t>(m_shadowLightIndex) < m_frameLightCount) {
            const uint32_t count = glm::clamp(settings.cascadeCount, 1u, kMaxShadowCascades);
            std::array<glm::mat4, kMaxShadowCascades> cascadeViewProj{};
            ComputeShadowCascades(camera, count, ubo, cascadeViewProj);

            glm::vec3 mn, mx;
            for (uint32_t c = 0; c < count; ++c) {
                m_casterItems.clear();
                const Frustum frustum = Frustum::FromViewProjection(cascadeViewProj[c]);
                m_spatial.QueryFrustum(frustum, [&](SpatialIndex::Handle h) {
                    const RenderProxy& p = m_proxies[static_cast<uint32_t>(m_spatial.GetUserData(h))];
                    m_casterItems.push_back(RenderItem{ p.mesh, MaterialHandle{}, p.model });
                });
                for (const RenderItem& item : m_renderQueue) {
                    if (GetMeshBounds(item.mesh, mn, mx) && !frustum.Intersects(AABB::Transform(AABB(mn, mx), item.model))) { continue; }
                    m_casterItems.push_back(RenderItem{ item.mesh, MaterialHandle{}, item.model });
                }
                // Depth only: the material doesn't matter, so batch by mesh alone.
                std::sort(m_casterItems.begin(), m_casterItems.end(), [](const RenderItem& a, const RenderItem& b) { return a.mesh < b.mesh; });
                stats.shadowCasters   += AppendBatches(m_casterItems, out.shadow[c], false);
                stats.shadowDrawCalls += static_cast<uint32_t>(out.shadow[c].size());
            }
            out.cascadeCount   = count;
            ubo.lightParams.y  = static_cast<uint32_t>(m_shadowLightIndex) + 1u;
            ubo.lightParams.z  = count;
        }

        // ---- Point/spot lights binned into this view's cluster grid; particles ----
        stats.clusteredLights = BuildLightClusters(camera, extent, ubo);
        PrepareParticles(camera, out, stats);
        ++m_preparedViews;

        // ---- Selection outline mask: just the selected object ----
        if (outline) {
            const uint32_t id = ResolveOutlineProxy();
            if (id != 0xFFFFFFFFu && m_proxies[id].spatial != SpatialIndex::kInvalid) {
                m_casterItems.clear();
                m_casterItems.push_back(RenderItem{ m_proxies[id].mesh, MaterialHandle{}, m_proxies[id].model });
                AppendBatches(m_casterItems, out.outline, false);
            }
        }

        memcpy(uniformDst, &ubo, sizeof(ubo));

    }   // Renderer::PrepareView()

    // ------------------------------------------------------------------------------------------------------------- //
    //  CLUSTERED LIGHTS
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::CreateClusterBuffers() {

        static_assert(kMaxClusterViews >= kMaxViewports + 1, "one cluster grid per viewport, plus the window");

        const uint32_t frames = m_frameManager.MaxFramesInFlight();
        for (uint32_t i = 0; i < frames; ++i) {
            m_clusterGridBuffers.emplace_back();
            m_clusterGridBuffers.back().Create(m_context, vk::DeviceSize{ kMaxClusterViews } * kClustersPerView * sizeof(ClusterCell),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_clusterGridMapped.push_back(m_clusterGridBuffers.back().Map());

            m_clusterIndexBuffers.emplace_back();
            m_clusterIndexBuffers.back().Create(m_context, vk::DeviceSize{ kMaxClusterIndices } * sizeof(uint32_t),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_clusterIndexMapped.push_back(m_clusterIndexBuffers.back().Map());
        }
        m_clusterCounts.resize(kClustersPerView);

    }   // Renderer::CreateClusterBuffers()

    uint32_t Renderer::BuildLightClusters(const Camera& camera, vk::Extent2D extent, FrameUniforms& ubo) {

        // The grid: kClusterX x kClusterY tiles across the screen, kClusterZ slices in depth. Slices
        // are logarithmic (each one a constant factor deeper than the last), which keeps cells
        // roughly cube-shaped from the near plane to the far plane.
        const float nearZ    = std::max(camera.nearZ, 1e-3f);
        const float farZ     = std::max(camera.farZ, nearZ * 1.01f);
        const float logScale = static_cast<float>(kClusterZ) / std::log(farZ / nearZ);
        const float tanY     = std::tan(glm::radians(camera.fovY) * 0.5f);
        const float tanX     = tanY * camera.aspect;

        const uint32_t view = std::min(m_preparedViews, kMaxClusterViews - 1);
        const uint32_t base = view * kClustersPerView;
        ubo.clusterDims = glm::uvec4(kClusterX, kClusterY, kClusterZ, base);
        ubo.clusterZ    = glm::vec4(nearZ, logScale, static_cast<float>(std::max(extent.width, 1u)), static_cast<float>(std::max(extent.height, 1u)));

        m_clusterGridScratch.resize(static_cast<size_t>(base) + kClustersPerView);   // zero-initialised cells
        std::fill(m_clusterCounts.begin(), m_clusterCounts.end(), 0u);
        m_clusterRanges.clear();
        m_clusterRangeLights.clear();

        // Pass 1: for each point/spot light, the box of cells its sphere of influence can touch.
        // A spot light is treated as its whole sphere (conservative: a few extra cells, never a
        // missing one).
        for (uint32_t i = m_frameDirLightCount; i < m_frameLightCount; ++i) {

            const GpuLight& light = m_frameLights[i];
            const glm::vec3 c = glm::vec3(ubo.view * glm::vec4(glm::vec3(light.positionRange), 1.0f));
            const float r = light.positionRange.w;
            const float z = -c.z;   // view space looks down -Z; z is depth in front of the camera

            if (z + r <= nearZ || z - r >= farZ) { continue; }
            const float zMin = std::max(z - r, nearZ);
            const float zMax = std::min(z + r, farZ);

            // Screen extent of the sphere's view-space bounding box, as NDC (-1..1). A positive edge
            // is widest on screen where the box is nearest; a negative edge where it is farthest
            // from the centre line - so each edge is projected at whichever depth pushes it outward.
            const auto project = [](float lo, float hi, float zNear, float zFar, float tanHalf, float& outLo, float& outHi) {
                outLo = lo / ((lo < 0.0f ? zNear : zFar) * tanHalf);
                outHi = hi / ((hi > 0.0f ? zNear : zFar) * tanHalf);
            };
            float x0, x1, y0, y1;
            project(c.x - r, c.x + r, zMin, zMax, tanX, x0, x1);
            project(c.y - r, c.y + r, zMin, zMax, tanY, y0, y1);
            if (x1 <= -1.0f || x0 >= 1.0f || y1 <= -1.0f || y0 >= 1.0f) { continue; }   // off screen

            const auto tile = [](float ndc, uint32_t count) {
                const float t = (ndc * 0.5f + 0.5f) * static_cast<float>(count);
                return static_cast<uint16_t>(glm::clamp(static_cast<int>(std::floor(t)), 0, static_cast<int>(count) - 1));
            };
            const auto slice = [&](float depth) {
                const float s = std::log(depth / nearZ) * logScale;
                return static_cast<uint16_t>(glm::clamp(static_cast<int>(std::floor(s)), 0, static_cast<int>(kClusterZ) - 1));
            };
            const std::array<uint16_t, 6> range{ tile(x0, kClusterX), tile(x1, kClusterX), tile(y0, kClusterY), tile(y1, kClusterY), slice(zMin), slice(zMax) };

            for (uint32_t cz = range[4]; cz <= range[5]; ++cz) {
                for (uint32_t cy = range[2]; cy <= range[3]; ++cy) {
                    uint32_t* row = &m_clusterCounts[cz * kClusterX * kClusterY + cy * kClusterX];
                    for (uint32_t cx = range[0]; cx <= range[1]; ++cx) { ++row[cx]; }
                }
            }
            m_clusterRanges.push_back(range);
            m_clusterRangeLights.push_back(i);
        }

        // Pass 2: give every cell its slice of the index buffer. If the frame's budget of light
        // references runs out, the remaining cells simply get fewer (or no) lights.
        uint32_t next = static_cast<uint32_t>(m_clusterIndexScratch.size());
        ClusterCell* cells = &m_clusterGridScratch[base];
        for (uint32_t cell = 0; cell < kClustersPerView; ++cell) {
            const uint32_t want = m_clusterCounts[cell];
            const uint32_t room = kMaxClusterIndices - next;
            cells[cell].offset = next;
            cells[cell].count  = 0;                       // refilled in pass 3
            m_clusterCounts[cell] = std::min(want, room); // now: capacity of this cell
            next += m_clusterCounts[cell];
        }
        m_clusterIndexScratch.resize(next);

        // Pass 3: write each light's index into every cell of its range.
        for (size_t n = 0; n < m_clusterRanges.size(); ++n) {
            const auto&    range = m_clusterRanges[n];
            const uint32_t light = m_clusterRangeLights[n];
            for (uint32_t cz = range[4]; cz <= range[5]; ++cz) {
                for (uint32_t cy = range[2]; cy <= range[3]; ++cy) {
                    for (uint32_t cx = range[0]; cx <= range[1]; ++cx) {
                        const uint32_t cell = cz * kClusterX * kClusterY + cy * kClusterX + cx;
                        if (cells[cell].count < m_clusterCounts[cell]) {
                            m_clusterIndexScratch[cells[cell].offset + cells[cell].count++] = light;
                        }
                    }
                }
            }
        }

        return static_cast<uint32_t>(m_clusterRanges.size());

    }   // Renderer::BuildLightClusters()

    void Renderer::UploadClusters(uint32_t frameIndex) {

        if (!m_clusterGridScratch.empty()) {
            const size_t cells = std::min<size_t>(m_clusterGridScratch.size(), size_t{ kMaxClusterViews } * kClustersPerView);
            memcpy(m_clusterGridMapped[frameIndex], m_clusterGridScratch.data(), cells * sizeof(ClusterCell));
        }
        if (!m_clusterIndexScratch.empty()) {
            memcpy(m_clusterIndexMapped[frameIndex], m_clusterIndexScratch.data(), m_clusterIndexScratch.size() * sizeof(uint32_t));
        }

    }   // Renderer::UploadClusters()

    // ------------------------------------------------------------------------------------------------------------- //
    //  PARTICLES
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::SubmitParticles(const ParticleDrawDesc& desc, const GpuParticle* particles, uint32_t count) {

        if (count == 0 || particles == nullptr) { return; }
        const uint32_t first = static_cast<uint32_t>(m_particleQueue.size());
        m_particleQueue.insert(m_particleQueue.end(), particles, particles + count);
        m_particleSubmits.push_back(ParticleSubmit{ desc, first, count });

    }   // Renderer::SubmitParticles()

    void Renderer::CreateParticleResources() {

        const uint32_t frames = m_frameManager.MaxFramesInFlight();
        for (uint32_t i = 0; i < frames; ++i) {
            m_particleBuffers.emplace_back();
            m_particleBuffers.back().Create(m_context, vk::DeviceSize{ kInitialParticleCapacity } * sizeof(GpuParticle),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_particleBuffersMapped.push_back(m_particleBuffers.back().Map());
            m_particleCapacity.push_back(kInitialParticleCapacity);
        }

        // One set per texture that particles use (binding 0 = the texture).
        vk::DescriptorPoolSize size{ .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = kMaxParticleTextures };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = kMaxParticleTextures,
            .poolSizeCount = 1, .pPoolSizes = &size };
        m_particleTexturePool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

    }   // Renderer::CreateParticleResources()

    void Renderer::PrepareParticles(const Camera& camera, ViewDrawList& out, RenderCullStats& stats) {

        if (m_particleSubmits.empty()) { return; }

        const Frustum   frustum = Frustum::FromViewProjection(camera.GetProjection() * camera.GetView());
        const glm::vec3 eye     = camera.position;

        // Visible batches, far to near: blending is only right when what is behind is drawn first.
        m_particleOrder.clear();
        for (uint32_t i = 0; i < m_particleSubmits.size(); ++i) {
            const ParticleDrawDesc& desc = m_particleSubmits[i].desc;
            if (!frustum.Intersects(AABB(desc.boundsMin, desc.boundsMax))) { continue; }
            const glm::vec3 d = (desc.boundsMin + desc.boundsMax) * 0.5f - eye;
            m_particleOrder.emplace_back(glm::dot(d, d), i);
        }
        std::sort(m_particleOrder.begin(), m_particleOrder.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

        for (const auto& entry : m_particleOrder) {

            const ParticleSubmit& submit = m_particleSubmits[entry.second];
            if (m_particleScratch.size() + submit.count > kMaxParticlesPerFrame) {
                if (!m_warnedParticleOverflow) {
                    CRU_WARN("[Renderer] More than {} particles drawn in one frame; the rest are dropped.", kMaxParticlesPerFrame);
                    m_warnedParticleOverflow = true;
                }
                break;
            }

            const uint32_t first = static_cast<uint32_t>(m_particleScratch.size());
            m_particleScratch.insert(m_particleScratch.end(),
                m_particleQueue.begin() + submit.first, m_particleQueue.begin() + submit.first + submit.count);

            // Additive particles sum to the same result in any order. Alpha-blended ones don't, so
            // this view gets its own copy sorted back to front.
            if (!submit.desc.additive && submit.count > 1) {
                std::sort(m_particleScratch.begin() + first, m_particleScratch.end(), [&](const GpuParticle& a, const GpuParticle& b) {
                    const glm::vec3 da = glm::vec3(a.posSize) - eye, db = glm::vec3(b.posSize) - eye;
                    return glm::dot(da, da) > glm::dot(db, db);
                });
            }

            const bool textured = submit.desc.texture.IsValid() && submit.desc.texture.id < m_textures.size();
            ParticleBatch batch;
            batch.texture = textured ? submit.desc.texture : m_defaultWhiteTexture;
            batch.first   = first;
            batch.count   = submit.count;
            batch.push.params = glm::vec4(
                static_cast<float>(std::max(submit.desc.flipbookCols, 1u)), static_cast<float>(std::max(submit.desc.flipbookRows, 1u)),
                submit.desc.additive ? 1.0f : 0.0f, textured ? 1.0f : 0.0f);
            out.particles.push_back(batch);

            stats.particles += submit.count;
            ++stats.particleDrawCalls;
        }

    }   // Renderer::PrepareParticles()

    void Renderer::UploadParticles(uint32_t frameIndex) {

        const size_t count = m_particleScratch.size();

        // Same growth rule and safety argument as UploadInstances().
        if (count > m_particleCapacity[frameIndex]) {
            uint32_t capacity = m_particleCapacity[frameIndex];
            while (capacity < count) { capacity *= 2; }
            m_particleBuffers[frameIndex].Create(m_context, vk::DeviceSize{ capacity } * sizeof(GpuParticle),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_particleBuffersMapped[frameIndex] = m_particleBuffers[frameIndex].Map();
            m_particleCapacity[frameIndex]      = capacity;

            vk::DescriptorBufferInfo info{ .buffer = *m_particleBuffers[frameIndex].GetBuffer(), .offset = 0, .range = vk::WholeSize };
            auto repoint = [&](const vk::raii::DescriptorSet& set) {
                vk::WriteDescriptorSet write{
                    .dstSet = *set, .dstBinding = 5, .dstArrayElement = 0, .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &info };
                m_context.GetDevice().updateDescriptorSets(write, {});
            };
            repoint(m_descriptorSets[frameIndex]);
            for (auto& [id, vp] : m_viewports) { repoint(vp.descriptorSets[frameIndex]); }
        }

        if (count > 0) { memcpy(m_particleBuffersMapped[frameIndex], m_particleScratch.data(), count * sizeof(GpuParticle)); }

    }   // Renderer::UploadParticles()

    vk::DescriptorSet Renderer::GetParticleTextureSet(TextureHandle texture) {

        if (!texture.IsValid() || texture.id >= m_textures.size()) { texture = m_defaultWhiteTexture; }

        if (auto it = m_particleTextureSets.find(texture.id); it != m_particleTextureSets.end()) { return *it->second; }

        // Pool full (more than kMaxParticleTextures different particle textures): fall back to the
        // white texture's set, which is created first and so always exists.
        if (m_particleTextureSets.size() >= kMaxParticleTextures) {
            return *m_particleTextureSets.at(m_defaultWhiteTexture.id);
        }

        vk::DescriptorSetLayout layout = *m_pipeline.GetSingleTextureSetLayout();
        vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = m_particleTexturePool, .descriptorSetCount = 1, .pSetLayouts = &layout };
        vk::raii::DescriptorSet set = std::move(m_context.GetDevice().allocateDescriptorSets(allocInfo).front());

        // Clamped sampling (the viewport sampler), so a flipbook frame never bleeds in from the
        // opposite edge of the atlas.
        vk::DescriptorImageInfo imageInfo{
            .sampler = *m_viewportSampler, .imageView = *m_textures[texture.id].view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::WriteDescriptorSet write{
            .dstSet = *set, .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageInfo };
        m_context.GetDevice().updateDescriptorSets(write, {});

        return *m_particleTextureSets.emplace(texture.id, std::move(set)).first->second;

    }   // Renderer::GetParticleTextureSet()

    void Renderer::DrawParticles(vk::CommandBuffer cmd, vk::DescriptorSet frameSet, const std::vector<ParticleBatch>& batches) {

        if (batches.empty()) { return; }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetParticlePipeline(m_drawSamples));
        // Set 0 again: this layout's push-constant range differs from the previous pipeline's, so
        // the earlier binding doesn't carry over.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetParticleLayout(), 0, frameSet, nullptr);

        TextureHandle bound;
        for (const ParticleBatch& batch : batches) {
            if (!(batch.texture == bound)) {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetParticleLayout(), 1, GetParticleTextureSet(batch.texture), nullptr);
                bound = batch.texture;
            }
            cmd.pushConstants<ParticlePush>(*m_pipeline.GetParticleLayout(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, batch.push);
            // Six vertices per particle, no vertex buffer; the vertex index (which includes
            // firstVertex) tells the shader which particle and which corner.
            cmd.draw(batch.count * 6, 1, batch.first * 6, 0);
        }

    }   // Renderer::DrawParticles()

    // ------------------------------------------------------------------------------------------------------------- //
    //  POST-PROCESSING: HDR SCENE IMAGE -> BLOOM -> TONEMAP
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::CreatePostDescriptorPool() {

        // Per PostTargets: the HDR image as a bloom source (1 set, 1 image), one set per bloom
        // step, and the tonemap set (2 images). One PostTargets per viewport, plus the window's.
        const uint32_t targets = kMaxViewports + 1;
        // ... plus ambient occlusion: its depth set (1 image) and its blur set (2 images).
        const uint32_t sets    = targets * (kBloomMips + 4);
        vk::DescriptorPoolSize size{ .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = targets * (kBloomMips + 6) };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = sets,
            .poolSizeCount = 1, .pPoolSizes = &size };
        m_postDescriptorPool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

    }   // Renderer::CreatePostDescriptorPool()

    void Renderer::CreatePostTargets(PostTargets& t, uint32_t width, uint32_t height) {

        width  = std::max(width, 1u);
        height = std::max(height, 1u);
        t.extent = vk::Extent2D{ width, height };

        const vk::Format format = VulkanPipeline::kSceneColorFormat;
        const auto makeView = [&](VulkanImage& image) {
            vk::ImageViewCreateInfo info{
                .image = image.GetImage(), .viewType = vk::ImageViewType::e2D, .format = format,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 } };
            return vk::raii::ImageView(m_context.GetDevice(), info);
        };

        t.hdrImage.Create(m_context, width, height, format, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        t.hdrView = makeView(t.hdrImage);

        // MSAA targets: what the scene is actually drawn into when samples > 1. Never sampled or
        // kept (they resolve into hdrImage and the view's depth image at the end of the pass).
        t.samples       = m_msaaSamples;
        t.msaaColorView = nullptr;
        t.msaaDepthView = nullptr;
        t.msaaColor     = VulkanImage{};
        t.msaaDepth     = VulkanImage{};
        if (t.samples > 1) {
            const vk::SampleCountFlagBits samples     = static_cast<vk::SampleCountFlagBits>(t.samples);
            const vk::Format              depthFormat = m_swapchain.GetDepthFormat();
            t.msaaColor.Create(m_context, width, height, format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, 1, {}, 1, samples);
            t.msaaColorView = makeView(t.msaaColor);
            t.msaaDepth.Create(m_context, width, height, depthFormat, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, 1, {}, 1, samples);
            vk::ImageViewCreateInfo depthViewInfo{
                .image = t.msaaDepth.GetImage(), .viewType = vk::ImageViewType::e2D, .format = depthFormat,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1 } };
            t.msaaDepthView = vk::raii::ImageView(m_context.GetDevice(), depthViewInfo);
        }

        // Bloom chain: each step half the size of the one before. Tiny targets get fewer steps.
        uint32_t steps = 1;
        while (steps < kBloomMips && (std::min(width, height) >> (steps + 1)) >= 2) { ++steps; }
        t.bloomCount = steps;
        for (uint32_t i = 0; i < steps; ++i) {
            t.bloomExtents[i] = vk::Extent2D{ std::max(width >> (i + 1), 1u), std::max(height >> (i + 1), 1u) };
            t.bloomImages[i].Create(m_context, t.bloomExtents[i].width, t.bloomExtents[i].height, format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            t.bloomViews[i] = makeView(t.bloomImages[i]);
        }

        // Ambient occlusion, at half size.
        t.aoExtent = vk::Extent2D{ std::max(width / 2, 1u), std::max(height / 2, 1u) };
        t.aoDepthView = nullptr;
        t.aoRawView   = nullptr;
        t.aoBlurView  = nullptr;
        t.aoDepth.Create(m_context, t.aoExtent.width, t.aoExtent.height, VulkanPipeline::kAoDepthFormat, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        {
            vk::ImageViewCreateInfo info{
                .image = t.aoDepth.GetImage(), .viewType = vk::ImageViewType::e2D, .format = VulkanPipeline::kAoDepthFormat,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1 } };
            t.aoDepthView = vk::raii::ImageView(m_context.GetDevice(), info);
        }
        const auto makeAoImage = [&](VulkanImage& image) {
            image.Create(m_context, t.aoExtent.width, t.aoExtent.height, VulkanPipeline::kAoFormat, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            vk::ImageViewCreateInfo info{
                .image = image.GetImage(), .viewType = vk::ImageViewType::e2D, .format = VulkanPipeline::kAoFormat,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 } };
            return vk::raii::ImageView(m_context.GetDevice(), info);
        };
        t.aoRawView  = makeAoImage(t.aoRaw);
        t.aoBlurView = makeAoImage(t.aoBlur);

        // The sets are allocated once and re-pointed whenever the images are recreated.
        if (!*t.hdrSet) {
            const auto allocate = [&](const vk::raii::DescriptorSetLayout& layout) {
                vk::DescriptorSetLayout raw = *layout;
                vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = m_postDescriptorPool, .descriptorSetCount = 1, .pSetLayouts = &raw };
                return std::move(m_context.GetDevice().allocateDescriptorSets(allocInfo).front());
            };
            t.hdrSet = allocate(m_pipeline.GetSingleTextureSetLayout());
            for (auto& set : t.bloomSets) { set = allocate(m_pipeline.GetSingleTextureSetLayout()); }
            t.tonemapSet = allocate(m_pipeline.GetTonemapDescriptorSetLayout());
            t.aoDepthSet = allocate(m_pipeline.GetSingleTextureSetLayout());
            t.aoBlurSet  = allocate(m_pipeline.GetTonemapDescriptorSetLayout());
        }

        const auto write = [&](const vk::raii::DescriptorSet& set, uint32_t binding, const vk::raii::ImageView& view) {
            vk::DescriptorImageInfo imageInfo{
                .sampler = *m_viewportSampler, .imageView = *view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            vk::WriteDescriptorSet w{
                .dstSet = *set, .dstBinding = binding, .dstArrayElement = 0, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageInfo };
            m_context.GetDevice().updateDescriptorSets(w, {});
        };
        write(t.hdrSet, 0, t.hdrView);
        for (uint32_t i = 0; i < steps; ++i) { write(t.bloomSets[i], 0, t.bloomViews[i]); }
        write(t.tonemapSet, 0, t.hdrView);
        write(t.tonemapSet, 1, t.bloomViews[0]);

        // Depth is read unfiltered (m_pointSampler); the raw occlusion with the usual sampler.
        const auto writeDepth = [&](const vk::raii::DescriptorSet& set, uint32_t binding) {
            vk::DescriptorImageInfo imageInfo{
                .sampler = *m_pointSampler, .imageView = *t.aoDepthView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            vk::WriteDescriptorSet w{
                .dstSet = *set, .dstBinding = binding, .dstArrayElement = 0, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageInfo };
            m_context.GetDevice().updateDescriptorSets(w, {});
        };
        writeDepth(t.aoDepthSet, 0);
        write(t.aoBlurSet, 0, t.aoRawView);
        writeDepth(t.aoBlurSet, 1);

    }   // Renderer::CreatePostTargets()

    void Renderer::DrawAmbientOcclusion(FrameContext& fc, PostTargets& t, vk::DescriptorSet frameSet, const ViewDrawList& draws) {

        if (!*t.aoBlurView) { return; }
        vk::CommandBuffer cmd = *fc.frame.commandBuffer;
        const vk::Extent2D extent = t.aoExtent;

        const auto beginColour = [&](VulkanImage& image, const vk::raii::ImageView& view) {
            TransitionImageLayout(fc, image.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                {}, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput);
            vk::RenderingAttachmentInfo color{
                .imageView   = view,
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp      = vk::AttachmentLoadOp::eClear,
                .storeOp     = vk::AttachmentStoreOp::eStore,
                .clearValue  = vk::ClearColorValue(1.0f, 1.0f, 1.0f, 1.0f)   // white = no occlusion
            };
            vk::RenderingInfo renderingInfo{
                .renderArea = { .offset = { 0, 0 }, .extent = extent }, .layerCount = 1,
                .colorAttachmentCount = 1, .pColorAttachments = &color };
            cmd.beginRendering(renderingInfo);
            cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
            cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
        };
        const auto endColour = [&](VulkanImage& image) {
            cmd.endRendering();
            TransitionImageLayout(fc, image.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader);
        };

        // Off: the scene shader still samples the image, so hand it an all-white one.
        if (!m_postSettings.ao) {
            beginColour(t.aoBlur, t.aoBlurView);
            endColour(t.aoBlur);
            return;
        }

        // 1. The scene's depth, at half size: every mesh the main pass will draw, depth only.
        TransitionImageLayout(fc, t.aoDepth.GetImage(), vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
            {}, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests);
        {
            vk::RenderingAttachmentInfo depth{
                .imageView   = t.aoDepthView,
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp      = vk::AttachmentLoadOp::eClear,
                .storeOp     = vk::AttachmentStoreOp::eStore,
                .clearValue  = vk::ClearDepthStencilValue(1.0f, 0)
            };
            vk::RenderingInfo renderingInfo{
                .renderArea = { .offset = { 0, 0 }, .extent = extent }, .layerCount = 1, .pDepthAttachment = &depth };
            cmd.beginRendering(renderingInfo);
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetDepthPrepassPipeline());
            // Flipped like the main pass, so its pixels line up with the scene's.
            cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(extent.height), static_cast<float>(extent.width),
                                            -static_cast<float>(extent.height), 0.0f, 1.0f));
            cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetOutlineMaskLayout(), 0, frameSet, nullptr);
            DrawMeshBatches(cmd, draws.main);
            cmd.endRendering();
        }
        TransitionImageLayout(fc, t.aoDepth.GetImage(), vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eFragmentShader);

        PostPush push;
        push.a = glm::vec4(1.0f / static_cast<float>(extent.width), 1.0f / static_cast<float>(extent.height),
                           std::max(m_postSettings.aoRadius, 0.01f), std::max(m_postSettings.aoIntensity, 0.0f));
        push.b = draws.projInfo;

        // 2. Occlusion from the depth image.
        beginColour(t.aoRaw, t.aoRawView);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetAoPipeline());
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetBloomLayout(), 0, *t.aoDepthSet, nullptr);
        cmd.pushConstants<PostPush>(*m_pipeline.GetBloomLayout(), vk::ShaderStageFlagBits::eFragment, 0, push);
        cmd.draw(3, 1, 0, 0);
        endColour(t.aoRaw);

        // 3. Smoothed.
        beginColour(t.aoBlur, t.aoBlurView);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetAoBlurPipeline());
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetTonemapLayout(), 0, *t.aoBlurSet, nullptr);
        cmd.pushConstants<PostPush>(*m_pipeline.GetTonemapLayout(), vk::ShaderStageFlagBits::eFragment, 0, push);
        cmd.draw(3, 1, 0, 0);
        endColour(t.aoBlur);

    }   // Renderer::DrawAmbientOcclusion()

    // ------------------------------------------------------------------------------------------------------------- //
    //  TIMINGS AND THE LOAD TEST
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::CreateTimestampPool() {

        // Timestamps are optional: without them the GPU half of RenderFrameStats stays empty.
        const vk::PhysicalDeviceLimits limits = m_context.GetPhysicalDevice().getProperties().limits;
        if (!limits.timestampComputeAndGraphics || limits.timestampPeriod <= 0.0f) { return; }

        const uint32_t frames = m_frameManager.MaxFramesInFlight();
        vk::QueryPoolCreateInfo info{ .queryType = vk::QueryType::eTimestamp, .queryCount = kMaxGpuMarks * frames };
        m_timestampPool     = vk::raii::QueryPool(m_context.GetDevice(), info);
        m_timestampPeriodNs = limits.timestampPeriod;
        m_gpuMarks.resize(frames);

    }   // Renderer::CreateTimestampPool()

    void Renderer::MarkGpu(vk::CommandBuffer cmd, GpuSection section) {

        if (!*m_timestampPool) { return; }
        std::vector<GpuSection>& marks = m_gpuMarks[m_markFrame];
        if (marks.size() >= kMaxGpuMarks) { return; }

        // "Bottom of pipe": the time at which everything recorded before this point has finished.
        cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *m_timestampPool,
                           m_markFrame * kMaxGpuMarks + static_cast<uint32_t>(marks.size()));
        marks.push_back(section);

    }   // Renderer::MarkGpu()

    void Renderer::ReadGpuTimings(uint32_t frameIndex) {

        if (!*m_timestampPool) { return; }
        const std::vector<GpuSection>& marks = m_gpuMarks[frameIndex];
        if (marks.size() < 2) { return; }

        // Two numbers per timestamp: the time, and whether it has been written. A frame that was
        // recorded but never submitted leaves them unwritten; skip it rather than wait.
        const uint32_t count = static_cast<uint32_t>(marks.size());
        std::vector<uint64_t> data(static_cast<size_t>(count) * 2);
        const vk::Result result = (*m_context.GetDevice()).getQueryPoolResults(
            *m_timestampPool, frameIndex * kMaxGpuMarks, count,
            data.size() * sizeof(uint64_t), data.data(), 2 * sizeof(uint64_t),
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
        if (result != vk::Result::eSuccess) { return; }
        for (uint32_t i = 0; i < count; ++i) { if (data[static_cast<size_t>(i) * 2 + 1] == 0) { return; } }

        // The time between two marks belongs to the section the earlier one started.
        float sections[static_cast<size_t>(GpuSection::Count)]{};
        float total = 0.0f;
        for (uint32_t i = 0; i + 1 < count; ++i) {
            const uint64_t a = data[static_cast<size_t>(i) * 2], b = data[static_cast<size_t>(i + 1) * 2];
            if (b <= a) { continue; }
            const float ms = static_cast<float>(static_cast<double>(b - a) * static_cast<double>(m_timestampPeriodNs) * 1e-6);
            sections[static_cast<size_t>(marks[i])] += ms;
            total += ms;
        }
        for (size_t i = 0; i < static_cast<size_t>(GpuSection::Count); ++i) {
            m_frameStats.gpuMs[i] += (sections[i] - m_frameStats.gpuMs[i]) * 0.1f;
        }
        m_frameStats.gpuTotalMs += (total - m_frameStats.gpuTotalMs) * 0.1f;
        m_frameStats.gpuValid = true;

    }   // Renderer::ReadGpuTimings()

    void Renderer::UpdateStressTest() {

        const uint32_t count   = std::min(m_stressSettings.count, 100000u);
        const float    spacing = std::max(m_stressSettings.spacing, 0.5f);

        // (Re)build when the count or spacing changed.
        bool rebuilt = false;
        if (count != m_stressProxies.size() || spacing != m_stressBuiltSpacing) {
            for (RenderProxyHandle handle : m_stressProxies) { DestroyRenderProxy(handle); }
            m_stressProxies.clear();
            m_stressBuiltSpacing = spacing;
            if (count == 0) { return; }

            // A handful of materials, so batches form the way a real scene's would: plain, rough,
            // metallic and one that glows.
            if (m_stressMaterials.empty()) {
                const auto make = [&](glm::vec3 tint, float metallic, float roughness, glm::vec3 emissive) {
                    MaterialData data;
                    data.factors.albedoTint      = glm::vec4(tint, 1.0f);
                    data.factors.metallicFactor  = metallic;
                    data.factors.roughnessFactor = roughness;
                    data.factors.emissiveFactor  = emissive;
                    m_stressMaterials.push_back(CreateMaterial(data));
                };
                make({ 0.55f, 0.52f, 0.48f }, 0.0f, 0.85f, glm::vec3(0.0f));
                make({ 0.35f, 0.38f, 0.42f }, 0.0f, 0.60f, glm::vec3(0.0f));
                make({ 0.80f, 0.45f, 0.25f }, 0.0f, 0.70f, glm::vec3(0.0f));
                make({ 0.90f, 0.90f, 0.92f }, 1.0f, 0.25f, glm::vec3(0.0f));
                make({ 0.95f, 0.75f, 0.35f }, 1.0f, 0.45f, glm::vec3(0.0f));
                make({ 0.10f, 0.10f, 0.10f }, 0.0f, 0.90f, glm::vec3(3.0f, 0.9f, 0.3f));
            }
            m_stressProxies.reserve(count);
            for (uint32_t i = 0; i < count; ++i) { m_stressProxies.push_back(CreateRenderProxy(0)); }   // 0 = no entity behind it
            rebuilt = true;
        }
        if (count == 0 || m_primitiveMeshes.empty()) { return; }

        // Standing still, the proxies only need placing once (and once more after motion stops).
        const bool moving = m_stressSettings.moving;
        if (!rebuilt && !moving && !m_stressWasMoving) { return; }
        m_stressWasMoving = moving;

        const uint32_t side = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(count))));
        const float    half = static_cast<float>(side) * 0.5f;
        const float    t    = moving ? m_time : 0.0f;
        for (uint32_t i = 0; i < count; ++i) {
            const float phase = static_cast<float>(i) * 0.61803f;
            const glm::vec3 base((static_cast<float>(i % side) - half) * spacing, 1.0f, (static_cast<float>(i / side) - half) * spacing);
            // Each object walks a 6 m circle at about 3 m/s (a horde's pace), with a small bob. It has
            // to actually travel: the spatial index only treats an object as moving once it keeps
            // leaving its padded box, and a wobble on the spot never does.
            const glm::vec3 offset(std::sin(t * 0.5f + phase) * 6.0f, std::sin(t * 1.7f + phase) * 0.25f, std::cos(t * 0.5f + phase) * 6.0f);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), base + offset);
            model = glm::rotate(model, t * 0.5f + phase, glm::vec3(0.0f, 1.0f, 0.0f));
            SetRenderProxy(m_stressProxies[i], m_primitiveMeshes[i % m_primitiveMeshes.size()],
                           m_stressMaterials[(i * 7u) % m_stressMaterials.size()], model, true);
        }

    }   // Renderer::UpdateStressTest()

    uint32_t Renderer::SupportedMsaaSamples(uint32_t wanted) const {

        const vk::PhysicalDeviceLimits limits = m_context.GetPhysicalDevice().getProperties().limits;
        const vk::SampleCountFlags     both   = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts;
        if (wanted >= 4 && (both & vk::SampleCountFlagBits::e4)) { return 4; }
        if (wanted >= 2 && (both & vk::SampleCountFlagBits::e2)) { return 2; }
        return 1;

    }   // Renderer::SupportedMsaaSamples()

    void Renderer::ApplyMsaaSetting() {

        const uint32_t wanted = SupportedMsaaSamples(m_postSettings.msaaSamples);
        if (wanted == m_msaaSamples) { return; }

        // The old targets may still be in use by frames the GPU has not finished.
        m_context.GetDevice().waitIdle();
        m_msaaSamples = wanted;
        for (auto& [id, vp] : m_viewports) {
            if (*vp.post.hdrView) { CreatePostTargets(vp.post, vp.post.extent.width, vp.post.extent.height); }
        }
        if (*m_windowPost.hdrView) { CreatePostTargets(m_windowPost, m_windowPost.extent.width, m_windowPost.extent.height); }

    }   // Renderer::ApplyMsaaSetting()

    void Renderer::DrawSceneHdr(FrameContext& fc, PostTargets& t, vk::Image depthImage, vk::ImageView depthView,
                                vk::Extent2D extent, vk::DescriptorSet frameSet, const ViewDrawList& draws) {

        vk::CommandBuffer cmd = *fc.frame.commandBuffer;

        // Last sampled by the previous frame's bloom/tonemap; its contents are re-cleared.
        TransitionImageLayout(fc, t.hdrImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
            {}, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);
        TransitionImageLayout(fc, depthImage, vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
            {}, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests);

        vk::RenderingAttachmentInfo color{
            .imageView   = t.hdrView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp      = vk::AttachmentLoadOp::eClear,
            .storeOp     = vk::AttachmentStoreOp::eStore,
            .clearValue  = vk::ClearColorValue(0.05f, 0.05f, 0.06f, 1.0f)
        };
        vk::RenderingAttachmentInfo depth{
            .imageView   = depthView,
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp      = vk::AttachmentLoadOp::eClear,
            .storeOp     = vk::AttachmentStoreOp::eStore,   // the debug lines of the final pass test against it
            .clearValue  = vk::ClearDepthStencilValue(1.0f, 0)
        };
        // MSAA: draw into the multisampled pair instead. When the pass ends the GPU resolves them
        // into the images set up above - colour by averaging the samples of each pixel (that is
        // the anti-aliasing), depth by taking one sample - so everything after this pass (bloom,
        // tonemap, debug lines, outline) carries on with ordinary one-sample images.
        const bool msaa = t.samples > 1 && *t.msaaColorView && *t.msaaDepthView;
        if (msaa) {
            TransitionImageLayout(fc, t.msaaColor.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                {}, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eColorAttachmentOutput);
            TransitionImageLayout(fc, t.msaaDepth.GetImage(), vk::ImageAspectFlagBits::eDepth,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
                {}, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests);

            color.imageView          = t.msaaColorView;
            color.resolveMode        = vk::ResolveModeFlagBits::eAverage;
            color.resolveImageView   = t.hdrView;
            color.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color.storeOp            = vk::AttachmentStoreOp::eDontCare;   // only the resolved image is kept

            depth.imageView          = t.msaaDepthView;
            depth.resolveMode        = vk::ResolveModeFlagBits::eSampleZero;
            depth.resolveImageView   = depthView;
            depth.resolveImageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
            depth.storeOp            = vk::AttachmentStoreOp::eDontCare;
        }

        vk::RenderingInfo renderingInfo{
            .renderArea           = { .offset = { 0, 0 }, .extent = extent },
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color,
            .pDepthAttachment     = &depth
        };
        cmd.beginRendering(renderingInfo);
        m_drawSamples = msaa ? t.samples : 1;
        DrawRenderQueue(cmd, extent, frameSet, draws);
        m_drawSamples = 1;
        cmd.endRendering();

        TransitionImageLayout(fc, t.hdrImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader);

        MarkGpu(cmd, GpuSection::Bloom);
        DrawBloom(fc, t);

        // The final pass reads and keeps testing this depth: make the scene pass's writes visible.
        TransitionImageLayout(fc, depthImage, vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests);

    }   // Renderer::DrawSceneHdr()

    void Renderer::DrawBloom(FrameContext& fc, PostTargets& t) {

        vk::CommandBuffer cmd = *fc.frame.commandBuffer;

        const auto toAttachment = [&](uint32_t i, bool keepContents) {
            TransitionImageLayout(fc, t.bloomImages[i].GetImage(), vk::ImageAspectFlagBits::eColor,
                keepContents ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal,
                keepContents ? vk::AccessFlagBits2::eShaderSampledRead : vk::AccessFlags2{},
                vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput);
        };
        const auto toSampled = [&](uint32_t i) {
            TransitionImageLayout(fc, t.bloomImages[i].GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader);
        };
        const auto begin = [&](uint32_t i, vk::AttachmentLoadOp loadOp) {
            vk::RenderingAttachmentInfo color{
                .imageView   = t.bloomViews[i],
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp      = loadOp,
                .storeOp     = vk::AttachmentStoreOp::eStore,
                .clearValue  = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f)
            };
            vk::RenderingInfo renderingInfo{
                .renderArea           = { .offset = { 0, 0 }, .extent = t.bloomExtents[i] },
                .layerCount           = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments    = &color
            };
            cmd.beginRendering(renderingInfo);
            // Plain (unflipped) viewport: these passes work in texture space.
            cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(t.bloomExtents[i].width), static_cast<float>(t.bloomExtents[i].height), 0.0f, 1.0f));
            cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), t.bloomExtents[i]));
        };

        // Bloom off: the tonemap pass still samples step 0, so give it a defined (black) image.
        if (!m_postSettings.bloom || t.bloomCount == 0) {
            toAttachment(0, false);
            begin(0, vk::AttachmentLoadOp::eClear);
            cmd.endRendering();
            toSampled(0);
            return;
        }

        const float exposure = std::max(m_environment.exposure, 0.0f);

        // Down the chain: each step filters the previous one into half the size. The first step
        // reads the scene and keeps only what is brighter than the threshold.
        vk::DescriptorSet source       = *t.hdrSet;
        vk::Extent2D      sourceExtent = t.extent;
        for (uint32_t i = 0; i < t.bloomCount; ++i) {
            toAttachment(i, false);
            begin(i, vk::AttachmentLoadOp::eDontCare);
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetBloomDownPipeline());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetBloomLayout(), 0, source, nullptr);
            PostPush push;
            push.a = glm::vec4(1.0f / static_cast<float>(sourceExtent.width), 1.0f / static_cast<float>(sourceExtent.height),
                               i == 0 ? std::max(m_postSettings.bloomThreshold, 0.0f) : -1.0f, std::max(m_postSettings.bloomKnee, 1e-4f));
            push.b = glm::vec4(exposure, 0.0f, 0.0f, 0.0f);
            cmd.pushConstants<PostPush>(*m_pipeline.GetBloomLayout(), vk::ShaderStageFlagBits::eFragment, 0, push);
            cmd.draw(3, 1, 0, 0);
            cmd.endRendering();
            toSampled(i);
            source       = *t.bloomSets[i];
            sourceExtent = t.bloomExtents[i];
        }

        // Back up the chain: each step is blurred up and ADDED onto the next larger one, so step 0
        // ends up holding glow at every scale - tight around small lights, wide around big ones.
        for (uint32_t i = t.bloomCount - 1; i > 0; --i) {
            toAttachment(i - 1, true);
            begin(i - 1, vk::AttachmentLoadOp::eLoad);
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetBloomUpPipeline());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetBloomLayout(), 0, *t.bloomSets[i], nullptr);
            PostPush push;
            push.a = glm::vec4(1.0f / static_cast<float>(t.bloomExtents[i].width), 1.0f / static_cast<float>(t.bloomExtents[i].height), 0.0f, 0.0f);
            cmd.pushConstants<PostPush>(*m_pipeline.GetBloomLayout(), vk::ShaderStageFlagBits::eFragment, 0, push);
            cmd.draw(3, 1, 0, 0);
            cmd.endRendering();
            toSampled(i - 1);
        }

    }   // Renderer::DrawBloom()

    void Renderer::DrawTonemap(vk::CommandBuffer cmd, const PostTargets& t, vk::Extent2D extent) {

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetTonemapPipeline());
        cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetTonemapLayout(), 0, *t.tonemapSet, nullptr);

        // The glow is the sum of bloomCount steps; dividing by that keeps bloomIntensity meaning
        // roughly the same thing whatever the render target's size.
        const bool  bloom    = m_postSettings.bloom && t.bloomCount > 0;
        const float strength = bloom ? std::max(m_postSettings.bloomIntensity, 0.0f) / static_cast<float>(t.bloomCount) : 0.0f;
        PostPush push;
        push.a = glm::vec4(std::max(m_environment.exposure, 0.0f), strength, m_postSettings.fxaa ? 1.0f : 0.0f, 0.0f);
        push.b = glm::vec4(1.0f / static_cast<float>(std::max(extent.width, 1u)), 1.0f / static_cast<float>(std::max(extent.height, 1u)), 0.0f, 0.0f);
        cmd.pushConstants<PostPush>(*m_pipeline.GetTonemapLayout(), vk::ShaderStageFlagBits::eFragment, 0, push);
        cmd.draw(3, 1, 0, 0);

    }   // Renderer::DrawTonemap()

    uint32_t Renderer::AppendBatches(const std::vector<RenderItem>& items, std::vector<DrawBatch>& batches, bool byMaterial) {

        uint32_t added = 0;
        for (const RenderItem& item : items) {

            // Skip what can't be drawn: no mesh, or one still uploading (CreateMeshAsync).
            if (!item.mesh.IsValid() || item.mesh.id >= m_meshes.size() || m_meshes[item.mesh.id].indexCount == 0) { continue; }

            // The main pass always needs SOME material set bound (the pipeline layout declares set 1
            // unconditionally), so substitute the default rather than skip. Depth-only passes don't
            // care, and leaving it Invalid lets their batches merge across materials.
            MaterialHandle material;
            if (byMaterial) {
                material = item.material.IsValid() && item.material.id < m_materials.size() ? item.material : m_defaultMaterial;
            }

            const uint32_t index = static_cast<uint32_t>(m_instanceScratch.size());
            m_instanceScratch.push_back(InstanceData{ item.model, item.params });
            ++added;

            if (!batches.empty()) {
                DrawBatch& last = batches.back();
                if (last.mesh == item.mesh && last.material == material && last.firstInstance + last.instanceCount == index) {
                    ++last.instanceCount;
                    continue;
                }
            }
            batches.push_back(DrawBatch{ item.mesh, material, index, 1 });
        }
        return added;

    }   // Renderer::AppendBatches()

    void Renderer::CreateInstanceBuffers() {

        const uint32_t frames = m_frameManager.MaxFramesInFlight();
        for (uint32_t i = 0; i < frames; ++i) {
            m_instanceBuffers.emplace_back();
            m_instanceBuffers.back().Create(m_context, vk::DeviceSize{ kInitialInstanceCapacity } * sizeof(InstanceData),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_instanceBuffersMapped.push_back(m_instanceBuffers.back().Map());
            m_instanceCapacity.push_back(kInitialInstanceCapacity);
        }
        m_instanceScratch.reserve(kInitialInstanceCapacity);

    }   // Renderer::CreateInstanceBuffers()

    void Renderer::UploadInstances(uint32_t frameIndex) {

        const size_t count = m_instanceScratch.size();

        // Grow (power of two). Safe to recreate here: BeginFrame() already waited on this frame
        // index's fence, so the GPU is done with the old buffer, and nothing recorded this frame
        // references it yet. Only this frame index's sets point at it, so only they are rewritten.
        if (count > m_instanceCapacity[frameIndex]) {
            uint32_t capacity = m_instanceCapacity[frameIndex];
            while (capacity < count) { capacity *= 2; }
            m_instanceBuffers[frameIndex].Create(m_context, vk::DeviceSize{ capacity } * sizeof(InstanceData),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_instanceBuffersMapped[frameIndex] = m_instanceBuffers[frameIndex].Map();
            m_instanceCapacity[frameIndex]      = capacity;

            vk::DescriptorBufferInfo info{ .buffer = *m_instanceBuffers[frameIndex].GetBuffer(), .offset = 0, .range = vk::WholeSize };
            auto repoint = [&](const vk::raii::DescriptorSet& set) {
                vk::WriteDescriptorSet write{
                    .dstSet = *set, .dstBinding = 2, .dstArrayElement = 0, .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &info };
                m_context.GetDevice().updateDescriptorSets(write, {});
            };
            repoint(m_descriptorSets[frameIndex]);
            for (auto& [id, vp] : m_viewports) { repoint(vp.descriptorSets[frameIndex]); }
        }

        if (count > 0) { memcpy(m_instanceBuffersMapped[frameIndex], m_instanceScratch.data(), count * sizeof(InstanceData)); }

    }   // Renderer::UploadInstances()

    void Renderer::DrawMeshBatches(vk::CommandBuffer cmd, const std::vector<DrawBatch>& batches) {

        MeshHandle bound;
        for (const DrawBatch& batch : batches) {
            const GpuMesh& mesh = m_meshes[batch.mesh.id];
            if (!(batch.mesh == bound)) {
                cmd.bindVertexBuffers(0, *mesh.vertexBuffer.GetBuffer(), { 0 });
                cmd.bindIndexBuffer(*mesh.indexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);
                bound = batch.mesh;
            }
            cmd.drawIndexed(mesh.indexCount, batch.instanceCount, 0, 0, batch.firstInstance);
        }

    }   // Renderer::DrawMeshBatches()

    // ------------------------------------------------------------------------------------------------------------- //
    //  CASCADED SHADOW MAPS
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::CreateShadowResources() {

        m_shadowImage.Create(m_context, kShadowMapSize, kShadowMapSize, kShadowFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            kMaxShadowCascades);

        // One view of every layer for sampling (Texture2DArray), one per layer to render into.
        vk::ImageViewCreateInfo arrayViewInfo{
            .image            = m_shadowImage.GetImage(),
            .viewType         = vk::ImageViewType::e2DArray,
            .format           = kShadowFormat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .baseArrayLayer = 0, .layerCount = kMaxShadowCascades }
        };
        m_shadowArrayView = vk::raii::ImageView(m_context.GetDevice(), arrayViewInfo);
        for (uint32_t layer = 0; layer < kMaxShadowCascades; ++layer) {
            vk::ImageViewCreateInfo layerViewInfo{
                .image            = m_shadowImage.GetImage(),
                .viewType         = vk::ImageViewType::e2D,
                .format           = kShadowFormat,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .baseArrayLayer = layer, .layerCount = 1 }
            };
            m_shadowLayerViews[layer] = vk::raii::ImageView(m_context.GetDevice(), layerViewInfo);
        }

        // Comparison sampler: returns the fraction of the (bilinear) 2x2 footprint whose stored depth
        // passes ref <= stored, i.e. is lit. Outside the map = border white = depth 1 = lit.
        vk::SamplerCreateInfo samplerInfo{
            .magFilter               = vk::Filter::eLinear,
            .minFilter               = vk::Filter::eLinear,
            .mipmapMode              = vk::SamplerMipmapMode::eNearest,
            .addressModeU            = vk::SamplerAddressMode::eClampToBorder,
            .addressModeV            = vk::SamplerAddressMode::eClampToBorder,
            .addressModeW            = vk::SamplerAddressMode::eClampToEdge,
            .anisotropyEnable        = vk::False,
            .compareEnable           = vk::True,
            .compareOp               = vk::CompareOp::eLessOrEqual,
            .minLod                  = 0.0f,
            .maxLod                  = 0.0f,
            .borderColor             = vk::BorderColor::eFloatOpaqueWhite,
            .unnormalizedCoordinates = vk::False
        };
        m_shadowSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

        // Rest state is SHADER_READ_ONLY (see the member comment), so a view that skips shadows can
        // still bind it. Its contents are never read until a shadow pass has written them.
        vk::raii::CommandBuffer cmd = BeginSingleTimeCommand();
        vk::ImageMemoryBarrier2 barrier{
            .srcStageMask        = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask       = {},
            .dstStageMask        = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask       = vk::AccessFlagBits2::eShaderSampledRead,
            .oldLayout           = vk::ImageLayout::eUndefined,
            .newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = m_shadowImage.GetImage(),
            .subresourceRange    = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .baseArrayLayer = 0, .layerCount = kMaxShadowCascades }
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier });
        EndSingleTimeCommand(std::move(cmd));

    }   // Renderer::CreateShadowResources()

    void Renderer::ComputeShadowCascades(const Camera& camera, uint32_t count, FrameUniforms& ubo,
                                         std::array<glm::mat4, kMaxShadowCascades>& outViewProj) const {

        const ShadowSettings& settings = m_shadowSettings;
        const float nearZ  = std::max(camera.nearZ, 1e-3f);
        const float farZ   = glm::clamp(settings.distance, nearZ + 0.1f, camera.farZ);
        const float lambda = glm::clamp(settings.splitLambda, 0.0f, 1.0f);
        const float size   = static_cast<float>(kShadowMapSize);

        // Half-diagonal of the view frustum's cross-section per unit of depth.
        const float tanHalf  = std::tan(glm::radians(camera.fovY) * 0.5f);
        const float diagRate = tanHalf * std::sqrt(1.0f + camera.aspect * camera.aspect);

        const glm::vec3 forward = camera.Forward();
        const glm::vec3 L       = glm::normalize(m_shadowLightDir);   // direction the light travels
        const glm::vec3 up      = std::abs(L.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const float     ext     = std::max(settings.casterExtension, 0.0f);

        float sliceNear = nearZ;
        for (uint32_t c = 0; c < count; ++c) {

            // "Practical" split scheme: blend of logarithmic and uniform splits.
            const float p         = static_cast<float>(c + 1) / static_cast<float>(count);
            const float logSplit  = nearZ * std::pow(farZ / nearZ, p);
            const float uniSplit  = nearZ + (farZ - nearZ) * p;
            const float sliceFar  = lambda * logSplit + (1.0f - lambda) * uniSplit;

            // Smallest sphere around the slice whose centre is on the view axis: equidistant from the
            // near and far rings, clamped into the slice. It depends only on the slice, not on where
            // the camera looks, so its size never changes as the camera turns - with the snapping
            // below, that is what keeps shadow edges from shimmering.
            const float a  = sliceNear * diagRate;   // near ring radius
            const float b  = sliceFar  * diagRate;   // far ring radius
            float centreZ  = (sliceFar * sliceFar + b * b - sliceNear * sliceNear - a * a) / (2.0f * (sliceFar - sliceNear));
            centreZ        = glm::clamp(centreZ, sliceNear, sliceFar);
            float radius   = std::max(std::sqrt((centreZ - sliceNear) * (centreZ - sliceNear) + a * a),
                                      std::sqrt((sliceFar - centreZ) * (sliceFar - centreZ) + b * b));
            radius         = std::ceil(radius * 16.0f) / 16.0f;   // quantize against float drift

            const glm::vec3 centre = camera.position + forward * centreZ;

            // Ortho box around the sphere, pulled back toward the light by `ext` so casters between
            // the light and the sphere (off-screen towers, cliffs) still land in the map.
            const glm::mat4 lightView = glm::lookAt(centre - L * (radius + ext), centre, up);
            // orthoRH_ZO, not glm::ortho: the project does not define GLM_FORCE_DEPTH_ZERO_TO_ONE, so
            // glm::ortho maps depth to -1..1 and Vulkan (which keeps 0..1) would clip away the half
            // of the box nearest the light - losing the shadows of anything high above its receiver.
            glm::mat4       lightProj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + ext);

            // Texel snapping: shift the projection so the world origin lands exactly on a texel. The
            // map then only ever moves in whole texels as the camera moves.
            glm::vec4 origin = lightProj * lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            origin *= size * 0.5f;
            const glm::vec2 snapOffset = (glm::round(glm::vec2(origin)) - glm::vec2(origin)) * (2.0f / size);
            lightProj[3][0] += snapOffset.x;
            lightProj[3][1] += snapOffset.y;

            outViewProj[c]           = lightProj * lightView;
            ubo.shadowViewProj[c]    = outViewProj[c];
            ubo.cascadeSplits[c]     = sliceFar;
            ubo.cascadeTexelSize[c]  = 2.0f * radius / size;

            sliceNear = sliceFar;
        }

        ubo.shadowParams = glm::vec4(1.0f / size, settings.depthBias, settings.normalBias, glm::clamp(settings.fadeFraction, 0.0f, 1.0f));

    }   // Renderer::ComputeShadowCascades()

    void Renderer::DrawShadowCascades(FrameContext& fc, vk::DescriptorSet frameSet, const ViewDrawList& draws) {

        if (draws.cascadeCount == 0) { return; }
        vk::CommandBuffer cmd = *fc.frame.commandBuffer;

        // Shared by every view: wait for whatever sampled it last (an earlier view this frame, or a
        // previous frame - barriers cover all earlier submissions on the queue too). Its old
        // contents are discarded (UNDEFINED) since every layer is cleared below.
        TransitionImageLayout(fc, m_shadowImage.GetImage(), vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
            {}, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            kMaxShadowCascades);

        const vk::Extent2D extent{ kShadowMapSize, kShadowMapSize };
        for (uint32_t c = 0; c < draws.cascadeCount; ++c) {

            vk::RenderingAttachmentInfo depth{
                .imageView   = m_shadowLayerViews[c],
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp      = vk::AttachmentLoadOp::eClear,
                .storeOp     = vk::AttachmentStoreOp::eStore,
                .clearValue  = vk::ClearDepthStencilValue(1.0f, 0)
            };
            vk::RenderingInfo renderingInfo{
                .renderArea       = { .offset = { 0, 0 }, .extent = extent },
                .layerCount       = 1,
                .pDepthAttachment = &depth
            };
            cmd.beginRendering(renderingInfo);

            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetShadowPipeline());
            // Same flipped viewport as every other pass, so the shader's uv mapping is uniform.
            cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize),
                                            -static_cast<float>(kShadowMapSize), 0.0f, 1.0f));
            cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetShadowLayout(), 0, frameSet, nullptr);
            cmd.pushConstants<uint32_t>(*m_pipeline.GetShadowLayout(), vk::ShaderStageFlagBits::eVertex, 0, c);
            DrawMeshBatches(cmd, draws.shadow[c]);

            cmd.endRendering();
        }

        TransitionImageLayout(fc, m_shadowImage.GetImage(), vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eLateFragmentTests, vk::PipelineStageFlagBits2::eFragmentShader,
            kMaxShadowCascades);

    }   // Renderer::DrawShadowCascades()

    // ------------------------------------------------------------------------------------------------------------- //
    //  SELECTION OUTLINE
    // ------------------------------------------------------------------------------------------------------------- //

    uint32_t Renderer::ResolveOutlineProxy() {

        constexpr uint32_t kNone = 0xFFFFFFFFu;
        if (m_outlineUserData == 0) { return kNone; }

        if (m_outlineProxy < m_proxies.size() && m_proxies[m_outlineProxy].alive
            && m_proxies[m_outlineProxy].userData == m_outlineUserData) {
            return m_outlineProxy;
        }
        // Stale or never looked up: one linear scan (selection changes are rare; a selected entity
        // with no proxy, e.g. a light, costs one cheap scan per frame).
        m_outlineProxy = kNone;
        for (uint32_t i = 0; i < m_proxies.size(); ++i) {
            if (m_proxies[i].alive && m_proxies[i].userData == m_outlineUserData) { m_outlineProxy = i; break; }
        }
        return m_outlineProxy;

    }   // Renderer::ResolveOutlineProxy()

    void Renderer::DrawOutlineMask(FrameContext& fc, Viewport& vp, vk::DescriptorSet frameSet) {

        vk::CommandBuffer cmd = *fc.frame.commandBuffer;

        // Last frame's composite sampled it; contents are re-cleared, so discard them.
        TransitionImageLayout(fc, vp.maskImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
            {}, vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::PipelineStageFlagBits2::eColorAttachmentOutput);

        vk::RenderingAttachmentInfo color{
            .imageView   = vp.maskView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp      = vk::AttachmentLoadOp::eClear,
            .storeOp     = vk::AttachmentStoreOp::eStore,
            .clearValue  = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f)
        };
        vk::RenderingInfo renderingInfo{
            .renderArea           = { .offset = { 0, 0 }, .extent = vp.extent },
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color
        };
        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetOutlineMaskPipeline());
        // Same flipped viewport as the main pass, so mask pixels line up with the scene's.
        cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(vp.extent.height), static_cast<float>(vp.extent.width),
                                        -static_cast<float>(vp.extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), vp.extent));
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetOutlineMaskLayout(), 0, frameSet, nullptr);
        DrawMeshBatches(cmd, vp.draws.outline);
        cmd.endRendering();

        TransitionImageLayout(fc, vp.maskImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader);

    }   // Renderer::DrawOutlineMask()

    void Renderer::DrawOutlineComposite(FrameContext& fc, Viewport& vp) {

        vk::CommandBuffer cmd = *fc.frame.commandBuffer;

        // The main pass's writes must land before blending reads them back.
        TransitionImageLayout(fc, vp.colorImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eColorAttachmentOutput);

        vk::RenderingAttachmentInfo color{
            .imageView   = vp.colorView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp      = vk::AttachmentLoadOp::eLoad,
            .storeOp     = vk::AttachmentStoreOp::eStore
        };
        vk::RenderingInfo renderingInfo{
            .renderArea           = { .offset = { 0, 0 }, .extent = vp.extent },
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color
        };
        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetOutlineCompositePipeline());
        // Fullscreen triangle: orientation doesn't matter, the shader works in pixel coordinates.
        cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), vp.extent));
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetOutlineCompositeLayout(), 0, *vp.outlineSet, nullptr);
        cmd.pushConstants<OutlineCompositePush>(*m_pipeline.GetOutlineCompositeLayout(), vk::ShaderStageFlagBits::eFragment, 0, m_outlineStyle);
        cmd.draw(3, 1, 0, 0);
        cmd.endRendering();

    }   // Renderer::DrawOutlineComposite()

    namespace {

        // Möller-Trumbore, double-sided (picking should hit a plane from below too). t is in units
        // of `dir`, so it carries over unchanged between world and object space.
        bool RayTriangle(const glm::vec3& o, const glm::vec3& dir, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& t) {
            const glm::vec3 e1 = b - a, e2 = c - a;
            const glm::vec3 p = glm::cross(dir, e2);
            const float det = glm::dot(e1, p);
            if (std::abs(det) < 1e-12f) { return false; }
            const float inv = 1.0f / det;
            const glm::vec3 s = o - a;
            const float u = glm::dot(s, p) * inv;
            if (u < 0.0f || u > 1.0f) { return false; }
            const glm::vec3 q = glm::cross(s, e1);
            const float v = glm::dot(dir, q) * inv;
            if (v < 0.0f || u + v > 1.0f) { return false; }
            t = glm::dot(e2, q) * inv;
            return t >= 0.0f;
        }

    }   // anonymous namespace

    bool Renderer::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RenderRaycastHit& outHit) const {

        const float len = glm::length(direction);
        if (len < 1e-8f || maxDistance <= 0.0f) { return false; }
        const Ray ray{ origin, direction / len };

        bool found = false;
        float best = maxDistance;

        m_spatial.RayCast(ray, maxDistance, [&](SpatialIndex::Handle h, float tBox) {
            if (tBox >= best) { return best; }
            const RenderProxy& p = m_proxies[static_cast<uint32_t>(m_spatial.GetUserData(h))];
            if (!p.mesh.IsValid() || p.mesh.id >= m_meshes.size()) { return best; }
            const GpuMesh& mesh = m_meshes[p.mesh.id];

            float tHit = -1.0f;
            if (mesh.cpuIndices.empty()) {
                tHit = tBox;   // no geometry kept: the tight box is the best we can do
            } else {
                // Into object space: t is preserved because the mapping is affine.
                const glm::mat4 inv = glm::inverse(p.model);
                const glm::vec3 o = glm::vec3(inv * glm::vec4(ray.origin, 1.0f));
                const glm::vec3 d = glm::vec3(inv * glm::vec4(ray.direction, 0.0f));
                const auto& pos = mesh.cpuPositions;
                const auto& idx = mesh.cpuIndices;
                for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                    float t;
                    if (RayTriangle(o, d, pos[idx[i]], pos[idx[i + 1]], pos[idx[i + 2]], t) && t < best && (tHit < 0.0f || t < tHit)) { tHit = t; }
                }
            }

            if (tHit >= 0.0f && tHit < best) {
                best = tHit;
                found = true;
                outHit.userData = p.userData;
                outHit.distance = tHit;
                outHit.point    = ray.origin + ray.direction * tHit;
                outHit.mesh     = p.mesh;
            }
            return best;
        });

        return found;

    }   // Renderer::Raycast()

    void Renderer::DrawProxyBoundsDebug() {

        DebugStyle settled;
        settled.category = DebugCategory::Bounds;
        settled.color    = { 0.35f, 0.55f, 1.0f, 1.0f };
        DebugStyle moving = settled;
        moving.color     = { 1.0f, 0.55f, 0.15f, 1.0f };

        for (const RenderProxy& p : m_proxies) {
            if (!p.alive || p.spatial == SpatialIndex::kInvalid) { continue; }
            const AABB& b = m_spatial.GetBounds(p.spatial);
            DebugDraw::AABB(b.min, b.max, m_spatial.IsActive(p.spatial) ? moving : settled);
        }

    }   // Renderer::DrawProxyBoundsDebug()

    void Renderer::UpdateDebugBuffer(uint32_t frameIndex) {

        DebugDraw::Collect(m_debugTested, m_debugOnTop);

        // Clamp (whole segments only) so a runaway debug loop can't allocate unbounded memory.
        size_t total = m_debugTested.size() + m_debugOnTop.size();
        if (total > kMaxDebugVertices) {
            if (!m_warnedDebugOverflow) {
                CRU_WARN("[Renderer] More than {} debug line vertices in one frame; the rest are dropped.", kMaxDebugVertices);
                m_warnedDebugOverflow = true;
            }
            const size_t keepTested = std::min<size_t>(m_debugTested.size(), kMaxDebugVertices) & ~size_t{ 1 };
            m_debugTested.resize(keepTested);
            m_debugOnTop.resize(std::min(m_debugOnTop.size(), (kMaxDebugVertices - keepTested)) & ~size_t{ 1 });
            total = m_debugTested.size() + m_debugOnTop.size();
        }

        m_debugTestedCount = static_cast<uint32_t>(m_debugTested.size());
        m_debugOnTopCount  = static_cast<uint32_t>(m_debugOnTop.size());
        if (total == 0) { return; }

        if (m_debugBuffers.empty()) {
            const uint32_t frames = m_frameManager.MaxFramesInFlight();
            m_debugBuffers.resize(frames);
            m_debugBuffersMapped.assign(frames, nullptr);
            m_debugBufferCapacity.assign(frames, 0);
        }

        // Grow this frame's buffer (power of two, at least 8K vertices). Safe to recreate here:
        // BeginFrame() already waited on this frame index's fence, so the GPU is done with it.
        if (m_debugBufferCapacity[frameIndex] < total) {
            uint32_t capacity = 8192;
            while (capacity < total) { capacity *= 2; }
            m_debugBuffers[frameIndex].Create(m_context, vk::DeviceSize{ capacity } * sizeof(DebugVertex),
                vk::BufferUsageFlagBits::eVertexBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_debugBuffersMapped[frameIndex]  = m_debugBuffers[frameIndex].Map();
            m_debugBufferCapacity[frameIndex] = capacity;
        }

        auto* dst = static_cast<DebugVertex*>(m_debugBuffersMapped[frameIndex]);
        if (!m_debugTested.empty()) { memcpy(dst, m_debugTested.data(), m_debugTested.size() * sizeof(DebugVertex)); }
        if (!m_debugOnTop.empty())  { memcpy(dst + m_debugTested.size(), m_debugOnTop.data(), m_debugOnTop.size() * sizeof(DebugVertex)); }

    }   // Renderer::UpdateDebugBuffer()

    void Renderer::DrawDebugLines(vk::CommandBuffer cmd, vk::Extent2D extent, vk::DescriptorSet frameSet) {

        if (m_debugTestedCount + m_debugOnTopCount == 0) { return; }

        // The tonemap triangle drawn just before used a plain viewport; world geometry uses the
        // flipped one (+Y up), like the scene pass.
        cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(extent.height), static_cast<float>(extent.width), -static_cast<float>(extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
        const uint32_t frameIndex = m_frameManager.GetFrameIndex();

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetDebugLineLayout(), 0, frameSet, nullptr);
        cmd.bindVertexBuffers(0, *m_debugBuffers[frameIndex].GetBuffer(), { 0 });

        if (m_debugTestedCount > 0) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetDebugLinePipeline(true));
            cmd.draw(m_debugTestedCount, 1, 0, 0);
        }
        if (m_debugOnTopCount > 0) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetDebugLinePipeline(false));
            cmd.draw(m_debugOnTopCount, 1, m_debugTestedCount, 0);
        }

    }   // Renderer::DrawDebugLines()

    void Renderer::DrawSkybox(vk::CommandBuffer cmd, vk::DescriptorSet frameSet) {

        if (!m_environment.drawSkybox) { return; }

        CubemapHandle sky = m_environment.skybox;
        if (!sky.IsValid() || sky.id >= m_cubemaps.size()) { sky = m_defaultSkybox; }
        if (!sky.IsValid() || sky.id >= m_cubemaps.size()) { return; }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetSkyboxPipeline(m_drawSamples));

        // Set 0 must be rebound even though the mesh pass just bound the same set: the skybox
        // layout has no push-constant range, so it isn't "compatible" with the main layout for
        // set 0 and the earlier binding is disturbed by this pipeline's layout.
        const std::array<vk::DescriptorSet, 2> sets{ frameSet, *m_cubemaps[sky.id].descriptorSet };
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetSkyboxLayout(), 0, sets, nullptr);
        cmd.draw(36, 1, 0, 0);

    }   // Renderer::DrawSkybox()

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

    void Renderer::TransitionImageLayout(FrameContext fc, vk::Image image, vk::ImageAspectFlags aspectMask,
        vk::ImageLayout old_layout, vk::ImageLayout new_layout,
        vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
        vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask, uint32_t layerCount) {

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
                   .layerCount = layerCount}
        };
        vk::DependencyInfo dependency_info = {
            .dependencyFlags = {},
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };
        fc.frame.commandBuffer.pipelineBarrier2(dependency_info);

    }   // Renderer::TransitionImageLayout()

}   // NAMESPACE CRUCIBLE
