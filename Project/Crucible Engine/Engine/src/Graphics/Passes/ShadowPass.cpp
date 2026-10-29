/*************************************************************************
  \file       ShadowPass.cpp
  \brief
    See ShadowPass.h.
*************************************************************************/
#include "Graphics/Passes/ShadowPass.h"

#include "Graphics/RenderResources.h"
#include "Graphics/Camera.h"
#include "Graphics/Vulkan/RenderLayouts.h"
#include "Graphics/Vulkan/VulkanCommands.h"
#include "Graphics/Vulkan/VulkanShader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Crucible {

    namespace {
        // The push constant: the cascade (vertex), then CutoutPush (cut-out fragment shader).
        struct ShadowPush {
            uint32_t   cascade = 0;
            CutoutPush cutout{};
        };
        static_assert(sizeof(ShadowPush) == 12 && offsetof(ShadowPush, cutout) == 4, "ShadowPush must match Shadow.slang.");
        constexpr vk::ShaderStageFlags kShadowPushStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    }

    void ShadowPass::Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout materialSetLayout, vk::Format depthFormat) {


        vk::raii::ShaderModule shaderModule = LoadShaderModule(context, "Shadow.spv");

        // Solid materials: vertex stage only. With no fragment shader the rasterizer still writes
        // depth, which is all a shadow map needs (and keeps early-Z).
        vk::PipelineShaderStageCreateInfo stage{ .stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "shadowVert" };

        // Same vertex buffers as the main pass, but only location 0 (position) is read.
        auto binding    = Vertex::GetBindingDescription();
        auto attributes = Vertex::GetAttributeDescriptions();
            const std::array solidAttributes{attributes[0], attributes[5], attributes[6]};
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount   = 1,
            .pVertexBindingDescriptions      = &binding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(solidAttributes.size()),
            .pVertexAttributeDescriptions    = solidAttributes.data()
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

        // One layout for both pipelines (so switching between them keeps the sets bound): set 1 is
        // the material, which only the cut-out pipeline reads.
        vk::PushConstantRange push{ .stageFlags = kShadowPushStages, .offset = 0, .size = sizeof(ShadowPush) };
        const std::array<vk::DescriptorSetLayout, 2> setLayouts{ frameSetLayout, materialSetLayout };
        vk::PipelineLayoutCreateInfo layoutInfo{
            .setLayoutCount = static_cast<uint32_t>(setLayouts.size()), .pSetLayouts = setLayouts.data(),
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

        // Cut-out: also the uv (location 2), and a fragment shader that drops cut-out pixels.
        const vk::PipelineShaderStageCreateInfo cutoutStages[] = {
            { .stage = vk::ShaderStageFlagBits::eVertex,   .module = shaderModule, .pName = "shadowCutoutVert" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "shadowCutoutFrag" }
        };
        const std::array cutoutAttributes{ attributes[0], attributes[2], attributes[5], attributes[6] };
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(cutoutAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions    = cutoutAttributes.data();
        chain.get<vk::GraphicsPipelineCreateInfo>().stageCount = 2;
        chain.get<vk::GraphicsPipelineCreateInfo>().pStages    = cutoutStages;
        m_shadowCutoutPipeline = vk::raii::Pipeline(context.GetDevice(), nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());

    }   // ShadowPass::Create()

    void ShadowPass::CreateResources(VulkanContext& context) {

        m_shadowImage.Create(context, kMapSize, kMapSize, kFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            kMaxShadowCascades);

        // One view of every layer for sampling (Texture2DArray), one per layer to render into.
        vk::ImageViewCreateInfo arrayViewInfo{
            .image            = m_shadowImage.GetImage(),
            .viewType         = vk::ImageViewType::e2DArray,
            .format           = kFormat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .baseArrayLayer = 0, .layerCount = kMaxShadowCascades }
        };
        m_shadowArrayView = vk::raii::ImageView(context.GetDevice(), arrayViewInfo);
        for (uint32_t layer = 0; layer < kMaxShadowCascades; ++layer) {
            vk::ImageViewCreateInfo layerViewInfo{
                .image            = m_shadowImage.GetImage(),
                .viewType         = vk::ImageViewType::e2D,
                .format           = kFormat,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .baseArrayLayer = layer, .layerCount = 1 }
            };
            m_shadowLayerViews[layer] = vk::raii::ImageView(context.GetDevice(), layerViewInfo);
        }

        // Comparison sampler: returns the fraction of the (bilinear) 2x2 footprint whose stored depth
        // passes ref <= stored, i.e. is lit. Outside the map = border white = depth 1 = lit.
        vk::SamplerCreateInfo samplerInfo{
            .magFilter               = vk::Filter::eLinear,
            .minFilter               = vk::Filter::eLinear,
            .mipmapMode              = vk::SamplerMipmapMode::eNearest,
            .addressModeU            = vk::SamplerAddressMode::eClampToBorder,
            .addressModeV            = vk::SamplerAddressMode::eClampToBorder,
            .addressModeW            = vk::SamplerAddressMode::eClampToEdge,
            .anisotropyEnable        = vk::False,
            .compareEnable           = vk::True,
            .compareOp               = vk::CompareOp::eLessOrEqual,
            .minLod                  = 0.0f,
            .maxLod                  = 0.0f,
            .borderColor             = vk::BorderColor::eFloatOpaqueWhite,
            .unnormalizedCoordinates = vk::False
        };
        m_shadowSampler = vk::raii::Sampler(context.GetDevice(), samplerInfo);

    }   // ShadowPass::CreateResources()

    void ShadowPass::ComputeCascades(const Camera& camera, const glm::vec3& lightDir, const ShadowSettings& settings, uint32_t count,
                                     FrameUniforms& ubo, std::array<glm::mat4, kMaxShadowCascades>& outViewProj) const {

        const float nearZ  = std::max(camera.nearZ, 1e-3f);
        const float farZ   = glm::clamp(settings.distance, nearZ + 0.1f, camera.farZ);
        const float lambda = glm::clamp(settings.splitLambda, 0.0f, 1.0f);
        const float size   = static_cast<float>(kMapSize);

        // Half-diagonal of the view frustum's cross-section per unit of depth.
        const float tanHalf  = std::tan(glm::radians(camera.fovY) * 0.5f);
        const float diagRate = tanHalf * std::sqrt(1.0f + camera.aspect * camera.aspect);

        const glm::vec3 forward = camera.Forward();
        const glm::vec3 L       = glm::normalize(lightDir);   // direction the light travels
        const glm::vec3 up      = std::abs(L.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const float     ext     = std::max(settings.casterExtension, 0.0f);

        float sliceNear = nearZ;
        for (uint32_t c = 0; c < count; ++c) {

            // "Practical" split scheme: blend of logarithmic and uniform splits.
            const float p         = static_cast<float>(c + 1) / static_cast<float>(count);
            const float logSplit  = nearZ * std::pow(farZ / nearZ, p);
            const float uniSplit  = nearZ + (farZ - nearZ) * p;
            const float sliceFar  = lambda * logSplit + (1.0f - lambda) * uniSplit;

            // Smallest sphere around the slice whose centre is on the view axis: equidistant from the
            // near and far rings, clamped into the slice. It depends only on the slice, not on where
            // the camera looks, so its size never changes as the camera turns - with the snapping
            // below, that is what keeps shadow edges from shimmering.
            const float a  = sliceNear * diagRate;   // near ring radius
            const float b  = sliceFar  * diagRate;   // far ring radius
            float centreZ  = (sliceFar * sliceFar + b * b - sliceNear * sliceNear - a * a) / (2.0f * (sliceFar - sliceNear));
            centreZ        = glm::clamp(centreZ, sliceNear, sliceFar);
            float radius   = std::max(std::sqrt((centreZ - sliceNear) * (centreZ - sliceNear) + a * a),
                                      std::sqrt((sliceFar - centreZ) * (sliceFar - centreZ) + b * b));
            radius         = std::ceil(radius * 16.0f) / 16.0f;   // quantize against float drift

            const glm::vec3 centre = camera.position + forward * centreZ;

            // Ortho box around the sphere, pulled back toward the light by `ext` so casters between
            // the light and the sphere (off-screen towers, cliffs) still land in the map.
            const glm::mat4 lightView = glm::lookAt(centre - L * (radius + ext), centre, up);
            // orthoRH_ZO, not glm::ortho: the project does not define GLM_FORCE_DEPTH_ZERO_TO_ONE, so
            // glm::ortho maps depth to -1..1 and Vulkan (which keeps 0..1) would clip away the half
            // of the box nearest the light - losing the shadows of anything high above its receiver.
            glm::mat4       lightProj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + ext);

            // Texel snapping: shift the projection so the world origin lands exactly on a texel. The
            // map then only ever moves in whole texels as the camera moves.
            glm::vec4 origin = lightProj * lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            origin *= size * 0.5f;
            const glm::vec2 snapOffset = (glm::round(glm::vec2(origin)) - glm::vec2(origin)) * (2.0f / size);
            lightProj[3][0] += snapOffset.x;
            lightProj[3][1] += snapOffset.y;

            outViewProj[c]           = lightProj * lightView;
            ubo.shadowViewProj[c]    = outViewProj[c];
            ubo.cascadeSplits[c]     = sliceFar;
            ubo.cascadeTexelSize[c]  = 2.0f * radius / size;

            sliceNear = sliceFar;
        }

        ubo.shadowParams = glm::vec4(1.0f / size, settings.depthBias, settings.normalBias, glm::clamp(settings.fadeFraction, 0.0f, 1.0f));

    }   // ShadowPass::ComputeCascades()

    void ShadowPass::Record(vk::CommandBuffer cmd, vk::DescriptorSet frameSet,
                            const std::array<std::vector<DrawBatch>, kMaxShadowCascades>& cascades, uint32_t cascadeCount) const {

        if (cascadeCount == 0) { return; }
        const vk::Image shadowImage = m_shadowImage.GetImage();
        const auto&     layerViews  = m_shadowLayerViews;
        const uint32_t  mapSize     = kMapSize;

        // Shared by every view: wait for whatever sampled it last (an earlier view this frame, or a
        // previous frame - barriers cover all earlier submissions on the queue too): the scene's
        // fragment shaders, and the volumetric fog's compute pass. Its old contents are discarded
        // (UNDEFINED) since every layer is cleared below.
        TransitionImageLayout(cmd, shadowImage, vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
            {}, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            kMaxShadowCascades);

        const vk::Extent2D extent{ mapSize, mapSize };
        for (uint32_t c = 0; c < cascadeCount; ++c) {

            vk::RenderingAttachmentInfo depth{
                .imageView   = *layerViews[c],
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp      = vk::AttachmentLoadOp::eClear,
                .storeOp     = vk::AttachmentStoreOp::eStore,
                .clearValue  = vk::ClearDepthStencilValue(1.0f, 0)
            };
            vk::RenderingInfo renderingInfo{
                .renderArea       = { .offset = { 0, 0 }, .extent = extent },
                .layerCount       = 1,
                .pDepthAttachment = &depth
            };
            cmd.beginRendering(renderingInfo);

            // Same flipped viewport as every other pass, so the shader's uv mapping is uniform.
            cmd.setViewport(0, vk::Viewport(0.0f, static_cast<float>(mapSize), static_cast<float>(mapSize),
                                            -static_cast<float>(mapSize), 0.0f, 1.0f));
            cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_shadowLayout, 0, frameSet, nullptr);
            const ShadowPush push{ c, CutoutPush{} };
            cmd.pushConstants<ShadowPush>(*m_shadowLayout, kShadowPushStages, 0, push);
            const DepthPassPipelines pipelines{ .solid = *m_shadowPipeline, .cutout = *m_shadowCutoutPipeline, .layout = *m_shadowLayout,
                                                .cutoutPushStages = kShadowPushStages, .cutoutPushOffset = offsetof(ShadowPush, cutout),
                                                .dynamicCull = false };
            m_resources.DrawDepthBatches(cmd, cascades[c], pipelines);   // binds the pipelines itself

            cmd.endRendering();
        }

        TransitionImageLayout(cmd, shadowImage, vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eLateFragmentTests, vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
            kMaxShadowCascades);

    }   // ShadowPass::Record()

}   // NAMESPACE CRUCIBLE
