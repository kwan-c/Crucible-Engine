/*************************************************************************
  \file       WorldTextPass.cpp
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    See WorldTextPass.h.
*************************************************************************/
#include "Graphics/Passes/WorldTextPass.h"

#include "Graphics/Camera.h"
#include "Graphics/RenderResources.h"
#include "Graphics/Vulkan/RenderLayouts.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Spatial/Bounds.h"
#include "Diagnostics/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Crucible {

    static_assert(RenderLayouts::kMsaaLevels == 3, "WorldTextPass keeps one pipeline per MSAA level");

    void WorldTextPass::Create(vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout textureSetLayout,
                               vk::Format colourFormat, vk::Format depthFormat, uint32_t framesInFlight) {

        m_textureSetLayout = textureSetLayout;

        m_vertexBuffers.resize(framesInFlight);
        m_vertexMapped.assign(framesInFlight, nullptr);
        m_vertexCapacity.assign(framesInFlight, 0);

        // As the UI's: clamped, so an atlas cell never picks up its neighbour across the edge, and
        // trilinear over whatever mips the atlas has.
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

        // ---- Pipelines ----
        vk::raii::ShaderModule shaderModule = LoadShaderModule(m_context, "WorldText.spv");
        vk::PipelineShaderStageCreateInfo stages[] = {
            { .stage = vk::ShaderStageFlagBits::eVertex,   .module = *shaderModule, .pName = "worldTextVert" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = *shaderModule, .pName = "worldTextFrag" }
        };

        // WorldTextVertex: vec2 local, vec2 uv, RGBA8 colour - 20 bytes. The colour is read as
        // UNORM, so the shader gets 0..1 sRGB values and converts them itself.
        vk::VertexInputBindingDescription binding{ .binding = 0, .stride = sizeof(WorldTextVertex), .inputRate = vk::VertexInputRate::eVertex };
        std::array<vk::VertexInputAttributeDescription, 3> attributes{ {
            { .location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat,  .offset = offsetof(WorldTextVertex, local) },
            { .location = 1, .binding = 0, .format = vk::Format::eR32G32Sfloat,  .offset = offsetof(WorldTextVertex, uv) },
            { .location = 2, .binding = 0, .format = vk::Format::eR8G8B8A8Unorm, .offset = offsetof(WorldTextVertex, colour) }
        } };
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount   = 1,
            .pVertexBindingDescriptions      = &binding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions    = attributes.data()
        };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

        // Cull mode (doubleSided) and depth test (alwaysOnTop) change per draw: dynamic state,
        // core in Vulkan 1.3, so one pipeline per MSAA level covers every combination.
        std::array<vk::DynamicState, 4> dynamicStates = {
            vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eCullMode, vk::DynamicState::eDepthTestEnable };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

        // Quads are wound counter-clockwise seen from the text's front (+z of its plane), like the
        // meshes' front faces.
        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable        = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode             = vk::PolygonMode::eFill,
            .cullMode                = vk::CullModeFlagBits::eNone,   // dynamic
            .frontFace               = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable         = vk::False,
            .lineWidth               = 1.0f
        };
        vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };

        // Hidden behind meshes (unless alwaysOnTop), never hiding anything itself.
        vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable       = vk::True,    // dynamic
            .depthWriteEnable      = vk::False,
            .depthCompareOp        = vk::CompareOp::eLessOrEqual,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable     = vk::False
        };

        // Premultiplied, as particles. The scene pass's second output (toon line data) gets zero
        // with zero alpha from the shader, which this blend leaves unchanged.
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
        const vk::PipelineColorBlendAttachmentState blends[2] = { blend, blend };
        vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 2, .pAttachments = blends };

        vk::PushConstantRange push{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .offset = 0, .size = sizeof(WorldTextPush) };
        std::array<vk::DescriptorSetLayout, 2> setLayouts{ frameSetLayout, textureSetLayout };
        vk::PipelineLayoutCreateInfo layoutInfo{
            .setLayoutCount = static_cast<uint32_t>(setLayouts.size()), .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
        m_layout = vk::raii::PipelineLayout(m_context.GetDevice(), layoutInfo);

        const vk::Format colourFormats[2] = { colourFormat, RenderLayouts::kLineDataFormat };
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
                .layout              = *m_layout,
                .renderPass          = nullptr
            },
            { .colorAttachmentCount = 2, .pColorAttachmentFormats = colourFormats, .depthAttachmentFormat = depthFormat }
        };
        // One per MSAA level (1, 2, 4 samples): the sample count is baked into a pipeline.
        for (uint32_t i = 0; i < RenderLayouts::kMsaaLevels; ++i) {
            multisampling.rasterizationSamples = static_cast<vk::SampleCountFlagBits>(1u << i);
            m_pipelines[i] = vk::raii::Pipeline(m_context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
        }

    }   // WorldTextPass::Create()

    // ------------------------------------------------------------------------------------------------------------- //
    //  THIS FRAME'S TEXT: SUBMIT, SORT PER VIEW, UPLOAD
    // ------------------------------------------------------------------------------------------------------------- //

    void WorldTextPass::Submit(const WorldTextDesc& desc, std::span<const WorldTextVertex> vertices, std::span<const WorldTextRun> runs) {

        if (vertices.empty() || runs.empty()) { return; }
        if (m_vertices.size() + vertices.size() > m_maxVertices) {
            if (!m_warnedOverflow) {
                CRU_WARN("[Renderer] More than {} world text vertices in one frame; the rest of the text is dropped.", m_maxVertices);
                m_warnedOverflow = true;
            }
            return;
        }

        const uint32_t base = static_cast<uint32_t>(m_vertices.size());
        Text text{ .desc = desc, .firstRun = static_cast<uint32_t>(m_runs.size()), .runCount = 0 };
        for (const WorldTextRun& run : runs) {
            const uint32_t count = run.vertexCount - run.vertexCount % 3;   // whole triangles only
            if (count == 0 || static_cast<uint64_t>(run.firstVertex) + count > vertices.size()) { continue; }   // out of range: dropped
            WorldTextRun moved = run;
            moved.firstVertex = base + run.firstVertex;
            moved.vertexCount = count;
            m_runs.push_back(moved);
            ++text.runCount;
        }
        if (text.runCount == 0) { return; }
        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
        m_texts.push_back(text);

    }   // WorldTextPass::Submit()

    void WorldTextPass::ClearSubmitted() {

        m_vertices.clear();
        m_runs.clear();
        m_texts.clear();

    }   // WorldTextPass::ClearSubmitted()

    void WorldTextPass::Prepare(const Camera& camera, std::vector<WorldTextDraw>& out) {

        if (m_texts.empty()) { return; }

        const Frustum   frustum = Frustum::FromViewProjection(camera.GetProjection() * camera.GetView());
        const glm::vec3 eye     = camera.position;

        m_order.clear();
        for (uint32_t i = 0; i < m_texts.size(); ++i) {
            const WorldTextDesc& desc = m_texts[i].desc;
            const glm::vec3 reach(desc.radius);
            if (!frustum.Intersects(AABB(desc.anchor - reach, desc.anchor + reach))) { continue; }

            const float distance = glm::length(desc.anchor - eye);
            float opacity = 1.0f;
            if (desc.fadeEnd > desc.fadeStart) {
                opacity = std::clamp((desc.fadeEnd - distance) / (desc.fadeEnd - desc.fadeStart), 0.0f, 1.0f);
                if (opacity <= 0.0f) { continue; }
            }
            m_order.push_back(Order{ .onTop = desc.alwaysOnTop, .distance = distance, .opacity = opacity, .text = i });
        }

        // Far to near (blending is only right when what is behind is drawn first); text that is
        // always on top after all the rest, so depth-tested text never covers it.
        std::sort(m_order.begin(), m_order.end(), [](const Order& a, const Order& b) {
            if (a.onTop != b.onTop) { return !a.onTop; }
            return a.distance > b.distance;
        });

        for (const Order& order : m_order) {
            const Text& text = m_texts[order.text];
            const WorldTextDesc& desc = text.desc;
            for (uint32_t r = 0; r < text.runCount; ++r) {
                const WorldTextRun& run = m_runs[text.firstRun + r];
                WorldTextDraw draw;
                draw.texture     = run.texture;
                draw.firstVertex = run.firstVertex;
                draw.vertexCount = run.vertexCount;
                draw.alwaysOnTop = desc.alwaysOnTop;
                draw.doubleSided = desc.doubleSided;
                draw.push.anchor  = glm::vec4(desc.anchor, order.opacity);
                draw.push.right   = glm::vec4(desc.right, desc.intensity);
                draw.push.up      = glm::vec4(desc.up, run.pixelRange);
                draw.push.modes   = glm::uvec4(static_cast<uint32_t>(run.mode), static_cast<uint32_t>(desc.facing),
                                               run.effects.shadowPass ? 1u : 0u, 0u);
                draw.push.colours = glm::uvec4(run.effects.outlineColour, run.effects.glowColour, 0u, 0u);
                draw.push.widths  = glm::vec4(run.effects.outlineWidth, run.effects.glowWidth, run.effects.glowSoftness, run.effects.dilate);
                draw.push.extra   = glm::vec4(run.effects.shadowSoftness, 0.0f, 0.0f, 0.0f);
                out.push_back(draw);
            }
        }

    }   // WorldTextPass::Prepare()

    void WorldTextPass::Upload(uint32_t frameIndex) {

        m_uploadedFrame = UINT32_MAX;
        m_uploadedCount = 0;
        if (m_vertices.empty() || frameIndex >= m_vertexBuffers.size()) { return; }

        // Grow this frame's buffer (powers of two). Safe to replace here: BeginFrame() already
        // waited on this frame index's fence, so the GPU is done with the old one.
        uint32_t& capacity = m_vertexCapacity[frameIndex];
        if (capacity < m_vertices.size()) {
            uint32_t newCapacity = 6 * 1024;
            while (newCapacity < m_vertices.size()) { newCapacity *= 2; }
            m_vertexBuffers[frameIndex].Create(m_context, vk::DeviceSize{ newCapacity } * sizeof(WorldTextVertex),
                vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_vertexMapped[frameIndex] = m_vertexBuffers[frameIndex].Map();
            capacity = newCapacity;
        }
        memcpy(m_vertexMapped[frameIndex], m_vertices.data(), m_vertices.size() * sizeof(WorldTextVertex));
        m_uploadedFrame = frameIndex;
        m_uploadedCount = static_cast<uint32_t>(m_vertices.size());

        // Made here rather than in Record(), so recording never allocates.
        EnsureTextureSet(TextureHandle{});   // the white fallback first, so it always fits
        for (const WorldTextRun& run : m_runs) { EnsureTextureSet(run.texture); }

    }   // WorldTextPass::Upload()

    vk::DescriptorSet WorldTextPass::EnsureTextureSet(TextureHandle texture) {

        if (!texture.IsValid() || texture.id >= m_resources.Textures().size()) { texture = m_resources.DefaultWhiteTexture(); }

        if (auto it = m_textureSets.find(texture.id); it != m_textureSets.end()) { return *it->second; }

        // Pool full: fall back to the white texture's set (made first, so it always exists).
        if (m_textureSets.size() >= m_maxTextureSets - 1 && texture.id != m_resources.DefaultWhiteTexture().id) {
            if (!m_warnedTextureSets) {
                CRU_WARN("[Renderer] World text used more than {} different font atlases; the rest are drawn white.", m_maxTextureSets - 1);
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

    }   // WorldTextPass::EnsureTextureSet()

    // ------------------------------------------------------------------------------------------------------------- //
    //  DRAWING
    // ------------------------------------------------------------------------------------------------------------- //

    void WorldTextPass::Record(vk::CommandBuffer cmd, uint32_t samples, vk::DescriptorSet frameSet, const std::vector<WorldTextDraw>& draws) const {

        if (draws.empty() || m_uploadedFrame >= m_vertexBuffers.size()) { return; }   // nothing, or Upload() didn't run

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipelines[RenderLayouts::MsaaIndex(samples)]);
        // Set 0 again: this layout's push-constant range differs from the previous pipeline's, so
        // the earlier binding doesn't carry over.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_layout, 0, frameSet, nullptr);
        cmd.bindVertexBuffers(0, *m_vertexBuffers[m_uploadedFrame].GetBuffer(), { vk::DeviceSize{ 0 } });

        const auto whiteSet = m_textureSets.find(m_resources.DefaultWhiteTexture().id);
        uint32_t boundTexture = UINT32_MAX;
        int      boundCull    = -1;
        int      boundDepth   = -1;
        for (const WorldTextDraw& draw : draws) {
            if (draw.firstVertex + draw.vertexCount > m_uploadedCount) { continue; }

            // Upload() made every set; an invalid texture (underlines) or a full pool gets white.
            const bool valid = draw.texture.IsValid() && draw.texture.id < m_resources.Textures().size();
            auto it = valid ? m_textureSets.find(draw.texture.id) : whiteSet;
            if (it == m_textureSets.end()) { it = whiteSet; }
            if (it == m_textureSets.end()) { continue; }
            if (it->first != boundTexture) {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_layout, 1, *it->second, nullptr);
                boundTexture = it->first;
            }

            const int cull = draw.doubleSided ? 0 : 1;
            if (cull != boundCull) {
                cmd.setCullMode(draw.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);
                boundCull = cull;
            }
            const int depth = draw.alwaysOnTop ? 0 : 1;
            if (depth != boundDepth) {
                cmd.setDepthTestEnable(draw.alwaysOnTop ? vk::False : vk::True);
                boundDepth = depth;
            }

            cmd.pushConstants<WorldTextPush>(*m_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, draw.push);
            cmd.draw(draw.vertexCount, 1, draw.firstVertex, 0);
        }

    }   // WorldTextPass::Record()

}   // NAMESPACE CRUCIBLE
