/*************************************************************************
  \file       ScenePass.h
  \brief
    The scene pass: draws a view's meshes with their materials and lighting
    (DefaultShader.slang), then the sky (Skybox.slang), then the particles,
    into the view's HDR image. With MSAA on it draws into the multisampled
    pair and resolves into the HDR image and depth when the pass ends.
  \details
    Owns the mesh and sky pipelines. Which meshes to draw, the per-frame
    data and the choice of sky are the Renderer's; the particles are drawn
    by ParticlePass inside this pass. Moved out of Renderer::DrawSceneHdr,
    DrawRenderQueue and DrawSkybox, and RenderLayouts::CreatePipeline and
    CreateSkyboxPipeline.
*************************************************************************/
#pragma once

#include "Graphics/ViewTargets.h"
#include "Graphics/RenderTypes.h"
#include "Graphics/DrawList.h"

#include <array>

namespace Crucible {

    class VulkanContext;
    class RenderResources;
    class ParticlePass;

    class ScenePass {
    public:
        ScenePass(const RenderResources& resources, const ParticlePass& particles) : m_resources(resources), m_particles(particles) {}

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
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, PostTargets& targets, vk::Image depthImage, vk::ImageView depthView, vk::Extent2D extent,
                    vk::DescriptorSet frameSet, const std::vector<DrawBatch>& meshes, vk::DescriptorSet skySet,
                    const std::vector<ParticleBatch>& particles) const;

    private:
        // Meshes, then the sky, then particles, into the rendering pass Record() has open.
        void DrawQueue(vk::CommandBuffer cmd, uint32_t samples, vk::Extent2D extent, vk::DescriptorSet cameraSet,
                       const std::vector<DrawBatch>& meshes, vk::DescriptorSet skySet, const std::vector<ParticleBatch>& particles) const;

        const RenderResources& m_resources;   // the meshes and materials it draws
        const ParticlePass&    m_particles;   // drawn last, inside this pass

        std::array<vk::raii::Pipeline, 3> m_meshPipelines{ nullptr, nullptr, nullptr };   // 1, 2, 4 samples
        vk::raii::PipelineLayout          m_meshLayout = nullptr;                         // set 0 + material set + PushConstants
        std::array<vk::raii::Pipeline, 3> m_skyPipelines{ nullptr, nullptr, nullptr };
        vk::raii::PipelineLayout          m_skyLayout = nullptr;                          // set 0 + cubemap set
    };

}   // NAMESPACE CRUCIBLE
