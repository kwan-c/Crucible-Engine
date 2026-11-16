/*************************************************************************
  \file       ScenePass.h
  \brief
    The scene pass: draws a view's meshes with their materials and lighting
    (DefaultShader.slang), then the sky (Skybox.slang), then any water,
    then the particles, into the view's HDR image. With MSAA on it draws
    into the multisampled images and resolves into the HDR image and depth
    when the pass ends.
  \details
    Owns the mesh and sky pipelines. Which meshes to draw, the per-frame
    data and the choice of sky are the Renderer's; the water is drawn by
    WaterPass and the particles by ParticlePass inside this pass. Moved out
    of Renderer::DrawSceneHdr, DrawRenderQueue and DrawSkybox, and
    RenderLayouts::CreatePipeline and CreateSkyboxPipeline.

    With water in sight the pass is drawn in two halves: meshes and sky;
    then copies of the colour and depth so far (PostTargets::sceneCopy,
    depthCopy), which the water reads to show the bottom through itself;
    then the water and the particles, on top of what the first half left.
*************************************************************************/
#pragma once

#include "Graphics/ViewTargets.h"
#include "Graphics/RenderTypes.h"
#include "Graphics/DrawList.h"

#include <array>
#include <span>

namespace Crucible {

    class VulkanContext;
    class DeferredDestroyQueue;
    class RenderResources;
    class ParticlePass;
    class WaterPass;
    class WorldUIPass;

    class ScenePass {
    public:
        ScenePass(const RenderResources& resources, const ParticlePass& particles, const WaterPass& water, const WorldUIPass& worldUI)
            : m_resources(resources), m_particles(particles), m_water(water), m_worldUI(worldUI) {}

        /*********************************************************************
         \brief  Creates the mesh and sky pipelines (one of each per MSAA
                 level) and their layouts. Call once, before recording.
         \param[in] context            The VulkanContext to create them through.
         \param[in] frameSetLayout     Layout of set 0 (the per-frame data).
         \param[in] materialSetLayout  Layout of the mesh pipeline's set 1
                                      (the five material textures).
         \param[in] skySetLayout       Layout of the sky pipeline's set 1
                                      (one cubemap).
         \param[in] colourFormat       Format of the HDR scene image.
         \param[in] depthFormat        Format of the scene's depth image.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout materialSetLayout,
                    vk::DescriptorSetLayout skySetLayout, vk::Format colourFormat, vk::Format depthFormat);

        /*********************************************************************
         \brief  Replaces the mesh shader while running: builds the mesh
                 pipelines again from newly compiled SPIR-V and draws with
                 them from the next recording on. Editor shader hot reload
                 (Renderer::ReloadMeshShader).
         \param[in] context      The VulkanContext to create them through.
         \param[in] spirv        The compiled shader, with the same entry
                                points and interface as DefaultShader.spv.
         \param[in,out] retired  Takes the old pipelines: frames already
                                recorded still draw with them.
         \throws If the module or a pipeline cannot be created; the
                 pipelines in use are untouched then.
         *********************************************************************/
        void ReloadMeshShader(VulkanContext& context, std::span<const uint32_t> spirv, DeferredDestroyQueue& retired);

        /*********************************************************************
         \brief  Records the whole scene pass for one view, barriers included.
                 Leaves the HDR image ready to sample, and the depth image
                 holding the scene's depth, still in the attachment layout.
         \param[in] cmd          Command buffer, with no rendering pass open.
         \param[in,out] targets  The view's targets (HDR image, MSAA pair).
         \param[in] depthImage   The view's one-sample depth image, and
                                depthView its view.
         \param[in] extent       Size of the view.
         \param[in] frameSet     The view's set 0.
         \param[in] meshes       Batches in (material, mesh) order.
         \param[in] skySet       The sky cubemap's descriptor set, or a null
                                handle to draw no sky.
         \param[in] particles    Far to near.
         \param[in] water        Water surfaces in view (WaterPass::Prepare).
                                Empty = one half only, no copies.
         \param[in] worldUI      World-space UI draws (WorldUIPass::Prepare),
                                drawn last: over the particles and the water.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, PostTargets& targets, vk::Image depthImage, vk::ImageView depthView, vk::Extent2D extent,
                    vk::DescriptorSet frameSet, const std::vector<DrawBatch>& meshes, vk::DescriptorSet skySet,
                    const std::vector<ParticleBatch>& particles, const std::vector<WaterDraw>& water,
                    const std::vector<WorldUIDraw>& worldUI) const;

    private:
        using MeshPipelines = std::array<vk::raii::Pipeline, 3>;    // 1, 2, 4 samples

        // Builds the mesh pipelines (every MSAA level; solid and cut-out) from one shader module,
        // into `solid` and `cutout`. Needs m_meshLayout and the formats Create() was given.
        void BuildMeshPipelines(VulkanContext& context, vk::ShaderModule shader, MeshPipelines& solid, MeshPipelines& cutout) const;

        // Meshes, then the sky, then particles, then world-space UI, into the rendering pass Record() has open.
        void DrawQueue(vk::CommandBuffer cmd, uint32_t samples, vk::Extent2D extent, vk::DescriptorSet cameraSet,
                       const std::vector<DrawBatch>& meshes, vk::DescriptorSet skySet, const std::vector<ParticleBatch>& particles,
                       const std::vector<WorldUIDraw>& worldUI) const;
        // Between the two halves (water in sight): copies the HDR image and depth as the first
        // half left them into targets.sceneCopy / depthCopy, and readies every attachment for the
        // second half to carry on drawing into.
        void CopyForWater(vk::CommandBuffer cmd, PostTargets& targets, vk::Image depthImage, vk::Extent2D extent, bool msaa) const;

        const RenderResources& m_resources;   // the meshes and materials it draws
        const ParticlePass&    m_particles;   // drawn last, inside this pass
        const WaterPass&       m_water;       // drawn after the sky, before the particles
        const WorldUIPass&     m_worldUI;     // world-space UI: drawn after the particles, last in this pass

        MeshPipelines                     m_meshPipelines{ nullptr, nullptr, nullptr };   // 1, 2, 4 samples
        MeshPipelines                     m_cutoutPipelines{ nullptr, nullptr, nullptr }; // the same with kCutout on (cut-out materials)
        
        vk::Format                        m_colourFormat = vk::Format::eUndefined;        // what Create() was given: the mesh
        vk::Format                        m_depthFormat  = vk::Format::eUndefined;        // pipelines are built again on a shader

        vk::raii::PipelineLayout          m_meshLayout = nullptr;                         // set 0 + material set + PushConstants
        std::array<vk::raii::Pipeline, 3> m_skyPipelines{ nullptr, nullptr, nullptr };
        vk::raii::PipelineLayout          m_skyLayout = nullptr;                          // set 0 + cubemap set
    };

}   // NAMESPACE CRUCIBLE
