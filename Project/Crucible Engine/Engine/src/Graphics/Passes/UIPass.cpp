/*************************************************************************
  \file       UIPass.cpp
  \brief
    See UIPass.h.
*************************************************************************/
#include "Graphics/Passes/UIPass.h"

#include "Graphics/RenderResources.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Diagnostics/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Crucible {

    namespace {

        // Per draw command. Must match UIPush in UI.slang, 64 bytes (the text fields feed TextShading.slang).
        struct UIPush {
            glm::vec4 transform;       // xy = scale, zw = offset: canvas pixels -> target pixels
            glm::vec2 invTargetSize;   // 1 / target size in pixels
            uint32_t  mode;            // UIDrawMode
            float     pixelRange;      // UIDrawCommand::pixelRange (distance-field text)
            // UITextEffects - every field 4 bytes, so the C++ and Slang layouts can't disagree.
            uint32_t  outlineColour;
            uint32_t  glowColour;
            float     outlineWidth;
            float     glowWidth;
            float     glowSoftness;
            float     dilate;
            float     shadowSoftness;
            uint32_t  flags;           // bit 0: shadow pass
        };
        static_assert(sizeof(UIPush) == 64, "UIPush must match UI.slang.");

    }   // namespace

    void UIPass::Create(vk::DescriptorSetLayout textureSetLayout, vk::Format colourFormat, vk::Format depthFormat, uint32_t framesInFlight) {

        m_textureSetLayout = textureSetLayout;

        m_vertexBuffers.resize(framesInFlight);
        m_vertexMapped.assign(framesInFlight, nullptr);
        m_vertexCapacity.assign(framesInFlight, 0);
        m_indexBuffers.resize(framesInFlight);
        m_indexMapped.assign(framesInFlight, nullptr);
        m_indexCapacity.assign(framesInFlight, 0);

        // Clamped, so an atlas cell never picks up its neighbour across the texture's edge, and
        // trilinear over whatever mips a texture has (images from files have them; font atlases
        // made with CreateTextureFromPixels usually don't).
        vk::SamplerCreateInfo samplerInfo{
            .magFilter        = vk::Filter::eLinear,
            .minFilter        = vk::Filter::eLinear,
            .mipmapMode       = vk::SamplerMipmapMode::eLinear,
            .addressModeU     = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV     = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW     = vk::SamplerAddressMode::eClampToEdge,
            .mipLodBias       = 0.0f,
            .anisotropyEnable = vk::False,
            .maxAnisotropy    = 1.0f,
            .compareEnable    = vk::False,
            .minLod           = 0.0f,
            .maxLod           = VK_LOD_CLAMP_NONE,
            .borderColor      = vk::BorderColor::eFloatTransparentBlack
        };
        m_sampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

        vk::DescriptorPoolSize poolSize{ .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = m_maxTextureSets };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,   // vk::raii::DescriptorSet frees itself
            .maxSets = m_maxTextureSets, .poolSizeCount = 1, .pPoolSizes = &poolSize };
        m_texturePool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

        // ---- Pipeline ----
        vk::raii::ShaderModule shaderModule = LoadShaderModule(m_context, "UI.spv");

        vk::PipelineShaderStageCreateInfo shaderStages[] = {
            { .stage = vk::ShaderStageFlagBits::eVertex,   .module = *shaderModule, .pName = "uiVert" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = *shaderModule, .pName = "uiFrag" }
        };

        // UIVertex (UITypes.h): vec2 position, vec2 uv, RGBA8 colour - 20 bytes. The colour is read
        // as UNORM, so the shader gets 0..1 sRGB values and converts them itself.
        vk::VertexInputBindingDescription binding{ .binding = 0, .stride = sizeof(UIVertex), .inputRate = vk::VertexInputRate::eVertex };
        std::array<vk::VertexInputAttributeDescription, 3> attributes{ {
            { .location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat,  .offset = offsetof(UIVertex, position) },
            { .location = 1, .binding = 0, .format = vk::Format::eR32G32Sfloat,  .offset = offsetof(UIVertex, uv) },
            { .location = 2, .binding = 0, .format = vk::Format::eR8G8B8A8Unorm, .offset = offsetof(UIVertex, colour) }
        } };
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount   = 1,
            .pVertexBindingDescriptions      = &binding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions    = attributes.data()
        };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

        std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

        // No culling: UI code is free to wind its quads either way.
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

        // UI ignores the scene's depth entirely.
        vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable       = vk::False,
            .depthWriteEnable      = vk::False,
            .depthCompareOp        = vk::CompareOp::eAlways,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable     = vk::False
        };

        // Straight alpha: colour = src * a + dst * (1 - a). Alpha accumulates coverage, so a UI
        // viewport cleared to transparent ends up with an alpha the editor can composite with.
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable         = vk::True,
            .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .colorBlendOp        = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .alphaBlendOp        = vk::BlendOp::eAdd,
            .colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };
        vk::PipelineColorBlendStateCreateInfo colorBlending{
            .logicOpEnable   = vk::False,
            .logicOp         = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments    = &colorBlendAttachment
        };

        // Set 0 = the texture; one push constant block per draw command.
        vk::PushConstantRange pushRange{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .offset = 0, .size = sizeof(UIPush) };
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .setLayoutCount = 1, .pSetLayouts = &m_textureSetLayout,
            .pushConstantRangeCount = 1, .pPushConstantRanges = &pushRange };
        m_layout = vk::raii::PipelineLayout(m_context.GetDevice(), pipelineLayoutInfo);

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
                .layout              = *m_layout,
                .renderPass          = nullptr
            },
            { .colorAttachmentCount = 1, .pColorAttachmentFormats = &colourFormat, .depthAttachmentFormat = depthFormat }
        };
        m_pipeline = vk::raii::Pipeline(m_context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());

    }   // UIPass::Create()

    // ------------------------------------------------------------------------------------------------------------- //
    //  SUBMISSION
    // ------------------------------------------------------------------------------------------------------------- //

    void UIPass::Submit(uint32_t target, const UIDrawList& list) {

        if (list.commands.empty() || list.indices.empty() || list.vertices.empty()) { return; }

        // A frame's worth of UI is a few thousand vertices; past these caps something is looping.
        if (m_vertices.size() + list.vertices.size() > m_maxVertices || m_indices.size() + list.indices.size() > m_maxIndices) {
            if (!m_warnedOverflow) {
                CRU_WARN("[Renderer] More than {} UI vertices or {} UI indices in one frame; the rest of the UI is dropped.", m_maxVertices, m_maxIndices);
                m_warnedOverflow = true;
            }
            return;
        }

        const uint32_t baseVertex = static_cast<uint32_t>(m_vertices.size());
        const uint32_t baseIndex  = static_cast<uint32_t>(m_indices.size());
        const int64_t  vertexCount = static_cast<int64_t>(list.vertices.size());

        Submission submission{ .target = target, .firstCommand = static_cast<uint32_t>(m_commands.size()), .commandCount = 0 };
        for (const UIDrawCommand& in : list.commands) {
            const uint32_t count = in.indexCount - in.indexCount % 3;   // whole triangles only
            bool ok = count > 0 && static_cast<uint64_t>(in.firstIndex) + count <= list.indices.size();
            // Every index must land inside this list's vertices: the GPU would otherwise read
            // another list's vertices, or past the end of the buffer.
            for (uint32_t i = 0; ok && i < count; ++i) {
                const int64_t v = static_cast<int64_t>(list.indices[in.firstIndex + i]) + in.vertexOffset;
                ok = v >= 0 && v < vertexCount;
            }
            if (!ok) {
                if (count > 0 && !m_warnedBadCommand) {
                    CRU_WARN("[Renderer] A UI draw command's indices are out of range of its list; it was dropped (warned once).");
                    m_warnedBadCommand = true;
                }
                continue;
            }
            m_commands.push_back({ .texture = in.texture, .mode = in.mode, .clipRect = in.clipRect,
                                   .firstIndex = baseIndex + in.firstIndex, .indexCount = count,
                                   .vertexOffset = static_cast<int32_t>(baseVertex) + in.vertexOffset,
                                   .pixelRange = in.pixelRange, .effects = in.effects });
            ++submission.commandCount;
        }
        if (submission.commandCount == 0) { return; }

        m_vertices.insert(m_vertices.end(), list.vertices.begin(), list.vertices.end());
        m_indices.insert(m_indices.end(), list.indices.begin(), list.indices.end());
        m_submissions.push_back(submission);

    }   // UIPass::Submit()

    void UIPass::ClearSubmitted() {

        m_vertices.clear();
        m_indices.clear();
        m_commands.clear();
        m_submissions.clear();

    }   // UIPass::ClearSubmitted()

    bool UIPass::HasContent(uint32_t target) const {

        return std::any_of(m_submissions.begin(), m_submissions.end(), [target](const Submission& s) { return s.target == target; });

    }   // UIPass::HasContent()

    // ------------------------------------------------------------------------------------------------------------- //
    //  UPLOAD
    // ------------------------------------------------------------------------------------------------------------- //

    void UIPass::Upload(uint32_t frameIndex) {

        if (m_submissions.empty() || frameIndex >= m_vertexBuffers.size()) { return; }

        // Grow this frame's buffers (powers of two). Safe to replace here: BeginFrame() already
        // waited on this frame index's fence, so the GPU is done with the old ones.
        const auto grow = [&](VulkanBuffer& buffer, void*& mapped, uint32_t& capacity, size_t needed, uint32_t minimum,
                              vk::DeviceSize elementSize, vk::BufferUsageFlags usage) {
            if (capacity >= needed) { return; }
            uint32_t newCapacity = minimum;
            while (newCapacity < needed) { newCapacity *= 2; }
            buffer.Create(m_context, vk::DeviceSize{ newCapacity } * elementSize, usage,
                          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            mapped   = buffer.Map();
            capacity = newCapacity;
        };
        grow(m_vertexBuffers[frameIndex], m_vertexMapped[frameIndex], m_vertexCapacity[frameIndex], m_vertices.size(), 4096,
             sizeof(UIVertex), vk::BufferUsageFlagBits::eVertexBuffer);
        grow(m_indexBuffers[frameIndex], m_indexMapped[frameIndex], m_indexCapacity[frameIndex], m_indices.size(), 8192,
             sizeof(uint32_t), vk::BufferUsageFlagBits::eIndexBuffer);

        memcpy(m_vertexMapped[frameIndex], m_vertices.data(), m_vertices.size() * sizeof(UIVertex));
        memcpy(m_indexMapped[frameIndex], m_indices.data(), m_indices.size() * sizeof(uint32_t));

        // Made here rather than in Record(), so recording never allocates.
        for (Command& command : m_commands) { command.set = EnsureTextureSet(command.texture); }

    }   // UIPass::Upload()

    vk::DescriptorSet UIPass::EnsureTextureSet(TextureHandle texture) {

        if (!texture.IsValid() || texture.id >= m_resources.Textures().size()) { texture = m_resources.DefaultWhiteTexture(); }

        if (auto it = m_textureSets.find(texture.id); it != m_textureSets.end()) { return *it->second; }

        // Pool full: fall back to the white texture's set (made first, below, so it always fits).
        if (m_textureSets.size() >= m_maxTextureSets - 1 && texture.id != m_resources.DefaultWhiteTexture().id) {
            if (!m_warnedTextureSets) {
                CRU_WARN("[Renderer] The UI used more than {} different textures; the rest are drawn white.", m_maxTextureSets - 1);
                m_warnedTextureSets = true;
            }
            return EnsureTextureSet(m_resources.DefaultWhiteTexture());
        }

        vk::DescriptorSetLayout layout = m_textureSetLayout;
        vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = *m_texturePool, .descriptorSetCount = 1, .pSetLayouts = &layout };
        vk::raii::DescriptorSet set = std::move(m_context.GetDevice().allocateDescriptorSets(allocInfo).front());

        vk::DescriptorImageInfo imageInfo{
            .sampler = *m_sampler, .imageView = *m_resources.Textures()[texture.id].view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::WriteDescriptorSet write{
            .dstSet = *set, .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageInfo };
        m_context.GetDevice().updateDescriptorSets(write, {});

        return *m_textureSets.emplace(texture.id, std::move(set)).first->second;

    }   // UIPass::EnsureTextureSet()

    // ------------------------------------------------------------------------------------------------------------- //
    //  DRAWING
    // ------------------------------------------------------------------------------------------------------------- //

    void UIPass::Record(vk::CommandBuffer cmd, uint32_t frameIndex, vk::Extent2D extent, const glm::vec4& transform, uint32_t target) const {

        if (extent.width == 0 || extent.height == 0 || !HasContent(target)) { return; }
        if (frameIndex >= m_vertexBuffers.size() || m_vertexCapacity[frameIndex] < m_vertices.size()) { return; }   // Upload() didn't run

        const float width  = static_cast<float>(extent.width);
        const float height = static_cast<float>(extent.height);

        // Plain viewport (+y down), unlike the scene's flipped one: canvas pixels already point down.
        cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, width, height, 0.0f, 1.0f));
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
        cmd.bindVertexBuffers(0, *m_vertexBuffers[frameIndex].GetBuffer(), { vk::DeviceSize{ 0 } });
        cmd.bindIndexBuffer(*m_indexBuffers[frameIndex].GetBuffer(), 0, vk::IndexType::eUint32);

        UIPush push{};
        push.transform     = transform;
        push.invTargetSize = glm::vec2(1.0f / width, 1.0f / height);
        vk::DescriptorSet boundSet{};

        for (const Submission& submission : m_submissions) {
            if (submission.target != target) { continue; }
            for (uint32_t c = 0; c < submission.commandCount; ++c) {
                const Command& command = m_commands[submission.firstCommand + c];

                // The clip rectangle goes through the same scale and offset as the vertices, then is
                // cut to the image (a scissor may not start off its edge).
                const float x0 = std::clamp(std::floor(command.clipRect.x * transform.x + transform.z), 0.0f, width);
                const float y0 = std::clamp(std::floor(command.clipRect.y * transform.y + transform.w), 0.0f, height);
                const float x1 = std::clamp(std::ceil(command.clipRect.z * transform.x + transform.z), 0.0f, width);
                const float y1 = std::clamp(std::ceil(command.clipRect.w * transform.y + transform.w), 0.0f, height);
                if (x1 <= x0 || y1 <= y0) { continue; }   // clipped away entirely
                cmd.setScissor(0, vk::Rect2D(vk::Offset2D(static_cast<int32_t>(x0), static_cast<int32_t>(y0)),
                                             vk::Extent2D(static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0))));

                if (command.set != boundSet) {
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_layout, 0, command.set, nullptr);
                    boundSet = command.set;
                }
                push.mode = static_cast<uint32_t>(command.mode);
                push.pixelRange = command.pixelRange;
                const UITextEffects& fx = command.effects;
                push.outlineColour  = fx.outlineColour;
                push.glowColour     = fx.glowColour;
                push.outlineWidth   = fx.outlineWidth;
                push.glowWidth      = fx.glowWidth;
                push.glowSoftness   = fx.glowSoftness;
                push.dilate         = fx.dilate;
                push.shadowSoftness = fx.shadowSoftness;
                push.flags          = fx.shadowPass ? 1u : 0u;
                cmd.pushConstants<UIPush>(*m_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);

                cmd.drawIndexed(command.indexCount, 1, command.firstIndex, command.vertexOffset, 0);
            }
        }

    }   // UIPass::Record()

}   // NAMESPACE CRUCIBLE
