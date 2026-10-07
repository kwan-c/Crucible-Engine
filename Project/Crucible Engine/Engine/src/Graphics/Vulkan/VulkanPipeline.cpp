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
    
    const vk::raii::Pipeline&               VulkanPipeline::GetPipeline(uint32_t samples)         const { return m_graphicsPipelines[MsaaIndex(samples)]; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetLayout()                 const { return m_pipelineLayout; }
    const vk::raii::DescriptorSetLayout&    VulkanPipeline::GetDescriptorSetLayout()    const { return m_descriptorSetLayout; }
    const vk::raii::DescriptorSetLayout&    VulkanPipeline::GetMaterialDescriptorSetLayout() const { return m_materialDescriptorSetLayout; }
    const vk::raii::Pipeline&               VulkanPipeline::GetSkyboxPipeline(uint32_t samples)   const { return m_skyboxPipelines[MsaaIndex(samples)]; }
    const vk::raii::PipelineLayout&         VulkanPipeline::GetSkyboxLayout()           const { return m_skyboxPipelineLayout; }
    const vk::raii::DescriptorSetLayout&    VulkanPipeline::GetSkyboxDescriptorSetLayout() const { return m_skyboxDescriptorSetLayout; }
    const vk::raii::DescriptorSetLayout&    VulkanPipeline::GetTonemapDescriptorSetLayout() const { return m_tonemapDescriptorSetLayout; }

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN PIPELINE
    // ------------------------------------------------------------------------------------------------------------- //

    void VulkanPipeline::CreateDescriptorSetLayout(VulkanContext& context) {

        std::array<vk::DescriptorSetLayoutBinding, 10> bindings{ {
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
            },
            {
                // The sky as a light source: the current skybox's pre-blurred environment map
                // (SamplerCube envMap). Re-pointed each frame (Renderer::WriteViewTextures).
                .binding            = 8,
                .descriptorType     = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount    = 1,
                .stageFlags         = vk::ShaderStageFlagBits::eFragment
            },
            {
                // This view's ambient occlusion image (Sampler2D aoTex). Per view.
                .binding            = 9,
                .descriptorType     = vk::DescriptorType::eCombinedImageSampler,
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
        // One pipeline per MSAA level (1, 2, 4 samples per pixel): the sample count is baked into a
        // pipeline and must match the target it draws into. `multisampling` is referenced by
        // pointer above, so changing it here changes what the next pipeline is built with.
        for (uint32_t i = 0; i < kMsaaLevels; ++i) {
            multisampling.rasterizationSamples = static_cast<vk::SampleCountFlagBits>(1u << i);
            m_graphicsPipelines[i] = vk::raii::Pipeline(context.GetDevice(), nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
        }


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
        // One pipeline per MSAA level (1, 2, 4 samples per pixel): the sample count is baked into a
        // pipeline and must match the target it draws into. `multisampling` is referenced by
        // pointer above, so changing it here changes what the next pipeline is built with.
        for (uint32_t i = 0; i < kMsaaLevels; ++i) {
            multisampling.rasterizationSamples = static_cast<vk::SampleCountFlagBits>(1u << i);
            m_skyboxPipelines[i] = vk::raii::Pipeline(context.GetDevice(), nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
        }

    }   // VulkanPipeline::CreateSkyboxPipeline()

    void VulkanPipeline::CreateTwoTextureSetLayout(VulkanContext& context) {

        // Two sampled textures. The tonemap pass reads the HDR scene (0) and the finished bloom (1)
        // through it; ambient occlusion reads depth and normals, then occlusion and depth.
        std::array<vk::DescriptorSetLayoutBinding, 2> tonemapBindings{ {
            { .binding = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment },
            { .binding = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment }
        } };
        vk::DescriptorSetLayoutCreateInfo tonemapSetInfo{
            .bindingCount = static_cast<uint32_t>(tonemapBindings.size()), .pBindings = tonemapBindings.data() };
        m_tonemapDescriptorSetLayout = vk::raii::DescriptorSetLayout(context.GetDevice(), tonemapSetInfo);

    }   // VulkanPipeline::CreateTwoTextureSetLayout()

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
