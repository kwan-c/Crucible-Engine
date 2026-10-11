/*************************************************************************
  \file       VolumetricFogPass.h
  \brief
    The volumetric fog pass: lights the scene's height fog with the sun
    (and its shadows - light shafts) and point and spot lights, in a froxel
    volume per view (VolumetricFog.slang). The engine's first compute work.
  \details
    Per view, before the scene is drawn: fogInject works out each froxel's
    density and in-scattered light (sky or fog colour as ambient, sun with
    shadows, point and spot lights) into the light volume; fogIntegrate adds
    them up from the camera outward into the integrated volume, which the
    mesh, particle and sky shaders read (set 0, binding 10; Common.slang's
    FogForPoint / FogForSky). Its extra last slice holds each column's
    unshadowed light, which lights the fog beyond the volume and the sky's.

    Owns its compute pipelines, the set layout and pool for the volumes,
    the sampler shaders read them through, and a 1x1x1 "no fog" volume
    bound for views that have none. The volumes themselves live with each
    view (FogVolumeTargets in Viewport, and the Renderer's for the window).
*************************************************************************/
#pragma once

#include "Graphics/Vulkan/VulkanResource.h"

namespace Crucible {

    class VulkanContext;
    class VulkanUploadQueue;

    /// One view's fog volumes (VolumetricFogPass makes them on first use). Both rest in eGeneral.
    struct FogVolumeTargets {
        VulkanImage             lightImage;                // rgb = in-scattered light per metre, a = extinction per metre
        vk::raii::ImageView     lightView = nullptr;
        VulkanImage             integratedImage;           // rgb = light added up to each slice, a = transmittance; + 1 slice: unshadowed light per column
        vk::raii::ImageView     integratedView = nullptr;
        vk::raii::DescriptorSet set = nullptr;             // set 1 of both compute pipelines: the two volumes as storage images
        bool                    initialised = false;       // moved out of eUndefined yet (first Record)

        bool IsCreated() const { return *integratedView != vk::ImageView{}; }
    };

    class VolumetricFogPass {
    public:
        // The volume's size: froxels across, down, and depth slices. 160 x 90 x 128 RGBA16F is
        // about 15 MB per volume, two per view.
        static constexpr uint32_t   kWidth  = 160;
        static constexpr uint32_t   kHeight = 90;
        static constexpr uint32_t   kSlices = 128;
        static constexpr vk::Format kFormat = vk::Format::eR16G16B16A16Sfloat;
        // The volume's slices start this far from the camera (m); nearer fog is faded in.
        static constexpr float      kNearDepth = 0.5f;

        explicit VolumetricFogPass(VulkanContext& context) : m_context(context) {}

        VolumetricFogPass(const VolumetricFogPass&) = delete;
        VolumetricFogPass& operator=(const VolumetricFogPass&) = delete;

        /*********************************************************************
         \brief  Creates the pipelines, the volumes' set layout and pool, the
                 sampler and the "no fog" volume. Call once, before any view's
                 set 0 is written.
         \param[in] frameSetLayout  Layout of set 0 (camera, fog settings,
                                    lights, shadow map, light clusters).
         \param[in] uploads         Fills the "no fog" volume at the start
                                    of the first frame.
         \param[in] maxViews        Most views that can have volumes at once.
         *********************************************************************/
        void Create(vk::DescriptorSetLayout frameSetLayout, VulkanUploadQueue& uploads, uint32_t maxViews);

        /*********************************************************************
         \brief  Makes a view's two volumes and their set, the first time it
                 needs them. Cheap to call every frame.
         \param[in,out] targets  The view's volumes.
         *********************************************************************/
        void EnsureTargets(FogVolumeTargets& targets);

        /*********************************************************************
         \brief  Records one view's fog: light the volume, integrate it, and
                 make it ready to be sampled by the scene's shaders.
         \param[in] cmd          Command buffer, with no rendering pass open.
                                 After this view's shadow pass (inject reads
                                 the shadow map), before its scene pass.
         \param[in,out] targets  The view's volumes (EnsureTargets'd).
         \param[in] frameSet     The view's set 0.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, FogVolumeTargets& targets, vk::DescriptorSet frameSet) const;

        /*********************************************************************
         \brief  What a view's set 0, binding 10 should point at: its
                 integrated volume, or the "no fog" volume when it has none.
         \param[in] targets  The view's volumes, or nullptr.
         *********************************************************************/
        vk::DescriptorImageInfo SampledVolume(const FogVolumeTargets* targets) const;

    private:
        VulkanContext&            m_context;

        vk::raii::Pipeline        m_injectPipeline = nullptr;
        vk::raii::Pipeline        m_integratePipeline = nullptr;
        vk::raii::PipelineLayout  m_layout = nullptr;            // set 0 = frame, set 1 = the two volumes
        vk::raii::DescriptorSetLayout m_volumeSetLayout = nullptr;
        vk::raii::DescriptorPool  m_pool = nullptr;              // BEFORE every FogVolumeTargets::set (the views own them)
        vk::raii::Sampler         m_sampler = nullptr;           // trilinear, clamped: how shaders read the integrated volume

        VulkanImage               m_emptyImage;                  // 1x1x1: no light added, everything shows
        vk::raii::ImageView       m_emptyView = nullptr;
    };

}   // NAMESPACE CRUCIBLE
