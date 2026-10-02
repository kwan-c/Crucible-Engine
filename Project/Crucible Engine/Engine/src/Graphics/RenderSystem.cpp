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
#include <cmath>
#include <memory>       // unique_ptr (stb_image pixel ownership in CreateCubemap)

#include <glm/gtc/packing.hpp>   // packHalf4x16 - RGBA16F cubemap texels

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Primitives.h"             // Primitives::MakeCube() and the rest of the whitebox set
#include "AssetPipe/GLTFLoader.h"   // GLTFLoader::LoadGLBFile - loads the demo scene's one real model
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
        m_frameManager.Init(m_context, m_swapchain);

        CreateUniformBuffers();
        // Before CreateDescriptorSets(): set 0's shared bindings point at these.
        CreateLightBuffers();
        CreateInstanceBuffers();
        CreateShadowResources();

        CreateDescriptorPool();
        CreateDescriptorSets();

        CreateViewportSampler();
        CreateViewportDescriptorPool();

        CreateMaterialSampler();
        CreateMaterialDescriptorPool();
        CreateDefaultTextures();
        m_defaultMaterial = CreateMaterial(MaterialData{});   // Invalid textures -> both placeholders, neutral factors

        CreateSkyboxSampler();
        CreateCubemapDescriptorPool();
        CreateDefaultSkybox();
        m_defaultEnvironment.skybox = m_defaultSkybox;
        m_environment = m_defaultEnvironment;
        m_lightQueue.reserve(kMaxLights);

        CreatePrimitiveMeshes();

        MaterialData demoMaterialData;
        demoMaterialData.albedo = CreateTexture("../../../Assets/Textures/Orange_Milk.jpg");
        m_demoMaterial = CreateMaterial(demoMaterialData);

        std::string const demoModelPath = "../../../Assets/Models/Cannon.glb";
        std::string const cacheKeyPrefix = "GLB:" + demoModelPath + ":";

        if (GLTFAsset glb = GLTFLoader::LoadGLBFile(demoModelPath); glb.ok) {
            // Prefer the glTF file's own mesh name (Cannon.glb's is "CannonMesh"); fall back to
            // the filename when the asset didn't set one (glTF's mesh.name is optional).
            std::string const meshName = !glb.meshName.empty()
                ? glb.meshName
                : std::filesystem::path(demoModelPath).stem().string();
            m_glbMesh = CreateMesh(glb.mesh, meshName);

            MaterialData glbMaterialData;
            if (glb.material) {
                GLTFMaterial const& gm = *glb.material;
                glbMaterialData.factors.albedoTint     = gm.baseColorFactor;
                glbMaterialData.factors.emissiveFactor = gm.emissiveFactor;
                glbMaterialData.factors.metallicFactor  = gm.metallicFactor;
                glbMaterialData.factors.roughnessFactor = gm.roughnessFactor;

                // Same sRGB/linear split as Inspector's material widget (Editor/src/ImGui/
                // Panels/ImGuiPanel_Inspector.cpp): albedo/emissive are colour data, the rest
                // are linear. Only the maps this sample asset actually has are non-null; the
                // others simply stay unset and CreateMaterial substitutes its own placeholders.
                if (gm.hasBaseColor) {
                    auto const& img = glb.images[gm.baseColorImage];
                    glbMaterialData.albedo = CreateTextureFromMemory(img.bytes.data(), img.bytes.size(), true, cacheKeyPrefix + "albedo");
                }
                if (gm.hasNormal) {
                    auto const& img = glb.images[gm.normalImage];
                    glbMaterialData.normal = CreateTextureFromMemory(img.bytes.data(), img.bytes.size(), false, cacheKeyPrefix + "normal");
                }
                if (gm.hasMetallicRoughness) {
                    auto const& img = glb.images[gm.metallicRoughnessImage];
                    glbMaterialData.metallicRoughness = CreateTextureFromMemory(img.bytes.data(), img.bytes.size(), false, cacheKeyPrefix + "metallicRoughness");
                }
                if (gm.hasEmissive) {
                    auto const& img = glb.images[gm.emissiveImage];
                    glbMaterialData.emissive = CreateTextureFromMemory(img.bytes.data(), img.bytes.size(), true, cacheKeyPrefix + "emissive");
                }
                if (gm.hasOcclusion) {
                    auto const& img = glb.images[gm.occlusionImage];
                    glbMaterialData.occlusion = CreateTextureFromMemory(img.bytes.data(), img.bytes.size(), false, cacheKeyPrefix + "occlusion");
                }
            }
            m_glbMaterial = CreateMaterial(glbMaterialData);

            CRU_INFO("[Renderer] Loaded {}: {} vertices, {} indices",
                demoModelPath, glb.mesh.vertices.size(), glb.mesh.indices.size());
        } else {
            CRU_WARN("[Renderer] Could not load {}: {}", demoModelPath, glb.error);
        }

        m_renderQueue.reserve(1024);
    }

    void Renderer::ShutDown() { m_context.ShutDown(); }

    void Renderer::Update(float dt) {

        m_time += dt;

        // Finish a few queued async mesh uploads (see CreateMeshAsync) before drawing this frame.
        ProcessPendingMeshUploads();

        // POPULATE
        //   Persistent proxies were updated by RenderExtractSystem (SetRenderProxy) during the ECS
        //   update; immediate Submit() items (the demo scene) are added here.
        //if (m_demoScene) { SubmitDemoScene(); }

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

        bool err = false;
        FrameContext fc = m_frameManager.BeginFrame(m_context, m_swapchain, err);
        if (err) { return; }

        const uint32_t frameIndex = m_frameManager.GetFrameIndex();

        // Lights first: every view's uniforms read the light count and shadow light this picks.
        UpdateLightBuffer(frameIndex);
        UpdateDebugBuffer(frameIndex);

        // PREPARE: cull and batch every view (main pass, shadow cascades, selection mask) before
        // recording anything, so the instance buffer's final size is known up front and it never
        // has to grow while commands already reference it.
        m_instanceScratch.clear();
        if (m_viewports.empty()) {
            const vk::Extent2D extent = m_swapchain.GetExtent();
            PrepareView(m_camera, extent, m_windowCullStats, m_windowDraws, false, m_uniformBuffersMapped[frameIndex]);
        } else {
            for (auto& [id, vp] : m_viewports) {
                PrepareView(vp.camera, vp.extent, vp.cullStats, vp.draws, vp.outline, vp.uniformBuffersMapped[frameIndex]);
            }
        }
        UploadInstances(frameIndex);

        // Record and submit command buffer.
        RecordCommandBuffer(fc);

        m_frameManager.EndFrame(m_context, m_swapchain, fc, m_window);

    }   // Renderer::DrawFrame()

    void Renderer::Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model) {

        m_renderQueue.push_back(RenderItem{ mesh, material, model });

    }   // Renderer::Submit()

    void Renderer::SubmitDemoScene() {

        // Demo lighting: a dimmed sun, three coloured point lights orbiting the grid, and a
        // spotlight sweeping over its centre - enough to see every light type at once. (With
        // the demo scene off and no Light entities, the fallback sun in UpdateLightBuffer()
        // lights the scene instead.)
        {
            LightData sun;
            sun.type      = LightType::Directional;
            sun.direction = -glm::vec3(0.2f, 0.8f, 0.5f);
            sun.intensity = 1.5f;
            sun.castShadows = true;
            SubmitLight(sun);

            const glm::vec3 colours[3] = { { 1.0f, 0.35f, 0.2f }, { 0.25f, 1.0f, 0.4f }, { 0.3f, 0.5f, 1.0f } };
            for (int k = 0; k < 3; ++k) {
                const float angle = m_time * 0.6f + static_cast<float>(k) * 2.0943951f;   // 120 degrees apart
                LightData point;
                point.type      = LightType::Point;
                point.position  = glm::vec3(std::cos(angle) * 5.0f, 1.5f, std::sin(angle) * 5.0f);
                point.colour    = colours[k];
                point.intensity = 20.0f;
                point.range     = 9.0f;
                SubmitLight(point);
            }

            LightData spot;
            spot.type         = LightType::Spot;
            spot.position     = glm::vec3(0.0f, 7.0f, 0.0f);
            spot.direction    = glm::vec3(std::sin(m_time * 0.5f) * 0.6f, -1.0f, std::cos(m_time * 0.5f) * 0.6f);
            spot.colour       = glm::vec3(1.0f, 0.9f, 0.7f);
            spot.intensity    = 80.0f;
            spot.range        = 20.0f;
            spot.innerConeDeg = 12.0f;
            spot.outerConeDeg = 22.0f;
            SubmitLight(spot);
        }

        // 7x7 grid of cubes on the XZ plane, each spinning at a slightly different phase.
        constexpr int   half    = 3;
        constexpr float spacing = 2.0f;

        int i = 0;

        for (int x = -half; x <= half; ++x) {
            for (int z = -half; z <= half; ++z) {
                const float phase = static_cast<float>(x * 7 + z * 3);
                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x * spacing, 0.0f, z * spacing));
                model = glm::rotate(model, (m_time + phase) * glm::radians(20.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::rotate(model, (m_time + phase) * glm::radians(10.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                Submit(m_primitiveMeshes[i], m_demoMaterial, model);

                i++;
                i = i % m_primitiveMeshes.size();
            }
        }

        if (m_glbMesh.IsValid()) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 0.0f));
            model = glm::rotate(model, m_time * glm::radians(15.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(1.0f));
            Submit(m_glbMesh, m_glbMaterial.IsValid() ? m_glbMaterial : GetDefaultMaterial(), model);
        }

    }   // Renderer::SubmitDemoScene()

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

        // ---- Offscreen editor viewports -------------------------------------------------------
        // Empty in a standalone Engine/Game build: this loop simply doesn't run, so the cost of
        // supporting viewports at all is the one `m_viewports.empty()` check further down.
        for (auto& [id, vp] : m_viewports) {

            const vk::DescriptorSet frameSet = *vp.descriptorSets[frameIndex];

            // Passes that feed this view's main pass: its shadow cascades into the shared shadow
            // map, and the selected object into its outline mask.
            DrawShadowCascades(fc, frameSet, vp.draws);
            if (vp.outline && !vp.draws.outline.empty()) { DrawOutlineMask(fc, vp, frameSet); }

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
            DrawRenderQueue(*fc.frame.commandBuffer, vp.extent, frameSet, vp.debugDraw, vp.draws.main);
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
        if (m_viewports.empty()) { DrawShadowCascades(fc, *m_descriptorSets[frameIndex], m_windowDraws); }

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
            DrawRenderQueue(*fc.frame.commandBuffer, m_swapchain.GetExtent(), *m_descriptorSets[frameIndex], m_debugInWindow, m_windowDraws.main);
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

    void Renderer::DrawRenderQueue(vk::CommandBuffer cmd, vk::Extent2D extent, vk::DescriptorSet cameraSet, bool drawDebug,
                                   const std::vector<DrawBatch>& batches) {

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetPipeline());
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

        // Debug lines last, over everything (the depth-tested ones still respect mesh depth).
        if (drawDebug) { DrawDebugLines(cmd, cameraSet); }

    }   // Renderer::DrawRenderQueue()

    MeshHandle Renderer::CreateMesh(const MeshData& data, const std::string& debugName) {

        MeshHandle handle{ static_cast<uint32_t>(m_meshes.size()) };
        m_meshes.emplace_back();       // reserve slot (indexCount == 0 => not yet renderable)
        UploadMesh(handle, data, debugName);      // ... filled in synchronously, right here
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
        mesh.vertexBuffer.UploadStaging(m_context, data.vertices.data(), vbSize, m_frameManager.GetCommandPool());

        vk::DeviceSize ibSize = sizeof(uint32_t) * data.indices.size();
        mesh.indexBuffer.Create(m_context, ibSize,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        mesh.indexBuffer.UploadStaging(m_context, data.indices.data(), ibSize, m_frameManager.GetCommandPool());

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

    }   // Renderer::CreateViewportSampler()

    void Renderer::CreateViewportDescriptorPool() {

        // Sized up front for the worst case (kMaxViewports simultaneously alive, each with its
        // own set per frame in flight) so viewport creation never has to touch pool capacity.
        uint32_t maxSets = kMaxViewports * m_frameManager.MaxFramesInFlight();
        // Each set 0 = FrameUniforms UBO (binding 0), light + instance storage buffers (1, 2),
        // shadow map image + comparison sampler (3, 4).
        std::array<vk::DescriptorPoolSize, 4> poolSizes{ {
            { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = maxSets },
            { .type = vk::DescriptorType::eStorageBuffer, .descriptorCount = maxSets * 2 },
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
        // Each set 0 = FrameUniforms UBO (binding 0), light + instance storage buffers (1, 2),
        // shadow map image + comparison sampler (3, 4).
        std::array<vk::DescriptorPoolSize, 4> poolSizes{ {
            { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = maxFrames },
            { .type = vk::DescriptorType::eStorageBuffer, .descriptorCount = maxFrames * 2 },
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

    TextureHandle Renderer::UploadTexturePixels(const unsigned char* pixels, uint32_t width, uint32_t height, bool isSRGB) {

        const vk::DeviceSize imageSize = vk::DeviceSize{ width } * height * 4;

        VulkanBuffer stagingBuff;
        stagingBuff.Create(m_context, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        void* mapped = stagingBuff.Map();
        memcpy(mapped, pixels, imageSize);
        stagingBuff.Unmap();

        GpuTexture tex;
        tex.width = width;
        tex.height = height;
        const vk::Format format = isSRGB ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
        tex.image.Create(m_context, width, height, format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        vk::raii::CommandBuffer commandBuffer = BeginSingleTimeCommand();
        TransitionImageLayout_I(commandBuffer, tex.image.GetImage(), vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
        CopyBufferToImage(commandBuffer, stagingBuff.GetBuffer(), tex.image.GetImage(), width, height);
        TransitionImageLayout_I(commandBuffer, tex.image.GetImage(), vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
        EndSingleTimeCommand(std::move(commandBuffer));

        vk::ImageViewCreateInfo viewInfo{
            .image = tex.image.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 }
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
        // framebuffer image).
        vk::SamplerCreateInfo samplerInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .anisotropyEnable = vk::False,
            .compareEnable = vk::False,
            .minLod = 0.0f,
            .maxLod = 0.0f,
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

        for (size_t i = 0; i < sets.size() && i < m_lightBuffers.size() && i < m_instanceBuffers.size(); ++i) {
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

            const std::array<vk::WriteDescriptorSet, 4> writes{ {
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

        // Same frame-in-flight safety as the uniform buffers: BeginFrame() has already waited on
        // this frame index's fence, so the GPU is done reading m_lightBuffers[frameIndex].
        if (m_lightQueue.empty()) {
            const GpuLight fallback = ToGpuLight(MakeFallbackSun());
            memcpy(m_lightBuffersMapped[frameIndex], &fallback, sizeof(GpuLight));
            m_frameLightCount  = 1;
            m_shadowLightIndex = 0;   // the fallback sun always casts
            m_shadowLightDir   = glm::vec3(fallback.directionType);
            return;
        }

        m_frameLightCount = static_cast<uint32_t>(m_lightQueue.size());   // already capped by SubmitLight
        memcpy(m_lightBuffersMapped[frameIndex], m_lightQueue.data(), sizeof(GpuLight) * m_frameLightCount);
        if (m_shadowLightIndex >= 0) { m_shadowLightDir = glm::vec3(m_lightQueue[m_shadowLightIndex].directionType); }

    }   // Renderer::UpdateLightBuffer()

    void Renderer::FillEnvironmentUniforms(FrameUniforms& ubo) const {

        const EnvironmentSettings& env = m_environment;
        ubo.ambientSky    = glm::vec4(env.ambientSky * env.ambientIntensity, 0.0f);
        ubo.ambientGround = glm::vec4(env.ambientGround * env.ambientIntensity, 0.0f);
        ubo.renderParams  = glm::vec4(std::max(env.exposure, 0.0f), std::max(env.skyboxIntensity, 0.0f), 0.0f, 0.0f);
        ubo.lightParams   = glm::uvec4(m_frameLightCount, 0u, 0u, 0u);

    }   // Renderer::FillEnvironmentUniforms()

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

        VulkanBuffer stagingBuff;
        stagingBuff.Create(m_context, totalBytes, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        memcpy(stagingBuff.Map(), faceData, static_cast<size_t>(totalBytes));
        stagingBuff.Unmap();

        GpuCubemap cube;
        cube.faceSize  = faceSize;
        cube.debugName = debugName;
        cube.image.Create(m_context, faceSize, faceSize, format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            /*arrayLayers*/ 6, vk::ImageCreateFlagBits::eCubeCompatible);

        vk::raii::CommandBuffer commandBuffer = BeginSingleTimeCommand();
        TransitionImageLayout_I(commandBuffer, cube.image.GetImage(), vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 6);
        CopyBufferToImage(commandBuffer, stagingBuff.GetBuffer(), cube.image.GetImage(), faceSize, faceSize, 6);
        TransitionImageLayout_I(commandBuffer, cube.image.GetImage(), vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 6);
        EndSingleTimeCommand(std::move(commandBuffer));

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

    void Renderer::SetRenderProxy(RenderProxyHandle handle, MeshHandle mesh, MaterialHandle material, const glm::mat4& model, bool visible) {

        if (!handle.IsValid() || handle.id >= m_proxies.size() || !m_proxies[handle.id].alive) { return; }
        RenderProxy& p = m_proxies[handle.id];

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
            m_visibleItems.push_back(RenderItem{ p.mesh, p.material, p.model });
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
        ubo.view        = camera.GetView();
        ubo.proj        = camera.GetProjection();
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
            m_instanceScratch.push_back(InstanceData{ item.model });
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
            glm::mat4       lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + ext);

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

    void Renderer::DrawDebugLines(vk::CommandBuffer cmd, vk::DescriptorSet frameSet) {

        if (m_debugTestedCount + m_debugOnTopCount == 0) { return; }
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

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetSkyboxPipeline());

        // Set 0 must be rebound even though the mesh pass just bound the same set: the skybox
        // layout has no push-constant range, so it isn't "compatible" with the main layout for
        // set 0 and the earlier binding is disturbed by this pipeline's layout.
        const std::array<vk::DescriptorSet, 2> sets{ frameSet, *m_cubemaps[sky.id].descriptorSet };
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetSkyboxLayout(), 0, sets, nullptr);
        cmd.draw(36, 1, 0, 0);

    }   // Renderer::DrawSkybox()

    void Renderer::CopyBufferToImage(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height,
        uint32_t layerCount) {

        // With bufferRowLength/bufferImageHeight 0 (tightly packed), a multi-layer copy reads
        // each layer from the buffer immediately after the previous one - exactly how
        // UploadCubemapFaces lays out its six faces.

        vk::BufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = layerCount},
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
        vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t layerCount) {

        vk::ImageMemoryBarrier barrier{ .oldLayout = oldLayout,
                                       .newLayout = newLayout,
                                       .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                                       .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                                       .image = image,
                                       .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = layerCount} };

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
