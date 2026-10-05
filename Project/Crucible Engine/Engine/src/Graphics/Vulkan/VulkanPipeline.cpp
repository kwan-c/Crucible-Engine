/*************************************************************************
* \file:         VulkanPipeline.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                See VulkanPipeline.h for what this class owns and why.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "VulkanPipeline.h"
#include "../RenderTypes.h"   // PushConstants (model matrix + material factors)

#include <fstream>

namespace Crucible {

    // ------------------------------------------------------------------------------------------------------------- //
    // ACCESSORS
    // ------------------------------------------------------------------------------------------------------------- //
    
    const vk::raii::Pipeline&               VulkanPipeline::GetPipeline()               const { return m_graphicsPipeline; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetLayout()                 const { return m_pipelineLayout; }
    const vk::raii::DescriptorSetLayout&    VulkanPipeline::GetDescriptorSetLayout()    const { return m_descriptorSetLayout; }
    const vk::raii::DescriptorSetLayout&    VulkanPipeline::GetMaterialDescriptorSetLayout() const { return m_materialDescriptorSetLayout; }
    const vk::raii::Pipeline&               VulkanPipeline::GetSkyboxPipeline()         const { return m_skyboxPipeline; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetSkyboxLayout()           const { return m_skyboxPipelineLayout; }
    const vk::raii::DescriptorSetLayout&    VulkanPipeline::GetSkyboxDescriptorSetLayout() const { return m_skyboxDescriptorSetLayout; }
    const vk::raii::Pipeline&               VulkanPipeline::GetDebugLinePipeline(bool depthTested) const { return depthTested ? m_debugLinePipeline : m_debugLineOnTopPipeline; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetDebugLineLayout()        const { return m_debugLineLayout; }
    const vk::raii::Pipeline&               VulkanPipeline::GetShadowPipeline()         const { return m_shadowPipeline; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetShadowLayout()           const { return m_shadowLayout; }
    const vk::raii::Pipeline&               VulkanPipeline::GetOutlineMaskPipeline()    const { return m_outlineMaskPipeline; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetOutlineMaskLayout()      const { return m_outlineMaskLayout; }
    const vk::raii::Pipeline&               VulkanPipeline::GetOutlineCompositePipeline() const { return m_outlineCompositePipeline; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetOutlineCompositeLayout() const { return m_outlineCompositeLayout; }
    const vk::raii::DescriptorSetLayout&    VulkanPipeline::GetOutlineDescriptorSetLayout() const { return m_outlineDescriptorSetLayout; }
    const vk::raii::Pipeline&               VulkanPipeline::GetParticlePipeline()       const { return m_particlePipeline; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetParticleLayout()         const { return m_particleLayout; }
    const vk::raii::Pipeline&               VulkanPipeline::GetBloomDownPipeline()      const { return m_bloomDownPipeline; }
    const vk::raii::Pipeline&               VulkanPipeline::GetBloomUpPipeline()        const { return m_bloomUpPipeline; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetBloomLayout()            const { return m_bloomLayout; }
    const vk::raii::Pipeline&               VulkanPipeline::GetTonemapPipeline()        const { return m_tonemapPipeline; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetTonemapLayout()          const { return m_tonemapLayout; }
    const vk::raii::DescriptorSetLayout&    VulkanPipeline::GetTonemapDescriptorSetLayout() const { return m_tonemapDescriptorSetLayout; }

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN PIPELINE
    // ------------------------------------------------------------------------------------------------------------- //

    void VulkanPipeline::CreateDescriptorSetLayout(VulkanContext& context) {

        std::array<vk::DescriptorSetLayoutBinding, 8> bindings{ {
            {
                .binding            = 0,
                .descriptorType     = vk::DescriptorType::eUniformBuffer,
                .descriptorCount    = 1,
                // Fragment needs this now too: FrameUniforms::cameraPosWS feeds the PBR specular
                // term's view vector, and the ambient/exposure/light-count members are all
                // fragment-side (see DefaultShader2.slang's fragMain).
                .stageFlags         = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
            },
            {
                // This frame's lights (StructuredBuffer<GpuLight> in DefaultShader2.slang). One
                // buffer per frame in flight, shared by the window's set AND every editor
                // viewport's set for that frame - lights are world state, not per-camera.
                .binding            = 1,
                .descriptorType     = vk::DescriptorType::eStorageBuffer,
                .descriptorCount    = 1,
                .stageFlags         = vk::ShaderStageFlagBits::eFragment
            },
            {
                // This frame's instances (StructuredBuffer<InstanceData>): every object drawn by
                // every pass of every view, one model matrix each. Per frame in flight, shared
                // like the light buffer.
                .binding            = 2,
                .descriptorType     = vk::DescriptorType::eStorageBuffer,
                .descriptorCount    = 1,
                .stageFlags         = vk::ShaderStageFlagBits::eVertex
            },
            {
                // The sun's cascaded shadow map (Texture2DArray, one layer per cascade).
                .binding            = 3,
                .descriptorType     = vk::DescriptorType::eSampledImage,
                .descriptorCount    = 1,
                .stageFlags         = vk::ShaderStageFlagBits::eFragment
            },
            {
                // Its comparison sampler (SamplerComparisonState).
                .binding            = 4,
                .descriptorType     = vk::DescriptorType::eSampler,
                .descriptorCount    = 1,
                .stageFlags         = vk::ShaderStageFlagBits::eFragment
            },
            {
                // This frame's particles (StructuredBuffer<GpuParticle> in Particle.slang): every
                // particle drawn by every view. Per frame in flight, shared like the instances.
                .binding            = 5,
                .descriptorType     = vk::DescriptorType::eStorageBuffer,
                .descriptorCount    = 1,
                .stageFlags         = vk::ShaderStageFlagBits::eVertex
            },
            {
                // Light clusters: one ClusterCell per cell, for every view drawn this frame.
                .binding            = 6,
                .descriptorType     = vk::DescriptorType::eStorageBuffer,
                .descriptorCount    = 1,
                .stageFlags         = vk::ShaderStageFlagBits::eFragment
            },
            {
                // Light clusters: the light indices the cells point into.
                .binding            = 7,
                .descriptorType     = vk::DescriptorType::eStorageBuffer,
                .descriptorCount    = 1,
                .stageFlags         = vk::ShaderStageFlagBits::eFragment
            }
        } };
        vk::DescriptorSetLayoutCreateInfo layoutInfo{
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings    = bindings.data()
        };
        m_descriptorSetLayout = vk::raii::DescriptorSetLayout(context.GetDevice(), layoutInfo);

    }   // VulkanPipeline::CreateDescriptorSetLayout()

    void VulkanPipeline::CreateMaterialDescriptorSetLayout(VulkanContext& context) {

        // Set 1, bindings 0-4: albedo, normal, metallicRoughness, emissive, occlusion - all
        // fragment-stage-only combined image samplers. One of these sets is allocated per
        // Material (Renderer::CreateMaterial), never per frame-in-flight: unlike the camera UBO,
        // a material's texture bindings don't change every frame, so a single set can be reused
        // across all frames in flight (see Renderer::WriteMaterialDescriptorSet).
        std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};
        for (uint32_t i = 0; i < static_cast<uint32_t>(bindings.size()); ++i) {
            bindings[i] = vk::DescriptorSetLayoutBinding{
                .binding         = i,
                .descriptorType  = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eFragment
            };
        }
        vk::DescriptorSetLayoutCreateInfo layoutInfo{
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings    = bindings.data()
        };
        m_materialDescriptorSetLayout = vk::raii::DescriptorSetLayout(context.GetDevice(), layoutInfo);

    }   // VulkanPipeline::CreateMaterialDescriptorSetLayout()

    void VulkanPipeline::CreatePipeline(VulkanContext& context, VulkanSwapchain& swapchain) {

        // Programmable stages (Vertex and Fragment shaders) ----------------------------------- //
        vk::raii::ShaderModule shaderModule = CreateShaderModule(context, ReadFile("../../../Assets/Shaders/DefaultShader.spv"));

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
        std::array<vk::DescriptorSetLayout, 2> setLayouts{ *m_descriptorSetLayout, *m_materialDescriptorSetLayout };
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .setLayoutCount         = static_cast<uint32_t>(setLayouts.size()),
            .pSetLayouts            = setLayouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &pushConstantRange
        };
        m_pipelineLayout = vk::raii::PipelineLayout(context.GetDevice(), pipelineLayoutInfo);

        // The scene is drawn into the HDR image, not the swapchain (see kSceneColorFormat).
        vk::Format swapchainFormat = kSceneColorFormat;
        auto depthFormat     = swapchain.GetDepthFormat();

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
                .layout = m_pipelineLayout,
                .renderPass = nullptr
            },
            {.colorAttachmentCount = 1, .pColorAttachmentFormats = &swapchainFormat, .depthAttachmentFormat = depthFormat }
        };
        m_graphicsPipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());


    }   // VulkanPipeline::CreatePipeline()

    void VulkanPipeline::CreateSkyboxDescriptorSetLayout(VulkanContext& context) {

        // Skybox pipeline's set 1, binding 0: the cubemap (SamplerCube in Skybox.slang). One set
        // per cubemap (Renderer::CreateCubemap*), never per frame - same reasoning as materials.
        vk::DescriptorSetLayoutBinding binding{
            .binding         = 0,
            .descriptorType  = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eFragment
        };
        vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = 1, .pBindings = &binding };
        m_skyboxDescriptorSetLayout = vk::raii::DescriptorSetLayout(context.GetDevice(), layoutInfo);

    }   // VulkanPipeline::CreateSkyboxDescriptorSetLayout()

    void VulkanPipeline::CreateSkyboxPipeline(VulkanContext& context, VulkanSwapchain& swapchain) {

        vk::raii::ShaderModule shaderModule = CreateShaderModule(context, ReadFile("../../../Assets/Shaders/Skybox.spv"));

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
        std::array<vk::DescriptorSetLayout, 2> setLayouts{ *m_descriptorSetLayout, *m_skyboxDescriptorSetLayout };
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
            .pSetLayouts    = setLayouts.data()
        };
        m_skyboxPipelineLayout = vk::raii::PipelineLayout(context.GetDevice(), pipelineLayoutInfo);

        // The scene is drawn into the HDR image, not the swapchain (see kSceneColorFormat).
        vk::Format swapchainFormat = kSceneColorFormat;
        auto depthFormat     = swapchain.GetDepthFormat();

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
                .layout              = m_skyboxPipelineLayout,
                .renderPass          = nullptr
            },
            {.colorAttachmentCount = 1, .pColorAttachmentFormats = &swapchainFormat, .depthAttachmentFormat = depthFormat }
        };
        m_skyboxPipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());

    }   // VulkanPipeline::CreateSkyboxPipeline()

    void VulkanPipeline::CreateDebugLinePipelines(VulkanContext& context, VulkanSwapchain& swapchain) {

        vk::raii::ShaderModule shaderModule = CreateShaderModule(context, ReadFile("../../../Assets/Shaders/DebugLine.spv"));

        vk::PipelineShaderStageCreateInfo shaderStages[] = {
            { .stage = vk::ShaderStageFlagBits::eVertex,   .module = shaderModule, .pName = "lineVert" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "lineFrag" }
        };

        // DebugVertex (DebugDraw.h): vec3 position + RGBA8 colour, 16 bytes. The colour is read as
        // UNORM so the shader receives 0..1 floats with no unpacking.
        vk::VertexInputBindingDescription binding{ .binding = 0, .stride = 16, .inputRate = vk::VertexInputRate::eVertex };
        std::array<vk::VertexInputAttributeDescription, 2> attributes{ {
            { .location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = 0 },
            { .location = 1, .binding = 0, .format = vk::Format::eR8G8B8A8Unorm,   .offset = 12 }
        } };
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount   = 1,
            .pVertexBindingDescriptions      = &binding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions    = attributes.data()
        };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eLineList };

        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

        // lineWidth 1: wider lines need the optional wideLines feature, which isn't enabled.
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

        // Set 0 only (view/proj from FrameUniforms), no push constants.
        vk::DescriptorSetLayout setLayout = *m_descriptorSetLayout;
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = 1, .pSetLayouts = &setLayout };
        m_debugLineLayout = vk::raii::PipelineLayout(context.GetDevice(), pipelineLayoutInfo);

        auto swapchainFormat = swapchain.GetSurfaceFormat().format;
        auto depthFormat     = swapchain.GetDepthFormat();

        // Two pipelines differing only in depth state: depth-tested lines hide behind geometry
        // (LESS_OR_EQUAL so a line lying exactly on a surface still shows), on-top lines ignore
        // depth. Neither writes depth, so debug lines never occlude anything.
        auto build = [&](bool depthTested) {
            vk::PipelineDepthStencilStateCreateInfo depthStencil{
                .depthTestEnable       = depthTested ? vk::True : vk::False,
                .depthWriteEnable      = vk::False,
                .depthCompareOp        = vk::CompareOp::eLessOrEqual,
                .depthBoundsTestEnable = vk::False,
                .stencilTestEnable     = vk::False
            };
            vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
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
                    .layout              = m_debugLineLayout,
                    .renderPass          = nullptr
                },
                {.colorAttachmentCount = 1, .pColorAttachmentFormats = &swapchainFormat, .depthAttachmentFormat = depthFormat }
            };
            return vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
        };
        m_debugLinePipeline      = build(true);
        m_debugLineOnTopPipeline = build(false);

    }   // VulkanPipeline::CreateDebugLinePipelines()

    void VulkanPipeline::CreateShadowPipeline(VulkanContext& context, vk::Format depthFormat) {

        vk::raii::ShaderModule shaderModule = CreateShaderModule(context, ReadFile("../../../Assets/Shaders/Shadow.spv"));

        // Vertex stage only: with no fragment shader the rasterizer still writes depth, which is
        // all a shadow map needs.
        vk::PipelineShaderStageCreateInfo stage{ .stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "shadowVert" };

        // Same vertex buffers as the main pass, but only location 0 (position) is read.
        auto binding    = Vertex::GetBindingDescription();
        auto attributes = Vertex::GetAttributeDescriptions();
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount   = 1,
            .pVertexBindingDescriptions      = &binding,
            .vertexAttributeDescriptionCount = 1,
            .pVertexAttributeDescriptions    = &attributes[0]
        };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

        // No culling: open meshes (planes, cards) must still cast. Slope-scaled bias pushes
        // steep surfaces back so they don't self-shadow; the shader adds a normal offset on top.
        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable        = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode             = vk::PolygonMode::eFill,
            .cullMode                = vk::CullModeFlagBits::eNone,
            .frontFace               = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable         = vk::True,
            .depthBiasConstantFactor = 1.0f,
            .depthBiasClamp          = 0.0f,
            .depthBiasSlopeFactor    = 1.75f,
            .lineWidth               = 1.0f
        };
        vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };
        vk::PipelineColorBlendStateCreateInfo  colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 0 };
        vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable       = vk::True,
            .depthWriteEnable      = vk::True,
            .depthCompareOp        = vk::CompareOp::eLessOrEqual,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable     = vk::False
        };

        vk::PushConstantRange push{ .stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = sizeof(uint32_t) };
        vk::DescriptorSetLayout setLayout = *m_descriptorSetLayout;
        vk::PipelineLayoutCreateInfo layoutInfo{
            .setLayoutCount = 1, .pSetLayouts = &setLayout,
            .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
        m_shadowLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);

        vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
            {
                .stageCount          = 1,
                .pStages             = &stage,
                .pVertexInputState   = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState      = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState   = &multisampling,
                .pDepthStencilState  = &depthStencil,
                .pColorBlendState    = &colorBlending,
                .pDynamicState       = &dynamicState,
                .layout              = m_shadowLayout,
                .renderPass          = nullptr
            },
            {.colorAttachmentCount = 0, .depthAttachmentFormat = depthFormat }
        };
        m_shadowPipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());

    }   // VulkanPipeline::CreateShadowPipeline()

    void VulkanPipeline::CreateOutlinePipelines(VulkanContext& context, VulkanSwapchain& swapchain) {

        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo      viewportState{ .viewportCount = 1, .scissorCount = 1 };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };
        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable        = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode             = vk::PolygonMode::eFill,
            .cullMode                = vk::CullModeFlagBits::eNone,
            .frontFace               = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable         = vk::False,
            .lineWidth               = 1.0f
        };
        vk::PipelineMultisampleStateCreateInfo  multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };
        vk::PipelineDepthStencilStateCreateInfo noDepth{ .depthTestEnable = vk::False, .depthWriteEnable = vk::False };

        // ---- Mask: the selected object's instances -> R8, no depth (shows through walls) ----
        {
            vk::raii::ShaderModule module = CreateShaderModule(context, ReadFile("../../../Assets/Shaders/OutlineMask.spv"));
            vk::PipelineShaderStageCreateInfo stages[] = {
                { .stage = vk::ShaderStageFlagBits::eVertex,   .module = module, .pName = "maskVert" },
                { .stage = vk::ShaderStageFlagBits::eFragment, .module = module, .pName = "maskFrag" }
            };
            auto binding    = Vertex::GetBindingDescription();
            auto attributes = Vertex::GetAttributeDescriptions();
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
                .vertexBindingDescriptionCount   = 1,
                .pVertexBindingDescriptions      = &binding,
                .vertexAttributeDescriptionCount = 1,
                .pVertexAttributeDescriptions    = &attributes[0]
            };
            vk::PipelineColorBlendAttachmentState blend{ .blendEnable = vk::False, .colorWriteMask = vk::ColorComponentFlagBits::eR };
            vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blend };

            vk::DescriptorSetLayout setLayout = *m_descriptorSetLayout;
            vk::PipelineLayoutCreateInfo layoutInfo{ .setLayoutCount = 1, .pSetLayouts = &setLayout };
            m_outlineMaskLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);

            const vk::Format maskFormat = kOutlineMaskFormat;
            vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
                {
                    .stageCount          = 2,
                    .pStages             = stages,
                    .pVertexInputState   = &vertexInputInfo,
                    .pInputAssemblyState = &inputAssembly,
                    .pViewportState      = &viewportState,
                    .pRasterizationState = &rasterizer,
                    .pMultisampleState   = &multisampling,
                    .pDepthStencilState  = &noDepth,
                    .pColorBlendState    = &colorBlending,
                    .pDynamicState       = &dynamicState,
                    .layout              = m_outlineMaskLayout,
                    .renderPass          = nullptr
                },
                {.colorAttachmentCount = 1, .pColorAttachmentFormats = &maskFormat }
            };
            m_outlineMaskPipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
        }

        // ---- Composite: fullscreen triangle, edge of the mask -> outline colour, blended ----
        {
            vk::DescriptorSetLayoutBinding maskBinding{
                .binding         = 0,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eFragment
            };
            vk::DescriptorSetLayoutCreateInfo setInfo{ .bindingCount = 1, .pBindings = &maskBinding };
            m_outlineDescriptorSetLayout = vk::raii::DescriptorSetLayout(context.GetDevice(), setInfo);

            vk::raii::ShaderModule module = CreateShaderModule(context, ReadFile("../../../Assets/Shaders/OutlineComposite.spv"));
            vk::PipelineShaderStageCreateInfo stages[] = {
                { .stage = vk::ShaderStageFlagBits::eVertex,   .module = module, .pName = "compositeVert" },
                { .stage = vk::ShaderStageFlagBits::eFragment, .module = module, .pName = "compositeFrag" }
            };
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};   // fullscreen triangle from the vertex index

            // Straight alpha blending over the finished scene.
            vk::PipelineColorBlendAttachmentState blend{
                .blendEnable         = vk::True,
                .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
                .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
                .colorBlendOp        = vk::BlendOp::eAdd,
                .srcAlphaBlendFactor = vk::BlendFactor::eZero,
                .dstAlphaBlendFactor = vk::BlendFactor::eOne,
                .alphaBlendOp        = vk::BlendOp::eAdd,
                .colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
            };
            vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blend };

            vk::PushConstantRange push{ .stageFlags = vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(OutlineCompositePush) };
            vk::DescriptorSetLayout setLayout = *m_outlineDescriptorSetLayout;
            vk::PipelineLayoutCreateInfo layoutInfo{
                .setLayoutCount = 1, .pSetLayouts = &setLayout,
                .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
            m_outlineCompositeLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);

            const vk::Format colorFormat = swapchain.GetSurfaceFormat().format;
            vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
                {
                    .stageCount          = 2,
                    .pStages             = stages,
                    .pVertexInputState   = &vertexInputInfo,
                    .pInputAssemblyState = &inputAssembly,
                    .pViewportState      = &viewportState,
                    .pRasterizationState = &rasterizer,
                    .pMultisampleState   = &multisampling,
                    .pDepthStencilState  = &noDepth,
                    .pColorBlendState    = &colorBlending,
                    .pDynamicState       = &dynamicState,
                    .layout              = m_outlineCompositeLayout,
                    .renderPass          = nullptr
                },
                {.colorAttachmentCount = 1, .pColorAttachmentFormats = &colorFormat }
            };
            m_outlineCompositePipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
        }

    }   // VulkanPipeline::CreateOutlinePipelines()

    void VulkanPipeline::CreateParticlePipeline(VulkanContext& context, VulkanSwapchain& swapchain) {

        vk::raii::ShaderModule shaderModule = CreateShaderModule(context, ReadFile("../../../Assets/Shaders/Particle.spv"));
        vk::PipelineShaderStageCreateInfo stages[] = {
            { .stage = vk::ShaderStageFlagBits::eVertex,   .module = shaderModule, .pName = "particleVert" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "particleFrag" }
        };

        // No vertex buffers: six vertices per particle are generated from the vertex index.
        vk::PipelineVertexInputStateCreateInfo   vertexInputInfo{};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
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

        // Premultiplied blending: result = src + dst * (1 - src.a). The shader writes alpha 0 for
        // additive particles (pure add) and the real alpha for alpha-blended ones.
        vk::PipelineColorBlendAttachmentState blend{
            .blendEnable         = vk::True,
            .srcColorBlendFactor = vk::BlendFactor::eOne,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .colorBlendOp        = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eZero,
            .dstAlphaBlendFactor = vk::BlendFactor::eOne,
            .alphaBlendOp        = vk::BlendOp::eAdd,
            .colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };
        vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blend };

        // Hidden behind meshes, but never hiding anything themselves (no depth write) - otherwise
        // a transparent quad would cut holes in the particles behind it.
        vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable       = vk::True,
            .depthWriteEnable      = vk::False,
            .depthCompareOp        = vk::CompareOp::eLessOrEqual,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable     = vk::False
        };

        vk::PushConstantRange push{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .offset = 0, .size = sizeof(ParticlePush) };
        std::array<vk::DescriptorSetLayout, 2> setLayouts{ *m_descriptorSetLayout, *m_skyboxDescriptorSetLayout };
        vk::PipelineLayoutCreateInfo layoutInfo{
            .setLayoutCount = static_cast<uint32_t>(setLayouts.size()), .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
        m_particleLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);

        const vk::Format colorFormat = kSceneColorFormat;
        const vk::Format depthFormat = swapchain.GetDepthFormat();
        vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
            {
                .stageCount          = 2,
                .pStages             = stages,
                .pVertexInputState   = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState      = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState   = &multisampling,
                .pDepthStencilState  = &depthStencil,
                .pColorBlendState    = &colorBlending,
                .pDynamicState       = &dynamicState,
                .layout              = m_particleLayout,
                .renderPass          = nullptr
            },
            {.colorAttachmentCount = 1, .pColorAttachmentFormats = &colorFormat, .depthAttachmentFormat = depthFormat }
        };
        m_particlePipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());

    }   // VulkanPipeline::CreateParticlePipeline()

    void VulkanPipeline::CreatePostPipelines(VulkanContext& context, VulkanSwapchain& swapchain) {

        vk::raii::ShaderModule shaderModule = CreateShaderModule(context, ReadFile("../../../Assets/Shaders/Post.spv"));

        // Tonemap's set: binding 0 = the HDR scene, binding 1 = the finished bloom.
        std::array<vk::DescriptorSetLayoutBinding, 2> tonemapBindings{ {
            { .binding = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment },
            { .binding = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment }
        } };
        vk::DescriptorSetLayoutCreateInfo tonemapSetInfo{
            .bindingCount = static_cast<uint32_t>(tonemapBindings.size()), .pBindings = tonemapBindings.data() };
        m_tonemapDescriptorSetLayout = vk::raii::DescriptorSetLayout(context.GetDevice(), tonemapSetInfo);

        vk::PushConstantRange push{ .stageFlags = vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(PostPush) };
        {
            vk::DescriptorSetLayout setLayout = *m_skyboxDescriptorSetLayout;   // the single-texture layout
            vk::PipelineLayoutCreateInfo layoutInfo{
                .setLayoutCount = 1, .pSetLayouts = &setLayout,
                .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
            m_bloomLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);
        }
        {
            vk::DescriptorSetLayout setLayout = *m_tonemapDescriptorSetLayout;
            vk::PipelineLayoutCreateInfo layoutInfo{
                .setLayoutCount = 1, .pSetLayouts = &setLayout,
                .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
            m_tonemapLayout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);
        }

        // State shared by all three: a fullscreen triangle, nothing culled, no depth test.
        vk::PipelineVertexInputStateCreateInfo   vertexInputInfo{};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };
        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable        = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode             = vk::PolygonMode::eFill,
            .cullMode                = vk::CullModeFlagBits::eNone,
            .frontFace               = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable         = vk::False,
            .lineWidth               = 1.0f
        };
        vk::PipelineMultisampleStateCreateInfo  multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };
        vk::PipelineDepthStencilStateCreateInfo noDepth{ .depthTestEnable = vk::False, .depthWriteEnable = vk::False };

        const vk::ColorComponentFlags rgba = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        auto build = [&](const char* fragEntry, bool additive, const vk::raii::PipelineLayout& layout,
                         vk::Format colorFormat, vk::Format depthFormat) {
            vk::PipelineShaderStageCreateInfo stages[] = {
                { .stage = vk::ShaderStageFlagBits::eVertex,   .module = shaderModule, .pName = "postVert" },
                { .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = fragEntry }
            };
            vk::PipelineColorBlendAttachmentState blend{
                .blendEnable         = additive ? vk::True : vk::False,
                .srcColorBlendFactor = vk::BlendFactor::eOne,
                .dstColorBlendFactor = vk::BlendFactor::eOne,
                .colorBlendOp        = vk::BlendOp::eAdd,
                .srcAlphaBlendFactor = vk::BlendFactor::eOne,
                .dstAlphaBlendFactor = vk::BlendFactor::eZero,
                .alphaBlendOp        = vk::BlendOp::eAdd,
                .colorWriteMask      = rgba
            };
            vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blend };
            vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
                {
                    .stageCount          = 2,
                    .pStages             = stages,
                    .pVertexInputState   = &vertexInputInfo,
                    .pInputAssemblyState = &inputAssembly,
                    .pViewportState      = &viewportState,
                    .pRasterizationState = &rasterizer,
                    .pMultisampleState   = &multisampling,
                    .pDepthStencilState  = &noDepth,
                    .pColorBlendState    = &colorBlending,
                    .pDynamicState       = &dynamicState,
                    .layout              = *layout,
                    .renderPass          = nullptr
                },
                {.colorAttachmentCount = 1, .pColorAttachmentFormats = &colorFormat, .depthAttachmentFormat = depthFormat }
            };
            return vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
        };

        m_bloomDownPipeline = build("bloomDownFrag", false, m_bloomLayout, kSceneColorFormat, vk::Format::eUndefined);
        m_bloomUpPipeline   = build("bloomUpFrag",   true,  m_bloomLayout, kSceneColorFormat, vk::Format::eUndefined);
        // The tonemap pass keeps the scene's depth attachment bound (unused here) so the debug
        // lines drawn right after it, in the same pass, can still be depth-tested.
        m_tonemapPipeline   = build("tonemapFrag",   false, m_tonemapLayout, swapchain.GetSurfaceFormat().format, swapchain.GetDepthFormat());

    }   // VulkanPipeline::CreatePostPipelines()

    void VulkanPipeline::CleanUp() {

    }   // VulkanPipeline::CleanUp()

    std::vector<char> VulkanPipeline::ReadFile(std::string const& filename) {

        std::ifstream ifs(filename, std::ios::ate | std::ios::binary);
        if (!ifs.is_open()) {
            throw std::runtime_error("[Renderer | Pipeline] Failed to open file: " + filename);
        }

        std::vector<char> buffer(ifs.tellg());
        ifs.seekg(0, std::ios::beg);
        ifs.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

        ifs.close();
        return buffer;

    }   // VulkanPipeline::ReadFile()

    [[nodiscard]] vk::raii::ShaderModule VulkanPipeline::CreateShaderModule(VulkanContext& context, std::vector<char> const& code) {

        vk::ShaderModuleCreateInfo createInfo{
            .codeSize = code.size() * sizeof(char),
            .pCode = reinterpret_cast<const uint32_t*>(code.data())
        };
        return { context.GetDevice(), createInfo };

    }   // VulkanPipeline::CreateShaderModule()

}   // NAMESPACE CRUCIBLE
