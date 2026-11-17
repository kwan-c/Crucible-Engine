/*************************************************************************
* \file:         RenderLayouts.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                See RenderLayouts.h for what this class owns and why.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "RenderLayouts.h"
#include "../RenderTypes.h"   // PushConstants (model matrix + material factors)

#include <fstream>

namespace Crucible {

    // ------------------------------------------------------------------------------------------------------------- //
    // ACCESSORS
    // ------------------------------------------------------------------------------------------------------------- //
    
    const vk::raii::DescriptorSetLayout&    RenderLayouts::GetDescriptorSetLayout()    const { return m_descriptorSetLayout; }
    const vk::raii::DescriptorSetLayout&    RenderLayouts::GetMaterialDescriptorSetLayout() const { return m_materialDescriptorSetLayout; }
    const vk::raii::DescriptorSetLayout&    RenderLayouts::GetSkyboxDescriptorSetLayout() const { return m_skyboxDescriptorSetLayout; }
    const vk::raii::DescriptorSetLayout&    RenderLayouts::GetTonemapDescriptorSetLayout() const { return m_tonemapDescriptorSetLayout; }

    // ------------------------------------------------------------------------------------------------------------- //
    // RENDER LAYOUTS
    // ------------------------------------------------------------------------------------------------------------- //

    void RenderLayouts::CreateDescriptorSetLayout(VulkanContext& context) {

        // Bindings the volumetric fog's compute pass reads (camera and fog settings, lights, the
        // shadow map, the light clusters) are visible to compute as well.
        const vk::ShaderStageFlags fragCompute = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

        std::array<vk::DescriptorSetLayoutBinding, 12> bindings{ {
            {
                .binding            = 0,
                .descriptorType     = vk::DescriptorType::eUniformBuffer,
                .descriptorCount    = 1,
                // Fragment needs this now too: FrameUniforms::cameraPosWS feeds the PBR specular
                // term's view vector, and the ambient/exposure/light-count members are all
                // fragment-side (see DefaultShader.slang's fragMain). Compute: the fog volume.
                .stageFlags         = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute
            },
            {
                // This frame's lights (StructuredBuffer<GpuLight> in DefaultShader.slang). One
                // buffer per frame in flight, shared by the window's set AND every editor
                // viewport's set for that frame - lights are world state, not per-camera.
                .binding            = 1,
                .descriptorType     = vk::DescriptorType::eStorageBuffer,
                .descriptorCount    = 1,
                .stageFlags         = fragCompute
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
                .stageFlags         = fragCompute
            },
            {
                // Its comparison sampler (SamplerComparisonState).
                .binding            = 4,
                .descriptorType     = vk::DescriptorType::eSampler,
                .descriptorCount    = 1,
                .stageFlags         = fragCompute
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
                .stageFlags         = fragCompute
            },
            {
                // Light clusters: the light indices the cells point into.
                .binding            = 7,
                .descriptorType     = vk::DescriptorType::eStorageBuffer,
                .descriptorCount    = 1,
                .stageFlags         = fragCompute
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
            },
            {
                // This view's integrated fog volume (Sampler3D fogVolume in Common.slang):
                // in-scattered light and transmittance from the camera to each froxel. Per view,
                // re-pointed each frame (Renderer::WriteViewTextures). Vertex: particles look up
                // their fog once, at their centre.
                .binding            = 10,
                .descriptorType     = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount    = 1,
                .stageFlags         = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
            },
            {
                // Every material's values for its shader's own parameters
                // (StructuredBuffer<float4> materialParams in Surface.slang; MaterialParamBuffer).
                // Per frame in flight, shared like the lights. Vertex as well as fragment: a
                // shader that moves vertices will read its parameters there.
                .binding            = 11,
                .descriptorType     = vk::DescriptorType::eStorageBuffer,
                .descriptorCount    = 1,
                .stageFlags         = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
            }
        } };
        vk::DescriptorSetLayoutCreateInfo layoutInfo{
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings    = bindings.data()
        };
        m_descriptorSetLayout = vk::raii::DescriptorSetLayout(context.GetDevice(), layoutInfo);

    }   // RenderLayouts::CreateDescriptorSetLayout()

    void RenderLayouts::CreateMaterialDescriptorSetLayout(VulkanContext& context) {

        // Set 1, bindings 0-7 (kMaterialTextureSlots): eight texture slots, all combined image
        // samplers, read by the fragment stage - and by the vertex stage of a shader that moves
        // vertices, which can read a texture there. The built-in shader's are 0-4: albedo, normal,
        // metallicRoughness, emissive, occlusion; a shader with a texture list of its own uses the
        // slots in its list's order. Eight for every material, so that every mesh shader shares
        // one pipeline layout. One of these sets is allocated per
        // Material (Renderer::CreateMaterial), never per frame-in-flight: unlike the camera UBO,
        // a material's texture bindings don't change every frame, so a single set can be reused
        // across all frames in flight (see Renderer::WriteMaterialDescriptorSet).
        std::array<vk::DescriptorSetLayoutBinding, kMaterialTextureSlots> bindings{};
        for (uint32_t i = 0; i < static_cast<uint32_t>(bindings.size()); ++i) {
            bindings[i] = vk::DescriptorSetLayoutBinding{
                .binding         = i,
                .descriptorType  = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
            };
        }
        vk::DescriptorSetLayoutCreateInfo layoutInfo{
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings    = bindings.data()
        };
        m_materialDescriptorSetLayout = vk::raii::DescriptorSetLayout(context.GetDevice(), layoutInfo);

    }   // RenderLayouts::CreateMaterialDescriptorSetLayout()

    void RenderLayouts::CreateSkyboxDescriptorSetLayout(VulkanContext& context) {

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

    }   // RenderLayouts::CreateSkyboxDescriptorSetLayout()

    void RenderLayouts::CreateTwoTextureSetLayout(VulkanContext& context) {

        // Two sampled textures. The tonemap pass reads the HDR scene (0) and the finished bloom (1)
        // through it; ambient occlusion reads depth and normals, then occlusion and depth.
        std::array<vk::DescriptorSetLayoutBinding, 2> tonemapBindings{ {
            { .binding = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment },
            { .binding = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment }
        } };
        vk::DescriptorSetLayoutCreateInfo tonemapSetInfo{
            .bindingCount = static_cast<uint32_t>(tonemapBindings.size()), .pBindings = tonemapBindings.data() };
        m_tonemapDescriptorSetLayout = vk::raii::DescriptorSetLayout(context.GetDevice(), tonemapSetInfo);

    }   // RenderLayouts::CreateTwoTextureSetLayout()

}   // NAMESPACE CRUCIBLE
