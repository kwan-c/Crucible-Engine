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

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN PIPELINE
    // ------------------------------------------------------------------------------------------------------------- //

    void VulkanPipeline::CreateDescriptorSetLayout(VulkanContext& context) {

        vk::DescriptorSetLayoutBinding uboLayoutBinding{
            .binding            = 0, 
            .descriptorType     = vk::DescriptorType::eUniformBuffer, 
            .descriptorCount    = 1, 
            // Fragment needs this now too: FrameUniforms::cameraPosWS feeds the PBR specular
            // term's view vector (see DefaultShader2.slang's fragMain).
            .stageFlags         = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        };
        vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = 1, .pBindings = &uboLayoutBinding };
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

        // Pipeline layout object. Set 0 = per-frame camera UBO (view/proj). Set 1 = the current
        // draw's material (five texture bindings - see CreateMaterialDescriptorSetLayout). A
        // push constant carries the per-draw model matrix AND the material's scalar factors
        // (PushConstants, RenderTypes.h), readable from both stages since the model matrix is
        // vertex-only but the factors are fragment-only.
        vk::PushConstantRange pushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
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

        auto swapchainFormat = swapchain.GetSurfaceFormat().format;
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
