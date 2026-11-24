/*************************************************************************
  \file       WaterPass.cpp
  \brief
    See WaterPass.h.
*************************************************************************/
#include "Graphics/Passes/WaterPass.h"

#include "Graphics/RenderResources.h"
#include "Graphics/Camera.h"
#include "Graphics/Vulkan/RenderLayouts.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Spatial/Bounds.h"

#include <algorithm>
#include <glm/gtc/packing.hpp>   // packHalf2x16 - the colours in WaterPush

namespace Crucible {

    static_assert(RenderLayouts::kMsaaLevels == 3, "WaterPass keeps one pipeline per MSAA level");

    void WaterPass::Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout sceneCopySetLayout,
                           vk::Format colourFormat, vk::Format depthFormat) {

        vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "Water.spv");
        vk::PipelineShaderStageCreateInfo shaderStages[] = {
            { .stage = vk::ShaderStageFlagBits::eVertex,   .module = shaderModule, .pName = "waterVert" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "waterFrag" }
        };

        // Ordinary meshes (the built-in Plane, or the entity's own mesh). Only the attributes
        // waterVert reads - position, normal, tangent - so validation has no unread inputs to
        // warn about.
        auto bindingDescription = Vertex::GetBindingDescription();
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
        for (const vk::VertexInputAttributeDescription& attribute : Vertex::GetAttributeDescriptions()) {
            if (attribute.location == 0 || attribute.location == 1 || attribute.location == 4) { attributeDescriptions.push_back(attribute); }
        }
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount   = 1,
            .pVertexBindingDescriptions      = &bindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions    = attributeDescriptions.data()
        };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

        // Seen from above only: from underneath, the surface is culled (no underwater look yet).
        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable        = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode             = vk::PolygonMode::eFill,
            .cullMode                = vk::CullModeFlagBits::eBack,
            .frontFace               = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable         = vk::False,
            .lineWidth               = 1.0f
        };
        vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };

        // No blending: the shader has already mixed in the bottom (from the scene copy). Both
        // outputs written as they are - the same state for both, as independentBlend is not enabled.
        const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable    = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA };
        const vk::PipelineColorBlendAttachmentState colorBlendAttachments[2] = { colorBlendAttachment, colorBlendAttachment };
        vk::PipelineColorBlendStateCreateInfo colorBlending{
            .logicOpEnable   = vk::False,
            .logicOp         = vk::LogicOp::eCopy,
            .attachmentCount = 2,
            .pAttachments    = colorBlendAttachments
        };

        // The surface hides what is under it from everything drawn after it (particles).
        vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable       = vk::True,
            .depthWriteEnable      = vk::True,
            .depthCompareOp        = vk::CompareOp::eLess,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable     = vk::False
        };

        // Set 0 = per-view frame data, set 1 = the scene copies; WaterPush for both stages (the
        // vertex shader reads the transform, the fragment shader the look).
        vk::PushConstantRange pushRange{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .offset     = 0,
            .size       = sizeof(WaterPush)
        };
        std::array<vk::DescriptorSetLayout, 2> setLayouts{ frameSetLayout, sceneCopySetLayout };
        vk::PipelineLayoutCreateInfo layoutInfo{
            .setLayoutCount         = static_cast<uint32_t>(setLayouts.size()),
            .pSetLayouts            = setLayouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &pushRange
        };
        m_layout = vk::raii::PipelineLayout(context.GetDevice(), layoutInfo);

        const vk::Format colourFormats[2] = { colourFormat, RenderLayouts::kLineDataFormat };
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
                .layout              = m_layout,
                .renderPass          = nullptr
            },
            {.colorAttachmentCount = 2, .pColorAttachmentFormats = colourFormats, .depthAttachmentFormat = depthFormat }
        };
        // One pipeline per MSAA level: `multisampling` is referenced by pointer above.
        for (uint32_t i = 0; i < RenderLayouts::kMsaaLevels; ++i) {
            multisampling.rasterizationSamples = static_cast<vk::SampleCountFlagBits>(1u << i);
            m_pipelines[i] = vk::raii::Pipeline(context.GetDevice(), context.GetPipelineCache(), pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
        }

    }   // WaterPass::Create()

    void WaterPass::Submit(MeshHandle mesh, const glm::mat4& model, const WaterSettings& s, bool sceneHidden) {

        if (!mesh.IsValid()) { return; }

        // Colours are picked in sRGB; the shader works in linear light (same curve as the toon tints).
        const auto toLinear = [](const glm::vec3& srgb) { return glm::pow(glm::clamp(srgb, glm::vec3(0.0f), glm::vec3(1.0f)), glm::vec3(2.2f)); };

        Submitted item;
        item.sceneHidden = sceneHidden;
        item.model     = model;
        item.draw.mesh = mesh;
        WaterPush& p   = item.draw.push;
        // glm is column-major: row r of the transform is (m[0][r], m[1][r], m[2][r], m[3][r]).
        p.model0  = glm::vec4(model[0][0], model[1][0], model[2][0], model[3][0]);
        p.model1  = glm::vec4(model[0][1], model[1][1], model[2][1], model[3][1]);
        p.model2  = glm::vec4(model[0][2], model[1][2], model[2][2], model[3][2]);

        // Six colours as half floats, two to a uint (WaterPush's order). The glint colour carries
        // the glint strength, so the shader needs no separate slot for it.
        const glm::vec3 shallow     = toLinear(s.shallowColour);
        const glm::vec3 deep        = toLinear(s.deepColour);
        const glm::vec3 foam        = toLinear(s.foamColour);
        const glm::vec3 reflectTint = toLinear(s.reflectionColour);
        const glm::vec3 line        = toLinear(s.rippleLineColour);
        const glm::vec3 glint       = toLinear(s.glintColour) * std::max(s.glints, 0.0f);
        const auto halves = [](float low, float high) { return glm::packHalf2x16(glm::vec2(low, high)); };
        p.coloursA = glm::uvec4(halves(shallow.r, shallow.g), halves(shallow.b, deep.r), halves(deep.g, deep.b), halves(foam.r, foam.g));
        p.coloursB = glm::uvec4(halves(foam.b, reflectTint.r), halves(reflectTint.g, reflectTint.b), halves(line.r, line.g), halves(line.b, glint.r));
        p.glintGB  = halves(glint.g, glint.b);

        p.water       = glm::vec4(std::max(s.deepDepth, 0.01f), std::max(s.clarity, 0.01f), glm::clamp(s.refraction, 0.0f, 1.0f), std::max(s.foamWidth, 0.0f));
        p.ripple      = glm::vec4(std::max(s.rippleSize, 0.05f), s.rippleSpeed, std::max(s.rippleStrength, 0.0f), glm::clamp(s.foamBreakup, 0.0f, 1.0f));
        p.reflection  = glm::clamp(s.reflection, 0.0f, 1.0f);
        p.rippleLines = glm::clamp(s.rippleLines, 0.0f, 1.0f);
        m_submitted.push_back(item);

    }   // WaterPass::Submit()

    void WaterPass::Prepare(const Camera& camera, std::vector<WaterDraw>& out, bool hideSceneHidden) const {

        out.clear();
        if (m_submitted.empty()) { return; }

        const Frustum frustum = Frustum::FromViewProjection(camera.GetProjection() * camera.GetView());
        for (const Submitted& item : m_submitted) {
            if (hideSceneHidden && item.sceneHidden) { continue; }   // hidden in the editor's Scene view
            glm::vec3 boundsMin, boundsMax;
            if (!m_resources.GetMeshBounds(item.draw.mesh, boundsMin, boundsMax)) { continue; }   // not uploaded yet
            if (!frustum.Intersects(AABB::Transform(AABB(boundsMin, boundsMax), item.model))) { continue; }
            out.push_back(item.draw);
        }

    }   // WaterPass::Prepare()

    void WaterPass::Record(vk::CommandBuffer cmd, uint32_t samples, vk::DescriptorSet frameSet, vk::DescriptorSet sceneCopySet,
                           const std::vector<WaterDraw>& draws) const {

        if (draws.empty() || !sceneCopySet) { return; }

        // Viewport and scissor are already set by the scene pass (dynamic state).
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipelines[RenderLayouts::MsaaIndex(samples)]);
        const std::array<vk::DescriptorSet, 2> sets{ frameSet, sceneCopySet };
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_layout, 0, sets, nullptr);

        for (const WaterDraw& draw : draws) {
            const uint32_t indexCount = m_resources.BindMesh(cmd, draw.mesh);
            if (indexCount == 0) { continue; }
            cmd.pushConstants<WaterPush>(*m_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, draw.push);
            cmd.drawIndexed(indexCount, 1, 0, 0, 0);
        }

    }   // WaterPass::Record()

}   // NAMESPACE CRUCIBLE
