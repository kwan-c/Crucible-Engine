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
        m_pipeline.CreatePipeline(m_context, m_swapchain);
        m_frameManager.Init(m_context, m_swapchain);

        CreateUniformBuffers();

        CreateDescriptorPool();
        CreateDescriptorSets();

        CreateViewportSampler();
        CreateViewportDescriptorPool();

        CreateMaterialSampler();
        CreateMaterialDescriptorPool();
        CreateDefaultTextures();
        m_defaultMaterial = CreateMaterial(MaterialData{});   // Invalid textures -> both placeholders, neutral factors

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

        // QUERY & CULL
        //   (Frustum culling would go here: m_camera.GetViewProjection() gives you the planes.)

        // POPULATE RENDER QUEUE
        //   The ECS extraction system calls Submit() before Update(). The demo scene is a stand-in.
        if (m_demoScene) { SubmitDemoScene(); }

        // SORT & BATCH
        //   Group by (material, mesh) so both the material descriptor set and the mesh's
        //   vertex/index buffers are rebound as rarely as possible.
        std::stable_sort(m_renderQueue.begin(), m_renderQueue.end(),
            [](const RenderItem& a, const RenderItem& b) {
                if (!(a.material == b.material)) { return a.material < b.material; }
                return a.mesh < b.mesh;
            });

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

    void Renderer::Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model) {

        m_renderQueue.push_back(RenderItem{ mesh, material, model });

    }   // Renderer::Submit()

    void Renderer::UpdateFrameUniforms(uint32_t frameIndex) {

        // Keep the projection in sync with the swapchain (handles window resize; skips minimised 0-height).
        const vk::Extent2D extent = m_swapchain.GetExtent();
        m_camera.SetAspect(static_cast<float>(extent.width), static_cast<float>(extent.height));

        FrameUniforms ubo{};
        ubo.view = m_camera.GetView();
        ubo.proj = m_camera.GetProjection();
        ubo.cameraPosWS = glm::vec4(m_camera.position, 0.0f);

        memcpy(m_uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));

    }   // Renderer::UpdateFrameUniforms()

    void Renderer::SubmitDemoScene() {

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
            model = glm::scale(model, glm::vec3(2.0f));   // the sample asset is a 1x1x1 unit cube
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

        // Camera data (view/proj) is bound once for the whole pass, at set 0.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline.GetLayout(), 0, cameraSet, nullptr);

        // Draw every submitted object. Queue is sorted by (material, mesh), so both the material
        // descriptor set (set 1) and the mesh's vertex/index buffers rebind only on change.
        MeshHandle     boundMesh;       // starts Invalid
        MaterialHandle boundMaterial;   // starts Invalid
        for (const RenderItem& item : m_renderQueue) {

            if (!item.mesh.IsValid() || item.mesh.id >= m_meshes.size()) { continue; }
            const GpuMesh& mesh = m_meshes[item.mesh.id];
            if (mesh.indexCount == 0) { continue; }   // still uploading (CreateMeshAsync); skip for now

            // Every draw needs SOME material set bound (the pipeline layout declares set 1
            // unconditionally) - fall back to the default material rather than skipping the draw
            // or leaving set 1 unbound.
            MaterialHandle material = item.material.IsValid() && item.material.id < m_materials.size()
                ? item.material : m_defaultMaterial;
            const GpuMaterial& mat = m_materials[material.id];

            if (!(material == boundMaterial)) {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline.GetLayout(), 1, *mat.descriptorSet, nullptr);
                boundMaterial = material;
            }

            if (!(item.mesh == boundMesh)) {
                cmd.bindVertexBuffers(0, *mesh.vertexBuffer.GetBuffer(), {0});
                cmd.bindIndexBuffer(*mesh.indexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);
                boundMesh = item.mesh;
            }

            // Per-object model matrix + the current material's scalar factors go in one push
            // constant (recorded into the command buffer, so there are no per-frame-in-flight
            // synchronisation concerns - unlike the descriptor set bound above).
            PushConstants pc{ item.model, mat.data.factors };
            cmd.pushConstants<PushConstants>(m_pipeline.GetLayout(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pc);
            cmd.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
        }

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

        m_meshes[handle.id] = std::move(mesh);

    }   // Renderer::UploadMesh()

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

    }   // Renderer::CreateViewportUniformsAndDescriptors()

    void Renderer::UpdateViewportUniforms(Viewport& vp, uint32_t frameIndex) {

        FrameUniforms ubo{};
        ubo.view = vp.camera.GetView();
        ubo.proj = vp.camera.GetProjection();
        ubo.cameraPosWS = glm::vec4(vp.camera.position, 0.0f);
        memcpy(vp.uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));

    }   // Renderer::UpdateViewportUniforms()

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
