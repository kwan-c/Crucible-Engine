/*************************************************************************
  \file       DebugLinePass.cpp
  \brief
    See DebugLinePass.h.
*************************************************************************/
#include "Graphics/Passes/DebugLinePass.h"

#include "Graphics/Vulkan/RenderLayouts.h"
#include "Graphics/Vulkan/VulkanCommands.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/DebugDraw.h"
#include "Diagnostics/Logger.h"

#include <algorithm>
#include <cstring>

namespace Crucible {

    void DebugLinePass::Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::Format colourFormat, vk::Format depthFormat) {


        vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "../../../Assets/Shaders/DebugLine.spv");

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
        vk::DescriptorSetLayout setLayout = frameSetLayout;
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = 1, .pSetLayouts = &setLayout };
        m_debugLineLayout = vk::raii::PipelineLayout(context.GetDevice(), pipelineLayoutInfo);

        auto swapchainFormat = colourFormat;

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

    }   // DebugLinePass::Create()

    void DebugLinePass::Update(VulkanContext& context, uint32_t frameIndex, uint32_t framesInFlight) {

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
            const uint32_t frames = framesInFlight;
            m_debugBuffers.resize(frames);
            m_debugBuffersMapped.assign(frames, nullptr);
            m_debugBufferCapacity.assign(frames, 0);
        }

        // Grow this frame's buffer (power of two, at least 8K vertices). Safe to recreate here:
        // BeginFrame() already waited on this frame index's fence, so the GPU is done with it.
        if (m_debugBufferCapacity[frameIndex] < total) {
            uint32_t capacity = 8192;
            while (capacity < total) { capacity *= 2; }
            m_debugBuffers[frameIndex].Create(context, vk::DeviceSize{ capacity } * sizeof(DebugVertex),
                vk::BufferUsageFlagBits::eVertexBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_debugBuffersMapped[frameIndex]  = m_debugBuffers[frameIndex].Map();
            m_debugBufferCapacity[frameIndex] = capacity;
        }

        auto* dst = static_cast<DebugVertex*>(m_debugBuffersMapped[frameIndex]);
        if (!m_debugTested.empty()) { memcpy(dst, m_debugTested.data(), m_debugTested.size() * sizeof(DebugVertex)); }
        if (!m_debugOnTop.empty())  { memcpy(dst + m_debugTested.size(), m_debugOnTop.data(), m_debugOnTop.size() * sizeof(DebugVertex)); }

    }   // DebugLinePass::Update()

    void DebugLinePass::Record(vk::CommandBuffer cmd, vk::Extent2D extent, vk::DescriptorSet frameSet, uint32_t frameIndex) const {

        if (!HasLines()) { return; }   // also: the buffers only exist once there has been something to draw
        const uint32_t   testedCount  = m_debugTestedCount;
        const uint32_t   onTopCount   = m_debugOnTopCount;
        const vk::Buffer vertexBuffer = *m_debugBuffers[frameIndex].GetBuffer();

        // The tonemap triangle drawn just before used a plain viewport; world geometry uses the
        // flipped one (+Y up), like the scene pass.
        cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(extent.height), static_cast<float>(extent.width), -static_cast<float>(extent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_debugLineLayout, 0, frameSet, nullptr);
        cmd.bindVertexBuffers(0, vertexBuffer, { 0 });

        if (testedCount > 0) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_debugLinePipeline);
            cmd.draw(testedCount, 1, 0, 0);
        }
        if (onTopCount > 0) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_debugLineOnTopPipeline);
            cmd.draw(onTopCount, 1, testedCount, 0);
        }

    }   // DebugLinePass::Record()

}   // NAMESPACE CRUCIBLE
