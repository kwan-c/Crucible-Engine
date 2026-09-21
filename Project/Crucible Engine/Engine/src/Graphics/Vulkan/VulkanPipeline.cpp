#include "VulkanPipeline.h"

#include <fstream>

namespace Crucible {

    // ------------------------------------------------------------------------------------------------------------- //
    // ACCESSORS
    // ------------------------------------------------------------------------------------------------------------- //
    
    const vk::raii::Pipeline&       VulkanPipeline::GetPipeline()   const { return m_graphicsPipeline; }
    const vk::raii::PipelineLayout& VulkanPipeline::GetLayout()     const { return m_pipelineLayout; }

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN PIPELINE
    // ------------------------------------------------------------------------------------------------------------- //

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
        vk::Viewport viewport{ 0.0f, 0.0f, static_cast<float>(swapchain.GetExtent().width), static_cast<float>(swapchain.GetExtent().height), 0.0f, 1.0f };
        vk::Rect2D scissor{ vk::Offset2D{0,0}, swapchain.GetExtent() };
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

        // Pipeline layout object (for uniform values).
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = 0, .pushConstantRangeCount = 0 };
        m_pipelineLayout = vk::raii::PipelineLayout(context.GetDevice(), pipelineLayoutInfo);

        auto swapchainFormat = swapchain.GetSurfaceFormat().format;

        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapchainFormat };

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
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = m_pipelineLayout,
                .renderPass = nullptr
            },
            {.colorAttachmentCount = 1, .pColorAttachmentFormats = &swapchainFormat }
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