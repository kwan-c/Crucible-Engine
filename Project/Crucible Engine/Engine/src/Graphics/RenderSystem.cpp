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
#include "Vulkan/VulkanCommands.h"

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
#include <memory>

#include <glm/gtc/packing.hpp>   // packHalf4x16 - RGBA16F cubemap texels

// No image library here: the engine uploads compiled textures and skies (.bill, .sky); the
// editor decodes images that are not compiled yet itself (Resources/ImageDecoders.h).

#include "Primitives.h"             // Primitives::MakeCube() and the rest of the whitebox set
#include "Diagnostics/Logger.h"

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace Crucible {

    namespace {

        // One sRGB channel (0..1) to linear, the exact curve: viewport clear colours are given in
        // sRGB like UI colours, but Vulkan clear values for an sRGB image are linear.
        float SrgbToLinear(float c) {
            return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }

    }   // namespace

    Renderer::Renderer(WindowManager& window) 
        : m_window{ window }, m_context{ m_window }, m_swapchain{ m_context, m_window } {

        m_layouts.CreateDescriptorSetLayout(m_context);
        m_layouts.CreateMaterialDescriptorSetLayout(m_context);
        m_layouts.CreateSkyboxDescriptorSetLayout(m_context);
        m_layouts.CreateTwoTextureSetLayout(m_context);
        // Passes that own their pipelines.
        m_scenePass.Create(m_context, *m_layouts.GetDescriptorSetLayout(), *m_layouts.GetMaterialDescriptorSetLayout(),
                           *m_layouts.GetSkyboxDescriptorSetLayout(), RenderLayouts::kSceneColorFormat, m_swapchain.GetDepthFormat());
        m_outlinePass.Create(m_context, *m_layouts.GetDescriptorSetLayout(), *m_layouts.GetMaterialDescriptorSetLayout(), m_swapchain.GetSurfaceFormat().format);
        m_aoPass.Create(m_context, *m_layouts.GetDescriptorSetLayout(), *m_layouts.GetMaterialDescriptorSetLayout(), *m_layouts.GetTonemapDescriptorSetLayout());
        m_bloomPass.Create(m_context, *m_layouts.GetSingleTextureSetLayout(), RenderLayouts::kSceneColorFormat);
        m_toonOutlinePass.Create(m_context, *m_layouts.GetSingleTextureSetLayout(), RenderLayouts::kSceneColorFormat);
        m_tonemapPass.Create(m_context, *m_layouts.GetTonemapDescriptorSetLayout(), m_swapchain.GetSurfaceFormat().format, m_swapchain.GetDepthFormat());
        m_shadowPass.Create(m_context, *m_layouts.GetDescriptorSetLayout(), *m_layouts.GetMaterialDescriptorSetLayout(), ShadowPass::kFormat);
        m_particlePass.Create(m_context, *m_layouts.GetDescriptorSetLayout(), *m_layouts.GetSingleTextureSetLayout(),
                              RenderLayouts::kSceneColorFormat, m_swapchain.GetDepthFormat());
        m_debugLinePass.Create(m_context, *m_layouts.GetDescriptorSetLayout(), m_swapchain.GetSurfaceFormat().format, m_swapchain.GetDepthFormat());
        // Water: set 1 is the view's two scene copies (PostTargets::waterSet, the two-texture layout).
        m_waterPass.Create(m_context, *m_layouts.GetDescriptorSetLayout(), *m_layouts.GetTonemapDescriptorSetLayout(),
                           RenderLayouts::kSceneColorFormat, m_swapchain.GetDepthFormat());
        m_frameManager.Init(m_context, m_swapchain);
        m_uploads.Init(m_context, m_frameManager.GetCommandPool());
        // Volumetric fog (compute): before any set 0 is written, since binding 10 starts out pointing
        // at its "no fog" volume.
        m_fogPass.Create(*m_layouts.GetDescriptorSetLayout(), m_uploads, ViewportSet::kMaxViewports + 1);

        CreateUniformBuffers();
        // Before CreateDescriptorSets(): set 0's shared bindings point at these.
        static_assert(SceneLights::kMaxClusterViews >= ViewportSet::kMaxViewports + 1, "one cluster grid per viewport, plus the window");
        m_lights.Init(m_context, m_frameManager.MaxFramesInFlight());   // light buffer + cluster buffers
        m_scene.Init(m_frameManager.MaxFramesInFlight());   // instance buffers
        CreateShadowResources();
        m_viewTargets.Init(ViewportSet::kMaxViewports + 1, *m_layouts.GetSingleTextureSetLayout(), *m_layouts.GetTonemapDescriptorSetLayout(),
                           m_swapchain.GetDepthFormat());   // samplers and the pool every view's targets use
        m_particlePass.InitResources(m_frameManager.MaxFramesInFlight(), *m_layouts.GetSingleTextureSetLayout(), *m_viewTargets.LinearSampler());

        CreateDescriptorPool();
        CreateDescriptorSets();

        m_viewportSet.Init(m_frameManager.MaxFramesInFlight(), *m_layouts.GetDescriptorSetLayout(), *m_outlinePass.GetMaskSetLayout(),
                           m_swapchain.GetSurfaceFormat().format, m_swapchain.GetDepthFormat());

        m_resources.InitMaterials(*m_layouts.GetMaterialDescriptorSetLayout());   // sampler, placeholders, default material
        m_particlePass.EnsureTextureSet(m_resources.DefaultWhiteTexture());   // the particle pipeline's fallback texture set
        // Screen-space UI: drawn into viewport images and the swapchain, which share one format.
        m_uiPass.Create(*m_layouts.GetSingleTextureSetLayout(), m_swapchain.GetSurfaceFormat().format,
                        m_swapchain.GetDepthFormat(), m_frameManager.MaxFramesInFlight());
        // World-space text: drawn inside the scene pass, into the HDR image.
        m_worldUIPass.Create(*m_layouts.GetDescriptorSetLayout(), *m_layouts.GetSingleTextureSetLayout(),
                               RenderLayouts::kSceneColorFormat, m_swapchain.GetDepthFormat(), m_frameManager.MaxFramesInFlight());

        m_environmentMaps.Init(*m_layouts.GetSkyboxDescriptorSetLayout());   // samplers, pool, the built-in sky
        m_defaultEnvironment.skybox = m_environmentMaps.DefaultSkybox();
        m_environment = m_defaultEnvironment;

        CreatePrimitiveMeshes();
        m_profiler.Init(m_context, m_frameManager.MaxFramesInFlight());

    }

    void Renderer::ShutDown() { m_context.ShutDown(); }

    void Renderer::Update(float dt) {

        // Shader clock (FrameUniforms::time). A float holds seconds accurately enough for
        // animation for many hours of play; wrap it if sessions ever get longer than that.
        m_deltaTime = std::max(dt, 0.0f);
        m_time     += m_deltaTime;

        UpdateStressTest();

        // Finish a few queued async mesh uploads (see CreateMeshAsync) before drawing this frame.
        m_resources.ProcessPendingMeshUploads();

        // POPULATE
        //   Persistent proxies were updated by RenderExtractSystem (SetRenderProxy) during the ECS
        //   update; immediate Submit() items were queued by whoever called Submit().

        m_scene.RetryPendingBounds();   // proxies whose mesh was still uploading last frame

        if (DebugDraw::ShouldDraw(DebugCategory::Bounds)) { m_scene.DrawBoundsDebug(); }

        // The built-in UI check (SetUITestOverlay) joins whatever UI the game submitted this frame.
        if (m_uiTestEnabled) { m_uiTestOverlay.Submit(*this, m_time); }

        // CULL, SORT & DRAW
        //   Per view (PrepareView -> RenderScene::BuildViewBatches, on the worker threads): each camera
        //   gets its own visible list, so the Scene and Game views never cull with each other's camera.
        DrawFrame();

        // Everything submitted for this frame is consumed; lights and environment are per-frame
        // state exactly like the render queue (see SubmitLight/SetEnvironment).
        m_scene.ClearSubmitted();
        m_particlePass.ClearSubmitted();
        m_waterPass.ClearSubmitted();
        m_worldUIPass.ClearSubmitted();
        m_lights.ClearSubmitted();
        m_uiPass.ClearSubmitted();
        m_environment = m_defaultEnvironment;

        // Age/clear debug lines, and tell DebugDraw whether anything will show them next frame -
        // when nothing does, every DebugDraw call becomes a cheap early-out.
        bool debugVisible = m_viewportSet.All().empty() && m_debugInWindow;
        for (const auto& [id, vp] : m_viewportSet.All()) { debugVisible = debugVisible || vp.debugDraw; }
        DebugDraw::EndFrame(dt, debugVisible);

        // Lets objects that have stopped moving settle back into the spatial index's tree.
        m_scene.EndFrame();

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
        m_profiler.Read(frameIndex, m_frameStats);
        const double tPrepare = now();
        smooth(m_frameStats.cpuWaitMs, tPrepare - tStart);

        // BeginFrame() has waited on this frame index's fence, so the GPU is done with the staging
        // buffers of the uploads recorded the last time this index was used: free them.
        m_uploads.ReleaseFrame(frameIndex);
        m_resources.ReleaseDestroyed();   // frees meshes/textures/materials destroyed long enough ago
        m_environmentMaps.ReleaseDestroyed();   // ... and skyboxes

        // ...and with this index's dynamic-mesh buffers: refill the ones whose geometry changed.
        m_resources.UploadDynamicMeshes(frameIndex);

        ApplyMsaaSetting();

        // Lights first: every view's uniforms read the light count and shadow light this picks.
        m_lights.BuildFrameLights(frameIndex);
        m_debugLinePass.Update(m_context, frameIndex, m_frameManager.MaxFramesInFlight());
        m_uiPass.Upload(frameIndex);

        // PREPARE: cull and batch every view (main pass, shadow cascades, selection mask) before
        // recording anything, so the instance buffer's final size is known up front and it never
        // has to grow while commands already reference it.
        m_scene.BeginViews();
        m_particlePass.ClearPrepared();
        m_lights.BeginViews();
        m_windowDraws.Clear();
        const bool fogVolume = VolumetricFogActive();   // every view that draws the scene gets its fog volume this frame
        if (m_viewportSet.All().empty()) {
            // No camera entity in the scene: nothing to look through, so the window shows only
            // its clear colour (plus any overlay).
            if (m_hasMainCamera) {
                const vk::Extent2D extent = m_swapchain.GetExtent();
                // The window's HDR image and bloom chain follow the swapchain's size.
                if (m_windowPost.extent != extent && extent.width > 0 && extent.height > 0) {
                    m_context.GetDevice().waitIdle();
                    m_viewTargets.Create(m_windowPost, extent.width, extent.height, m_msaaSamples);
                }
                if (fogVolume) { m_fogPass.EnsureTargets(m_windowFog); }
                PrepareView(m_camera, extent, m_windowCullStats, m_windowDraws, false, m_uniformBuffersMapped[frameIndex]);
                WriteViewTextures(*m_descriptorSets[frameIndex], m_windowPost, fogVolume ? &m_windowFog : nullptr);
            } else {
                m_windowCullStats = RenderCullStats{};
            }
        } else {
            for (auto& [id, vp] : m_viewportSet.All()) {
                if (!vp.active) { vp.draws.Clear(); continue; }
                // No scene this frame (a UI viewport, or a Game viewport with no camera to look through):
                // nothing to cull; the view shows its clear colour and UI.
                if (!ViewportDrawsScene(vp)) { vp.draws.Clear(); vp.cullStats = RenderCullStats{}; continue; }
                // A Game viewport always looks through the scene's main camera (its aspect is then
                // set from the viewport's own size, in PrepareView).
                if (vp.role == ViewportRole::Game) { vp.camera = m_camera; }
                if (fogVolume) { m_fogPass.EnsureTargets(vp.fog); }
                PrepareView(vp.camera, vp.extent, vp.cullStats, vp.draws, vp.outline, vp.uniformBuffersMapped[frameIndex]);
                WriteViewTextures(*vp.descriptorSets[frameIndex], vp.post, fogVolume ? &vp.fog : nullptr);
            }
        }
        const double tUpload = now();
        smooth(m_frameStats.cpuPrepareMs, tUpload - tPrepare);

        UploadInstances(frameIndex);
        UploadParticles(frameIndex);
        m_worldUIPass.Upload(frameIndex);
        m_lights.UploadClusters(frameIndex);
        const double tRecord = now();
        smooth(m_frameStats.cpuUploadMs, tRecord - tUpload);

        // Record and submit command buffer.
        RecordCommandBuffer(fc);
        const double tSubmit = now();
        smooth(m_frameStats.cpuRecordMs, tSubmit - tRecord);

        m_frameManager.EndFrame(m_context, m_swapchain, fc, m_window);
        smooth(m_frameStats.cpuSubmitMs, now() - tSubmit);

    }   // Renderer::DrawFrame()

    bool Renderer::GetObjectBounds(std::uint64_t userData, glm::vec3& outMin, glm::vec3& outMax) const {
        return m_scene.GetObjectBounds(userData, outMin, outMax);
    }

    void Renderer::SetRenderProxyAnimation(RenderProxyHandle handle, std::span<const glm::mat4> palette,
                                           const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
        m_scene.SetProxyAnimation(handle, palette, boundsMin, boundsMax);
    }
    void Renderer::Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model, const glm::vec4& params) { m_scene.Submit(mesh, material, model, params); }

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
        m_profiler.BeginFrame(*fc.frame.commandBuffer, frameIndex);
        m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::Uploads);

        // Mesh/texture/cubemap copies queued since the last frame go first, ahead of every pass,
        // so anything created before this point is filled by the time a draw below reads it.
        m_uploads.Record(*fc.frame.commandBuffer, frameIndex);

        // Same answer as when the views were prepared this frame (DrawFrame).
        const bool fogVolume = VolumetricFogActive();

        // ---- Offscreen editor viewports -------------------------------------------------------
        // Empty in a standalone Engine/Game build: this loop simply doesn't run, so the cost of
        // supporting viewports at all is the one `m_viewportSet.All().empty()` check further down.
        for (auto& [id, vp] : m_viewportSet.All()) {

            if (!vp.active) { continue; }   // paused (SetViewportActive): keeps its last image

            const vk::DescriptorSet frameSet = *vp.descriptorSets[frameIndex];
            const bool drawScene = ViewportDrawsScene(vp);   // false: clear colour + UI only

            if (drawScene) {
                // Passes that feed this view's main pass: its shadow cascades into the shared shadow
                // map, and the selected object into its outline mask.
                m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::Shadows);
                m_shadowPass.Record(*fc.frame.commandBuffer, frameSet, vp.draws.shadow, vp.draws.cascadeCount);
                // The fog volume, lit through the shadow map just drawn (compute, outside any rendering pass).
                if (fogVolume) {
                    m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::Volumetrics);
                    m_fogPass.Record(*fc.frame.commandBuffer, vp.fog, frameSet);
                }
                if (vp.outline && !vp.draws.outline.empty()) {
                    m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::Outline);
                    m_outlinePass.RecordMask(*fc.frame.commandBuffer, vp.maskImage.GetImage(), *vp.maskView, vp.extent, frameSet, vp.draws.outline);
                }

                // The scene itself: meshes, sky and particles into this view's HDR image, then bloom.
                m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::AmbientOcclusion);
                m_aoPass.Record(*fc.frame.commandBuffer, vp.post, m_postSettings, frameSet, vp.draws.main, vp.draws.projInfo);
                m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::Scene);
                DrawSceneHdr(fc, vp.post, vp.depthImage.GetImage(), *vp.depthView, vp.extent, frameSet, vp.draws);   // marks Bloom itself
            }
            m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::Final);

            // The final pass: tonemap the HDR image into the viewport's (sRGB) colour image, then
            // the debug lines on top - still depth-tested against the scene's depth - then UI.
            TransitionImageLayout(
                fc, vp.colorImage.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                {}, vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eColorAttachmentOutput
            );
            // With no scene, nothing has touched depth this frame; it is still attached (every
            // final-pass pipeline names the depth format), so start it fresh and clear it.
            if (!drawScene) {
                TransitionImageLayout(
                    fc, vp.depthImage.GetImage(), vk::ImageAspectFlagBits::eDepth,
                    vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests
                );
            }

            const glm::vec4 clearLinear(SrgbToLinear(vp.clearColour.r), SrgbToLinear(vp.clearColour.g), SrgbToLinear(vp.clearColour.b), vp.clearColour.a);
            vk::RenderingAttachmentInfo color{
                .imageView = vp.colorView,
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = drawScene ? vk::AttachmentLoadOp::eDontCare : vk::AttachmentLoadOp::eClear,   // with a scene, the tonemap triangle covers every pixel
                .storeOp = vk::AttachmentStoreOp::eStore,
                .clearValue = vk::ClearColorValue(clearLinear.r, clearLinear.g, clearLinear.b, clearLinear.a)   // the image is sRGB: clear values are linear
            };
            vk::RenderingAttachmentInfo depth{
                .imageView = vp.depthView,
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp = drawScene ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
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
            if (drawScene) {
                m_tonemapPass.Record(*fc.frame.commandBuffer, vp.post, vp.extent, m_postSettings, m_environment, m_time);
                if (vp.debugDraw) { m_debugLinePass.Record(*fc.frame.commandBuffer, vp.extent, frameSet, m_frameManager.GetFrameIndex()); }
            }
            // UI last, over everything: the game's UI in Game views, then this viewport's own.
            if (vp.role == ViewportRole::Game) { m_uiPass.Record(*fc.frame.commandBuffer, frameIndex, vp.extent, vp.uiTransform, UIPass::m_screenTarget); }
            m_uiPass.Record(*fc.frame.commandBuffer, frameIndex, vp.extent, vp.uiTransform, id);
            fc.frame.commandBuffer.endRendering();

            if (drawScene && vp.outline && !vp.draws.outline.empty()) { m_outlinePass.RecordComposite(*fc.frame.commandBuffer, vp.colorImage.GetImage(), *vp.colorView, vp.extent, *vp.outlineSet, m_outlineStyle); }

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
        const bool windowScene = m_viewportSet.All().empty() && m_hasMainCamera && *m_windowPost.hdrView;
        if (windowScene) {
            m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::Shadows);
            m_shadowPass.Record(*fc.frame.commandBuffer, *m_descriptorSets[frameIndex], m_windowDraws.shadow, m_windowDraws.cascadeCount);
            if (fogVolume) {
                m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::Volumetrics);
                m_fogPass.Record(*fc.frame.commandBuffer, m_windowFog, *m_descriptorSets[frameIndex]);
            }
            m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::AmbientOcclusion);
            m_aoPass.Record(*fc.frame.commandBuffer, m_windowPost, m_postSettings, *m_descriptorSets[frameIndex], m_windowDraws.main, m_windowDraws.projInfo);
            m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::Scene);
            DrawSceneHdr(fc, m_windowPost, m_swapchain.GetDepthImage(), m_swapchain.GetDepthImageView(),
                         m_swapchain.GetExtent(), *m_descriptorSets[frameIndex], m_windowDraws);
        }

        m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::Interface);

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

        // Black where nothing is drawn: a standalone menu scene with no camera shows its UI over this.
        vk::ClearValue              clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
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
            m_tonemapPass.Record(*fc.frame.commandBuffer, m_windowPost, m_swapchain.GetExtent(), m_postSettings, m_environment, m_time);
            if (m_debugInWindow) { m_debugLinePass.Record(*fc.frame.commandBuffer, m_swapchain.GetExtent(), *m_descriptorSets[frameIndex], m_frameManager.GetFrameIndex()); }
        }
        // The standalone game's UI, camera or not. (In the editor the window holds only ImGui;
        // the game's UI is in the Game view.)
        if (m_viewportSet.All().empty()) {
            m_uiPass.Record(*fc.frame.commandBuffer, frameIndex, m_swapchain.GetExtent(), glm::vec4(1.0f, 1.0f, 0.0f, 0.0f), UIPass::m_screenTarget);
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
        m_profiler.Mark(*fc.frame.commandBuffer, GpuSection::End);
        fc.frame.commandBuffer.end();

    }   // Renderer::RecordCommandBuffer()

    MeshHandle Renderer::CreateMesh(const MeshData& data, const std::string& debugName) { return m_resources.CreateMesh(data, debugName); }
    MeshHandle Renderer::CreateMeshAsync(MeshData data, const std::string& debugName)   { return m_resources.CreateMeshAsync(std::move(data), debugName); }
    MeshHandle Renderer::CreateDynamicMesh(const std::string& debugName)                 { return m_resources.CreateDynamicMesh(debugName); }
    void Renderer::UpdateDynamicMesh(MeshHandle handle, std::span<const Vertex> vertices, std::span<const uint32_t> indices) {
        m_resources.UpdateDynamicMesh(handle, vertices, indices);
    }
    bool Renderer::GetMeshBounds(MeshHandle handle, glm::vec3& outMin, glm::vec3& outMax) const { return m_resources.GetMeshBounds(handle, outMin, outMax); }
    const std::string& Renderer::GetMeshName(MeshHandle handle) const { return m_resources.GetMeshName(handle); }


    // ------------------------------------------------------------------------------------------------------------- //
    //  VIEWPORTS (offscreen render targets for the Editor)
    // ------------------------------------------------------------------------------------------------------------- //

    ViewportHandle Renderer::CreateViewport(uint32_t width, uint32_t height, ViewportRole role) {

        const ViewportHandle handle = m_viewportSet.Create(width, height, m_msaaSamples, role);
        // Bindings 1-7 of its new sets: the same per-frame buffers and shadow map the window's sets use.
        WriteSharedFrameDescriptors(m_viewportSet.Get(handle).descriptorSets);
        return handle;

    }   // Renderer::CreateViewport()

    void Renderer::ResizeViewport(ViewportHandle handle, uint32_t width, uint32_t height) { m_viewportSet.Resize(handle, width, height, m_msaaSamples); }

    void Renderer::DestroyViewport(ViewportHandle handle) { m_viewportSet.Destroy(handle); }

    void Renderer::SetViewportDebugDraw(ViewportHandle handle, bool enabled) {

        m_viewportSet.Get(handle).debugDraw = enabled;

    }   // Renderer::SetViewportDebugDraw()

    void Renderer::SetViewportOutline(ViewportHandle handle, bool enabled) {

        m_viewportSet.Get(handle).outline = enabled;

    }   // Renderer::SetViewportOutline()

    void Renderer::SetViewportActive(ViewportHandle handle, bool active) {

        m_viewportSet.Get(handle).active = active;

    }   // Renderer::SetViewportActive()

    ViewportRole Renderer::GetViewportRole(ViewportHandle handle) const {

        return m_viewportSet.Get(handle).role;

    }   // Renderer::GetViewportRole()

    void Renderer::SetViewportClearColour(ViewportHandle handle, const glm::vec4& srgb) {

        m_viewportSet.Get(handle).clearColour = glm::clamp(srgb, 0.0f, 1.0f);

    }   // Renderer::SetViewportClearColour()

    void Renderer::SetViewportUITransform(ViewportHandle handle, const glm::vec2& scale, const glm::vec2& offset) {

        m_viewportSet.Get(handle).uiTransform = glm::vec4(scale, offset);

    }   // Renderer::SetViewportUITransform()

    bool Renderer::ViewportDrawsScene(const Viewport& vp) const {

        switch (vp.role) {
            case ViewportRole::Scene: return true;
            case ViewportRole::Game:  return m_hasMainCamera;
            case ViewportRole::UI:    return false;
        }
        return true;

    }   // Renderer::ViewportDrawsScene()

    // ------------------------------------------------------------------------------------------------------------- //
    //  SCREEN-SPACE UI (UIPass)
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::SubmitUI(const UIDrawList& list) { m_uiPass.Submit(UIPass::m_screenTarget, list); }

    void Renderer::SubmitViewportUI(ViewportHandle viewport, const UIDrawList& list) {

        if (!viewport.IsValid() || !m_viewportSet.All().contains(viewport.id)) { return; }
        m_uiPass.Submit(viewport.id, list);

    }   // Renderer::SubmitViewportUI()

    glm::uvec2 Renderer::GetGameScreenSize() const {

        // Standalone game: the window. Editor: the Game view's image.
        if (m_viewportSet.All().empty()) {
            const vk::Extent2D extent = m_swapchain.GetExtent();
            return { extent.width, extent.height };
        }
        for (const auto& [id, vp] : m_viewportSet.All()) {
            if (vp.role == ViewportRole::Game) { return { vp.extent.width, vp.extent.height }; }
        }
        return { 0u, 0u };

    }   // Renderer::GetGameScreenSize()

    bool Renderer::ProjectToGameScreen(const glm::vec3& worldPos, glm::vec2& outPixels) const {

        if (!m_hasMainCamera) { return false; }
        const glm::uvec2 size = GetGameScreenSize();
        if (size.x == 0 || size.y == 0) { return false; }

        // The main camera at the game screen's aspect: exactly what the Game view or window draws.
        Camera camera = m_camera;
        camera.SetAspect(static_cast<float>(size.x), static_cast<float>(size.y));
        const glm::vec4 clip = camera.GetViewProjection() * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 1.0e-6f) { return false; }   // behind the camera (or on its plane)

        // Clip space has +y up (the scene is drawn with a flipped viewport); screen pixels go down.
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        outPixels = glm::vec2((ndc.x * 0.5f + 0.5f) * static_cast<float>(size.x),
                              (0.5f - ndc.y * 0.5f) * static_cast<float>(size.y));
        return true;

    }   // Renderer::ProjectToGameScreen()

    void Renderer::SetGameScreenPlacement(const glm::vec2& windowOrigin, float scale, bool pointerOver) {

        m_gameScreenOrigin      = windowOrigin;
        m_gameScreenScale       = scale;
        m_gameScreenPointerOver = pointerOver;

    }   // Renderer::SetGameScreenPlacement()

    bool Renderer::WindowToGameScreen(const glm::vec2& windowPos, glm::vec2& outPixels) const {

        const glm::uvec2 size = GetGameScreenSize();
        if (size.x == 0 || size.y == 0) { return false; }

        glm::vec2 pixels;
        if (m_viewportSet.All().empty()) {
            // Standalone: the window is the game screen. GLFW reports the cursor in window units,
            // the swapchain is in framebuffer pixels; on a high-DPI display they differ.
            int width = 0, height = 0;
            glfwGetWindowSize(m_window.GetNativeHandle(), &width, &height);
            if (width <= 0 || height <= 0) { return false; }
            pixels = windowPos * glm::vec2(size) / glm::vec2(static_cast<float>(width), static_cast<float>(height));
        }
        else {
            // Editor: the Game view's image, wherever it sits in the panel and however it is scaled.
            if (!m_gameScreenPointerOver || m_gameScreenScale <= 0.0f) { return false; }
            pixels = (windowPos - m_gameScreenOrigin) / m_gameScreenScale;
        }

        if (pixels.x < 0.0f || pixels.y < 0.0f || pixels.x >= static_cast<float>(size.x) || pixels.y >= static_cast<float>(size.y)) {
            return false;   // in the letterbox bars, or off the window
        }
        outPixels = pixels;
        return true;

    }   // Renderer::WindowToGameScreen()

    void Renderer::SetOutlinedObject(std::uint64_t userData) {

        m_scene.SetOutlinedObject(userData);

    }   // Renderer::SetOutlinedObject()

    void Renderer::SetOutlinedObjects(const std::vector<std::uint64_t>& userData) {

        m_scene.SetOutlinedObjects(userData);

    }   // Renderer::SetOutlinedObjects()

    void Renderer::SetOutlineStyle(const glm::vec4& color, float thicknessPixels) {

        m_outlineStyle.color     = color;
        m_outlineStyle.thickness = glm::clamp(thicknessPixels, 1.0f, 3.0f);

    }   // Renderer::SetOutlineStyle()

    Camera& Renderer::GetViewportCamera(ViewportHandle handle) {

        return m_viewportSet.Get(handle).camera;

    }   // Renderer::GetViewportCamera()

    vk::ImageView Renderer::GetViewportColorView(ViewportHandle handle) const {

        return *m_viewportSet.Get(handle).colorView;

    }   // Renderer::GetViewportColorView()

    vk::ImageView Renderer::GetTextureView(TextureHandle handle) const {

        return *m_resources.GetTextureOrThrow(handle).view;

    }   // Renderer::GetTextureView()

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
        // ... plus three textures: the environment map (8), the view's ambient occlusion (9) and
        // its fog volume (10).
        std::array<vk::DescriptorPoolSize, 5> poolSizes{ {
            { .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = maxFrames * 3 },
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
        std::vector<vk::DescriptorSetLayout> layouts(maxFrames, *m_layouts.GetDescriptorSetLayout());
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
    //  TEXTURES & MATERIALS (stored in RenderResources)
    // ------------------------------------------------------------------------------------------------------------- //

    TextureHandle Renderer::CreateTextureCompressed(const CompiledTexture& texture, bool isSRGB, const std::string& cacheKey) {
        return m_resources.CreateTextureCompressed(texture, isSRGB, cacheKey);
    }
    TextureHandle Renderer::CreateTextureFromPixels(const void* pixels, uint32_t width, uint32_t height, TexturePixelFormat format, bool mipmaps) {
        return m_resources.CreateTextureFromPixels(pixels, width, height, format, mipmaps);
    }
    MaterialHandle Renderer::CreateMaterial(const MaterialData& data) { return m_resources.CreateMaterial(data); }
    void Renderer::UpdateMaterial(MaterialHandle handle, const MaterialData& data) { m_resources.UpdateMaterial(handle, data); }
    const MaterialData& Renderer::GetMaterialData(MaterialHandle handle) const { return m_resources.GetMaterialData(handle); }
    bool Renderer::DestroyMesh(MeshHandle handle)         { return m_resources.DestroyMesh(handle); }
    bool Renderer::DestroyTexture(TextureHandle handle)   { return m_resources.DestroyTexture(handle); }
    bool Renderer::DestroyMaterial(MaterialHandle handle) { return m_resources.DestroyMaterial(handle); }
    bool Renderer::DestroyCubemap(CubemapHandle handle)   { return m_environmentMaps.DestroyCubemap(handle); }

    // ------------------------------------------------------------------------------------------------------------- //
    //  LIGHTS & ENVIRONMENT
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::SubmitLight(const LightData& light) { m_lights.Submit(light); }

    void Renderer::SetEnvironment(const EnvironmentSettings& settings) {

        m_environment = settings;

    }   // Renderer::SetEnvironment()

    void Renderer::WriteSharedFrameDescriptors(std::vector<vk::raii::DescriptorSet>& sets) {

        for (size_t i = 0; i < sets.size() && i < m_lights.BufferCount() && i < m_scene.InstanceBufferCount()
                           && i < m_particlePass.BufferCount(); ++i) {
            vk::DescriptorBufferInfo particleInfo{ .buffer = m_particlePass.Buffer(static_cast<uint32_t>(i)), .offset = 0, .range = vk::WholeSize };
            vk::DescriptorBufferInfo clusterGridInfo{ .buffer = m_lights.ClusterGridBuffer(i), .offset = 0, .range = vk::WholeSize };
            vk::DescriptorBufferInfo clusterIndexInfo{ .buffer = m_lights.ClusterIndexBuffer(i), .offset = 0, .range = vk::WholeSize };
            vk::DescriptorBufferInfo lightInfo{
                .buffer = m_lights.LightBuffer(i),
                .offset = 0,
                .range  = sizeof(GpuLight) * kMaxLights };
            vk::DescriptorBufferInfo instanceInfo{
                .buffer = m_scene.InstanceBuffer(i),
                .offset = 0,
                .range  = vk::WholeSize };
            vk::DescriptorImageInfo shadowImageInfo{
                .imageView   = m_shadowPass.ArrayView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            vk::DescriptorImageInfo shadowSamplerInfo{ .sampler = m_shadowPass.Sampler() };
            // Binding 10 starts at the "no fog" volume; WriteViewTextures points it at the view's own.
            vk::DescriptorImageInfo fogInfo = m_fogPass.SampledVolume(nullptr);

            const std::array<vk::WriteDescriptorSet, 8> writes{ {
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
                  .descriptorType = vk::DescriptorType::eSampler, .pImageInfo = &shadowSamplerInfo },
                { .dstSet = *sets[i], .dstBinding = 10, .descriptorCount = 1,
                  .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &fogInfo }
            } };
            m_context.GetDevice().updateDescriptorSets(writes, {});
        }

    }   // Renderer::WriteSharedFrameDescriptors()

    void Renderer::FillEnvironmentUniforms(FrameUniforms& ubo) const {

        const EnvironmentSettings& env = m_environment;
        ubo.ambientSky    = glm::vec4(env.ambientSky * env.ambientIntensity, 0.0f);
        ubo.ambientGround = glm::vec4(env.ambientGround * env.ambientIntensity, 0.0f);
        ubo.renderParams  = glm::vec4(std::max(env.exposure, 0.0f), std::max(env.skyboxIntensity, 0.0f), 0.0f, 0.0f);
        ubo.lightParams   = glm::uvec4(m_lights.LightCount(), 0u, 0u, m_lights.DirLightCount());

        ubo.fogColour = glm::vec4(glm::max(env.fogColour, glm::vec3(0.0f)), std::max(env.fogDensity, 0.0f));
        ubo.fogParams = glm::vec4(std::max(env.fogHeightFalloff, 0.0f), env.fogBaseHeight, std::max(env.fogStart, 0.0f),
                                  glm::clamp(env.fogMaxOpacity, 0.0f, 1.0f));

        // Light from the sky: as bright as the sky is drawn, times the ambient intensity.
        const GpuCubemap& sky = m_environmentMaps.Resolve(m_environment.skybox);
        ubo.ambientParams = glm::vec4(env.ambientFromSky ? 1.0f : 0.0f, static_cast<float>(sky.envMipCount - 1),
                                      std::max(env.ambientIntensity, 0.0f) * std::max(env.skyboxIntensity, 0.0f), 0.0f);
        for (size_t i = 0; i < sky.sh.size(); ++i) { ubo.skySH[i] = sky.sh[i]; }

        // Volumetric fog: every view that draws the scene has a fog volume while this is on.
        if (VolumetricFogActive()) {
            const float nearDepth = VolumetricFogPass::kNearDepth;
            const float farDepth  = glm::clamp(env.volumetricDistance, nearDepth * 4.0f, 1000.0f);
            ubo.volumetricParams  = glm::vec4(1.0f, nearDepth, farDepth, std::log(farDepth / nearDepth));
            ubo.volumetricLight   = glm::vec4(std::max(env.volumetricIntensity, 0.0f), glm::clamp(env.volumetricAnisotropy, -0.95f, 0.95f),
                                              static_cast<float>(VolumetricFogPass::kSlices), 0.0f);
            ubo.volumetricAmbient = glm::vec4(std::max(env.volumetricAmbient, 0.0f), env.volumetricSkyAmbient ? 1.0f : 0.0f, 0.0f, 0.0f);
        } else {
            ubo.volumetricParams  = glm::vec4(0.0f);
            ubo.volumetricLight   = glm::vec4(0.0f);
            ubo.volumetricAmbient = glm::vec4(0.0f);
        }

        // Toon shading. Always filled: materials set to "Toon" use these settings even when the
        // scene itself is lit realistically. x says what "Scene" materials do, or that the
        // Rendering menu has toon switched off everywhere. The tints are picked in sRGB; the
        // shader works in linear light. The edge tint is scaled to luminance 1 so the band
        // changes hue, not brightness.
        {
            const auto toLinear = [](const glm::vec3& srgb) { return glm::pow(glm::clamp(srgb, glm::vec3(0.0f), glm::vec3(1.0f)), glm::vec3(2.2f)); };
            glm::vec3 edge = toLinear(env.toonEdgeColour);
            const float edgeLuminance = glm::dot(edge, glm::vec3(0.2126f, 0.7152f, 0.0722f));
            edge = edgeLuminance > 1e-4f ? edge / edgeLuminance : glm::vec3(1.0f);
            const float mode = !m_postSettings.toonShading ? 2.0f : env.toonShading ? 1.0f : 0.0f;
            ubo.toonParams = glm::vec4(mode, glm::clamp(env.toonThreshold, 0.0f, 0.95f), glm::clamp(env.toonSoftness, 0.0f, 0.5f),
                                       std::max(env.toonShadeBrightness, 0.0f));
            ubo.toonShade  = glm::vec4(toLinear(env.toonShadeColour), glm::clamp(env.toonAmbientFlatten, 0.0f, 1.0f));
            ubo.toonEdge   = glm::vec4(edge, glm::clamp(env.toonEdgeWidth, 0.0f, 1.0f));
            ubo.toonExtra  = glm::vec4(glm::clamp(env.toonEdgeStrength, 0.0f, 1.0f), std::max(env.toonHighlight, 0.0f),
                                       glm::clamp(env.toonReflections, 0.0f, 1.0f), glm::clamp(env.toonLitShading, 0.0f, 1.0f));
            // Toon outlines: the scene shader writes line strengths only when the outline pass
            // will run (ToonLinesActive), so with lines off nothing downstream changes.
            ubo.toonLines  = glm::vec4(ToonLinesActive() ? 1.0f : 0.0f, glm::clamp(env.toonLineBreakup, 0.0f, 1.0f),
                                       1.0f / std::max(env.toonLineBreakupSize, 0.01f), 0.0f);
        }

    }   // Renderer::FillEnvironmentUniforms()

    bool Renderer::ToonLinesActive() const {

        // Lines belong to toon-shaded materials, so the Rendering menu's "toon off" turns them
        // off too.
        return m_environment.toonLines && m_postSettings.toonShading;

    }   // Renderer::ToonLinesActive()

    bool Renderer::VolumetricFogActive() const {

        return m_postSettings.volumetricFog && m_environment.volumetricFog && m_environment.fogDensity > 0.0f;

    }   // Renderer::VolumetricFogActive()

    void Renderer::WriteViewTextures(vk::DescriptorSet frameSet, const PostTargets& targets, const FogVolumeTargets* fog) {

        if (m_environmentMaps.Cubemaps().empty() || !*targets.aoBlurView) { return; }

        vk::DescriptorImageInfo envInfo{
            .sampler = *m_environmentMaps.EnvSampler(), .imageView = *m_environmentMaps.Resolve(m_environment.skybox).envView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::DescriptorImageInfo aoInfo{
            .sampler = *m_viewTargets.LinearSampler(), .imageView = *targets.aoBlurView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::DescriptorImageInfo fogInfo = m_fogPass.SampledVolume(fog);
        const std::array<vk::WriteDescriptorSet, 3> writes{ {
            { .dstSet = frameSet, .dstBinding = 8, .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &envInfo },
            { .dstSet = frameSet, .dstBinding = 9, .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &aoInfo },
            { .dstSet = frameSet, .dstBinding = 10, .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &fogInfo }
        } };
        m_context.GetDevice().updateDescriptorSets(writes, {});

    }   // Renderer::WriteViewTextures()

    // ------------------------------------------------------------------------------------------------------------- //
    //  SKYBOX / CUBEMAPS (stored in EnvironmentMaps)
    // ------------------------------------------------------------------------------------------------------------- //

    CubemapHandle Renderer::CreateCubemapFromEquirect(const float* rgba, uint32_t width, uint32_t height, uint32_t faceSize, const std::string& cacheKey) {
        return m_environmentMaps.CreateCubemapFromEquirect(rgba, width, height, faceSize, cacheKey);
    }
    CubemapHandle Renderer::CreateCubemapCompiled(const CompiledSky& sky, const std::string& debugName) { return m_environmentMaps.CreateCubemapCompiled(sky, debugName); }

    glm::vec3 Renderer::GetSkyIrradiance(CubemapHandle sky, const glm::vec3& normal) const {

        const float length = glm::length(normal);
        const glm::vec3 n = length > 1e-6f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
        return EvaluateSkyIrradiance(m_environmentMaps.Resolve(sky).sh, n);

    }   // Renderer::GetSkyIrradiance()

    // ------------------------------------------------------------------------------------------------------------- //
    //  RENDER PROXIES, CULLING, PICKING
    // ------------------------------------------------------------------------------------------------------------- //

    RenderProxyHandle Renderer::CreateRenderProxy(std::uint64_t userData) { return m_scene.CreateProxy(userData); }
    void Renderer::SetRenderProxy(RenderProxyHandle handle, MeshHandle mesh, MaterialHandle material, const glm::mat4& model, bool visible, const glm::vec4& params) {
        m_scene.SetProxy(handle, mesh, material, model, visible, params);
    }
    void Renderer::DestroyRenderProxy(RenderProxyHandle handle) { m_scene.DestroyProxy(handle); }

    RenderCullStats Renderer::GetViewportCullStats(ViewportHandle handle) const {

        auto it = m_viewportSet.All().find(handle.id);
        return it == m_viewportSet.All().end() ? RenderCullStats{} : it->second.cullStats;

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

        // ---- Shadow cascades: where each cascade's light-space box is (culled below, with the view) ----
        const ShadowSettings& settings = m_shadowSettings;
        uint32_t cascadeCount = 0;
        std::array<glm::mat4, kMaxShadowCascades> cascadeViewProj{};
        if (settings.enabled && m_lights.ShadowLightIndex() >= 0 && static_cast<uint32_t>(m_lights.ShadowLightIndex()) < m_lights.LightCount()) {
            cascadeCount = glm::clamp(settings.cascadeCount, 1u, kMaxShadowCascades);
            m_shadowPass.ComputeCascades(camera, m_lights.ShadowLightDir(), m_shadowSettings, cascadeCount, ubo, cascadeViewProj);
            ubo.lightParams.y  = static_cast<uint32_t>(m_lights.ShadowLightIndex()) + 1u;
            ubo.lightParams.z  = cascadeCount;
        }

        // ---- What the camera and each cascade see: one instanced draw per (material, mesh) run in
        //      the main pass, per mesh run in each cascade. Culled and batched on the worker threads. ----
        m_scene.BuildViewBatches(camera, std::span<const glm::mat4>(cascadeViewProj.data(), cascadeCount), stats,
                                 out.main, std::span<std::vector<DrawBatch>>(out.shadow.data(), cascadeCount));
        stats.drawCalls  = static_cast<uint32_t>(out.main.size());
        out.cascadeCount = cascadeCount;

        // ---- Point/spot lights binned into this view's cluster grid; particles ----
        stats.clusteredLights = m_lights.BuildClusters(camera, extent, ubo);
        m_particlePass.Prepare(camera, out.particles, stats);
        m_waterPass.Prepare(camera, out.water);
        m_worldUIPass.Prepare(camera, out.worldUI);
        m_lights.EndView();

        // ---- Selection outline mask: just the selected objects ----
        if (outline) { m_scene.BuildOutlineBatches(out.outline); }

        memcpy(uniformDst, &ubo, sizeof(ubo));

    }   // Renderer::PrepareView()

    // ------------------------------------------------------------------------------------------------------------- //
    //  CLUSTERED LIGHTS
    // ------------------------------------------------------------------------------------------------------------- //

    // ------------------------------------------------------------------------------------------------------------- //
    //  PARTICLES
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::SubmitParticles(const ParticleDrawDesc& desc, const GpuParticle* particles, uint32_t count) { m_particlePass.Submit(desc, particles, count); }

    void Renderer::SubmitWater(MeshHandle mesh, const glm::mat4& model, const WaterSettings& settings) { m_waterPass.Submit(mesh, model, settings); }

    void Renderer::SubmitWorldUI(const WorldUIDesc& desc, std::span<const WorldUIVertex> vertices, std::span<const WorldUIRun> runs) {
        m_worldUIPass.Submit(desc, vertices, runs);
    }

    void Renderer::UploadParticles(uint32_t frameIndex) {

        // The pass copies this frame's particles into its buffer. If it had to replace the buffer
        // with a bigger one, every set 0 that pointed at the old one is pointed at the new one.
        if (m_particlePass.Upload(frameIndex)) {
            vk::DescriptorBufferInfo info{ .buffer = m_particlePass.Buffer(frameIndex), .offset = 0, .range = vk::WholeSize };
            auto repoint = [&](const vk::raii::DescriptorSet& set) {
                vk::WriteDescriptorSet write{
                    .dstSet = *set, .dstBinding = 5, .dstArrayElement = 0, .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &info };
                m_context.GetDevice().updateDescriptorSets(write, {});
            };
            repoint(m_descriptorSets[frameIndex]);
            for (auto& [id, vp] : m_viewportSet.All()) { repoint(vp.descriptorSets[frameIndex]); }
        }

    }   // Renderer::UploadParticles()

    // ------------------------------------------------------------------------------------------------------------- //
    //  POST-PROCESSING: HDR SCENE IMAGE -> BLOOM -> TONEMAP
    // ------------------------------------------------------------------------------------------------------------- //

    // ------------------------------------------------------------------------------------------------------------- //
    //  TIMINGS AND THE LOAD TEST
    // ------------------------------------------------------------------------------------------------------------- //

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
        for (auto& [id, vp] : m_viewportSet.All()) {
            if (*vp.post.hdrView) { m_viewTargets.Create(vp.post, vp.post.extent.width, vp.post.extent.height, m_msaaSamples); }
        }
        if (*m_windowPost.hdrView) { m_viewTargets.Create(m_windowPost, m_windowPost.extent.width, m_windowPost.extent.height, m_msaaSamples); }

    }   // Renderer::ApplyMsaaSetting()

    void Renderer::DrawSceneHdr(FrameContext& fc, PostTargets& t, vk::Image depthImage, vk::ImageView depthView,
                                vk::Extent2D extent, vk::DescriptorSet frameSet, const ViewDrawList& draws) {

        vk::CommandBuffer cmd = *fc.frame.commandBuffer;

        // The current sky's descriptor set, or none when the sky is switched off.
        vk::DescriptorSet skySet{};
        if (m_environment.drawSkybox) {
            CubemapHandle sky = m_environment.skybox;
            if (!m_environmentMaps.IsUsable(sky)) { sky = m_environmentMaps.DefaultSkybox(); }   // also a destroyed one
            if (sky.IsValid() && sky.id < m_environmentMaps.Cubemaps().size()) { skySet = *m_environmentMaps.Cubemaps()[sky.id].descriptorSet; }
        }
        m_scenePass.Record(cmd, t, depthImage, depthView, extent, frameSet, draws.main, skySet, draws.particles, draws.water, draws.worldUI);

        // Toon outlines go into the scene's image before bloom and tonemapping, so the tonemap
        // pass's anti-aliasing smooths them like any other edge.
        if (ToonLinesActive()) { m_toonOutlinePass.Record(cmd, t, m_environment); }

        m_profiler.Mark(cmd, GpuSection::Bloom);
        m_bloomPass.Record(cmd, t, m_postSettings, m_environment.exposure);

        // The final pass reads and keeps testing this depth: make the scene pass's writes visible.
        TransitionImageLayout(fc, depthImage, vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests);

    }   // Renderer::DrawSceneHdr()

    void Renderer::UploadInstances(uint32_t frameIndex) {

        // The scene copies this frame's instances into its buffer. If it had to replace the buffer
        // with a bigger one, every set 0 of this frame index is pointed at the new one.
        if (m_scene.UploadInstances(frameIndex)) {
            vk::DescriptorBufferInfo info{ .buffer = m_scene.InstanceBuffer(frameIndex), .offset = 0, .range = vk::WholeSize };
            auto repoint = [&](const vk::raii::DescriptorSet& set) {
                vk::WriteDescriptorSet write{
                    .dstSet = *set, .dstBinding = 2, .dstArrayElement = 0, .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &info };
                m_context.GetDevice().updateDescriptorSets(write, {});
            };
            repoint(m_descriptorSets[frameIndex]);
            for (auto& [id, vp] : m_viewportSet.All()) { repoint(vp.descriptorSets[frameIndex]); }
        }

    }   // Renderer::UploadInstances()

    // ------------------------------------------------------------------------------------------------------------- //
    //  CASCADED SHADOW MAPS
    // ------------------------------------------------------------------------------------------------------------- //

    void Renderer::CreateShadowResources() {

        m_shadowPass.CreateResources(m_context);   // the map, its views and its sampler

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
            .image               = m_shadowPass.Image(),
            .subresourceRange    = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .baseArrayLayer = 0, .layerCount = kMaxShadowCascades }
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier });
        EndSingleTimeCommand(std::move(cmd));

    }   // Renderer::CreateShadowResources()

    // ------------------------------------------------------------------------------------------------------------- //
    //  SELECTION OUTLINE
    // ------------------------------------------------------------------------------------------------------------- //

    bool Renderer::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RenderRaycastHit& outHit) const { return m_scene.Raycast(origin, direction, maxDistance, outHit); }

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

        // The barrier itself lives in Vulkan/VulkanCommands.h, where the render passes share it.
        Crucible::TransitionImageLayout(*fc.frame.commandBuffer, image, aspectMask, old_layout, new_layout,
            src_access_mask, dst_access_mask, src_stage_mask, dst_stage_mask, layerCount);

    }   // Renderer::TransitionImageLayout()

}   // NAMESPACE CRUCIBLE
