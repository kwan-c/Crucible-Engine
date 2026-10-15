/*************************************************************************
  \file       ScenePass.cpp
  \brief
    See ScenePass.h.
*************************************************************************/
#include "Graphics/Passes/ScenePass.h"

#include "Graphics/Passes/ParticlePass.h"
#include "Graphics/RenderResources.h"
#include "Graphics/Vulkan/RenderLayouts.h"
#include "Graphics/Vulkan/VulkanCommands.h"
#include "Graphics/Vulkan/VulkanShader.h"

namespace Crucible {

    static_assert(RenderLayouts::kMsaaLevels == 3, "ScenePass keeps one pipeline per MSAA level");

    void ScenePass::Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout materialSetLayout,
                           vk::DescriptorSetLayout skySetLayout, vk::Format colourFormat, vk::Format depthFormat) {

        // ---- Meshes ----
        {
            // Programmable stages (Vertex and Fragment shaders) ----------------------------------- //
            vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "../../../Assets/Shaders/DefaultShader.spv");

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

            // Fixed functions --------------------------------------------------------------------- //
            auto bindingDescription = Vertex::GetBindingDescription();
            auto attributeDescriptions = Vertex::GetAttributeDescriptions();
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
                .vertexBindingDescriptionCount = 1,
                .pVertexBindingDescriptions = &bindingDescription,
                .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
                .pVertexAttributeDescriptions = attributeDescriptions.data()
            };
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

            // Set viewport to entire swapchain extent, make them dynamic for more flexibility in future.
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
                .frontFace = vk::FrontFace::eCounterClockwise,
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

            // Depth testing so overlapping meshes sort correctly (a single hard-coded cube never
            // needed this; multiple/imported models will).
            vk::PipelineDepthStencilStateCreateInfo depthStencil{
                .depthTestEnable = vk::True,
                .depthWriteEnable = vk::True,
                .depthCompareOp = vk::CompareOp::eLess,
                .depthBoundsTestEnable = vk::False,
                .stencilTestEnable = vk::False
            };

            // Pipeline layout object. Set 0 = per-view frame data (camera, lights, instances, shadow
            // map). Set 1 = the current draw's material (five texture bindings - see
            // CreateMaterialDescriptorSetLayout). The push constant carries the material's scalar
            // factors (PushConstants, RenderTypes.h) - fragment only, since the per-object model
            // matrix now comes from the instance buffer.
            vk::PushConstantRange pushConstantRange{
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
                .offset     = 0,
                .size       = sizeof(PushConstants)
            };
            std::array<vk::DescriptorSetLayout, 2> setLayouts{ frameSetLayout, materialSetLayout };
            vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
                .setLayoutCount         = static_cast<uint32_t>(setLayouts.size()),
                .pSetLayouts            = setLayouts.data(),
                .pushConstantRangeCount = 1,
                .pPushConstantRanges    = &pushConstantRange
            };
            m_meshLayout = vk::raii::PipelineLayout(context.GetDevice(), pipelineLayoutInfo);

            // The scene is drawn into the HDR image, not the swapchain (see kSceneColorFormat).
            vk::Format swapchainFormat = colourFormat;

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
                    .pDepthStencilState = &depthStencil,
                    .pColorBlendState = &colorBlending,
                    .pDynamicState = &dynamicState,
                    .layout = m_meshLayout,
                    .renderPass = nullptr
                },
                {.colorAttachmentCount = 1, .pColorAttachmentFormats = &swapchainFormat, .depthAttachmentFormat = depthFormat }
            };
            // One pipeline per MSAA level (1, 2, 4 samples per pixel): the sample count is baked into a
            // pipeline and must match the target it draws into. `multisampling` is referenced by
            // pointer above, so changing it here changes what the next pipeline is built with.
            for (uint32_t i = 0; i < RenderLayouts::kMsaaLevels; ++i) {
                multisampling.rasterizationSamples = static_cast<vk::SampleCountFlagBits>(1u << i);
                m_meshPipelines[i] = vk::raii::Pipeline(context.GetDevice(), nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
            }
        }

        // ---- Sky ----
        {
            vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "../../../Assets/Shaders/Skybox.spv");

            vk::PipelineShaderStageCreateInfo shaderStages[] = {
                { .stage = vk::ShaderStageFlagBits::eVertex,   .module = shaderModule, .pName = "skyVert" },
                { .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "skyFrag" }
            };

            // No vertex buffers at all: the vertex shader builds the unit cube's 36 corners from
            // SV_VertexID (a hardcoded table), so the skybox needs no mesh upload of its own.
            vk::PipelineVertexInputStateCreateInfo   vertexInputInfo{};
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

            std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dynamicState{
                .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
                .pDynamicStates    = dynamicStates.data()
            };
            vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

            // The camera sits INSIDE the cube, so its faces are seen from the back; simplest to not
            // cull at all (36 vertices - the cost is nothing).
            vk::PipelineRasterizationStateCreateInfo rasterizer{
                .depthClampEnable        = vk::False,
                .rasterizerDiscardEnable = vk::False,
                .polygonMode             = vk::PolygonMode::eFill,
                .cullMode                = vk::CullModeFlagBits::eNone,
                .frontFace               = vk::FrontFace::eCounterClockwise,
                .depthBiasEnable         = vk::False,
                .lineWidth               = 1.0f
            };
            vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };
            vk::PipelineColorBlendAttachmentState colorBlendAttachment{
                .blendEnable    = vk::False,
                .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
            };
            vk::PipelineColorBlendStateCreateInfo colorBlending{
                .logicOpEnable   = vk::False,
                .logicOp         = vk::LogicOp::eCopy,
                .attachmentCount = 1,
                .pAttachments    = &colorBlendAttachment
            };

            // Drawn AFTER the opaque meshes: the vertex shader pins every sky fragment to the far
            // plane (depth exactly 1.0, via clip.xyww), so LESS_OR_EQUAL against the 1.0 clear value
            // passes only where no mesh was drawn - no overdraw behind geometry, and nothing to
            // write back (the sky is by definition the farthest thing there is).
            vk::PipelineDepthStencilStateCreateInfo depthStencil{
                .depthTestEnable       = vk::True,
                .depthWriteEnable      = vk::False,
                .depthCompareOp        = vk::CompareOp::eLessOrEqual,
                .depthBoundsTestEnable = vk::False,
                .stencilTestEnable     = vk::False
            };

            // Set 0 = the SAME per-frame layout as the main pipeline (view/proj + exposure live in
            // FrameUniforms), set 1 = the cubemap. No push constants - which also makes this layout
            // NOT "compatible" with the main one for set 0 (compatibility requires identical push
            // constant ranges), so Renderer::DrawSkybox() rebinds set 0 itself.
            std::array<vk::DescriptorSetLayout, 2> setLayouts{ frameSetLayout, skySetLayout };
            vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
                .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
                .pSetLayouts    = setLayouts.data()
            };
            m_skyLayout = vk::raii::PipelineLayout(context.GetDevice(), pipelineLayoutInfo);

            // The scene is drawn into the HDR image, not the swapchain (see kSceneColorFormat).
            vk::Format swapchainFormat = colourFormat;

            vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain =
            {
                {
                    .stageCount          = 2,
                    .pStages             = shaderStages,
                    .pVertexInputState   = &vertexInputInfo,
                    .pInputAssemblyState = &inputAssembly,
                    .pViewportState      = &viewportState,
                    .pRasterizationState = &rasterizer,
                    .pMultisampleState   = &multisampling,
                    .pDepthStencilState  = &depthStencil,
                    .pColorBlendState    = &colorBlending,
                    .pDynamicState       = &dynamicState,
                    .layout              = m_skyLayout,
                    .renderPass          = nullptr
                },
                {.colorAttachmentCount = 1, .pColorAttachmentFormats = &swapchainFormat, .depthAttachmentFormat = depthFormat }
            };
            // One pipeline per MSAA level (1, 2, 4 samples per pixel): the sample count is baked into a
            // pipeline and must match the target it draws into. `multisampling` is referenced by
            // pointer above, so changing it here changes what the next pipeline is built with.
            for (uint32_t i = 0; i < RenderLayouts::kMsaaLevels; ++i) {
                multisampling.rasterizationSamples = static_cast<vk::SampleCountFlagBits>(1u << i);
                m_skyPipelines[i] = vk::raii::Pipeline(context.GetDevice(), nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
            }
        }

    }   // ScenePass::Create()

    void ScenePass::Record(vk::CommandBuffer cmd, PostTargets& t, vk::Image depthImage, vk::ImageView depthView, vk::Extent2D extent,
                           vk::DescriptorSet frameSet, const std::vector<DrawBatch>& meshes, vk::DescriptorSet skySet,
                           const std::vector<ParticleBatch>& particles) const {

        // Last sampled by the previous frame's bloom/tonemap; its contents are re-cleared.
        TransitionImageLayout(cmd, t.hdrImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
            {}, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);
        TransitionImageLayout(cmd, depthImage, vk::ImageAspectFlagBits::eDepth,
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
            TransitionImageLayout(cmd, t.msaaColor.GetImage(), vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                {}, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eColorAttachmentOutput);
            TransitionImageLayout(cmd, t.msaaDepth.GetImage(), vk::ImageAspectFlagBits::eDepth,
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
        DrawQueue(cmd, msaa ? t.samples : 1, extent, frameSet, meshes, skySet, particles);
        cmd.endRendering();

        TransitionImageLayout(cmd, t.hdrImage.GetImage(), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader);

    }   // ScenePass::Record()

    void ScenePass::DrawQueue(vk::CommandBuffer cmd, uint32_t samples, vk::Extent2D extent, vk::DescriptorSet cameraSet,
                              const std::vector<DrawBatch>& meshes, vk::DescriptorSet skySet, const std::vector<ParticleBatch>& particles) const {

        const std::vector<DrawBatch>& batches = meshes;

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_meshPipelines[RenderLayouts::MsaaIndex(samples)]);
        cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(extent.height), static_cast<float>(extent.width), -static_cast<float>(extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));

        // Per-view data (camera, lights, instances, shadow map) is bound once for the whole pass.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_meshLayout, 0, cameraSet, nullptr);

        // One instanced draw per batch (PrepareView/AppendBatches). Batches are in (material, mesh)
        // order, so the material set (set 1) and the mesh's buffers rebind only on change.
        MeshHandle     boundMesh;       // starts Invalid
        MaterialHandle boundMaterial;   // starts Invalid
        for (const DrawBatch& batch : batches) {

            const GpuMesh&     mesh = m_resources.Meshes()[batch.mesh.id];          // validated in AppendBatches
            const GpuMaterial& mat  = m_resources.Materials()[batch.material.id];   // default-substituted there too

            if (!(batch.material == boundMaterial)) {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_meshLayout, 1, *mat.descriptorSet, nullptr);
                // The material's scalar factors, recorded into the command buffer (so an Inspector
                // slider change needs no GPU synchronization). Constant across the batch.
                const PushConstants pc{ mat.data.factors };
                cmd.pushConstants<PushConstants>(*m_meshLayout, vk::ShaderStageFlagBits::eFragment, 0, pc);
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
        if (skySet) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_skyPipelines[RenderLayouts::MsaaIndex(samples)]);
            // Set 0 must be rebound even though the mesh pass just bound the same set: the skybox
            // layout has no push-constant range, so it isn't "compatible" with the main layout for
            // set 0 and the earlier binding is disturbed by this pipeline's layout.
            const std::array<vk::DescriptorSet, 2> sets{ cameraSet, skySet };
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_skyLayout, 0, sets, nullptr);
            cmd.draw(36, 1, 0, 0);
        }

        // Particles after everything opaque and the sky: they blend over what is behind them.
        m_particles.Record(cmd, samples, cameraSet, particles);

    }   // ScenePass::DrawQueue()

}   // NAMESPACE CRUCIBLE
