/*************************************************************************
  \file       ViewTargets.h
  \brief
    The images and descriptor sets one view renders through before its
    picture reaches the screen (PostTargets). One per editor viewport, plus
    one for the window in a standalone build.
  \details
    Moved out of the Renderer class so render passes (Graphics/Passes/) can
    work on a view's targets without knowing about the Renderer. Created and
    resized by ViewTargetAllocator, below.
*************************************************************************/
#pragma once

#include "Vulkan/VulkanResource.h"
#include "Vulkan/VulkanContext.h"
#include "RenderTypes.h"

#include <array>

namespace Crucible {

    // Where one view's scene is drawn before it reaches the screen: a floating-point (HDR)
    // colour image, plus the chain of ever smaller images the bloom is built in. One per
    // viewport, and one for the window in a standalone build. See Renderer::DrawSceneHdr, BloomPass
    // and TonemapPass.
    struct PostTargets {
        vk::Extent2D                                     extent{};
        VulkanImage                                      hdrImage;
        vk::raii::ImageView                              hdrView = nullptr;
        uint32_t                                         bloomCount = 0;   // steps in use (small targets get fewer)
        std::array<VulkanImage, kBloomMips>              bloomImages;
        std::array<vk::raii::ImageView, kBloomMips>      bloomViews{ nullptr, nullptr, nullptr, nullptr, nullptr };
        std::array<vk::Extent2D, kBloomMips>             bloomExtents{};
        vk::raii::DescriptorSet                          hdrSet = nullptr;       // the HDR image, as a bloom source
        std::array<vk::raii::DescriptorSet, kBloomMips>  bloomSets{ nullptr, nullptr, nullptr, nullptr, nullptr };
        vk::raii::DescriptorSet                          tonemapSet = nullptr;   // HDR image + finished bloom
        // Ambient occlusion, all half size: a depth-only image of the scene, the raw (noisy)
        // occlusion, and the smoothed result the scene shader samples (set 0, binding 9).
        vk::Extent2D                                     aoExtent{};
        VulkanImage                                      aoDepth;
        vk::raii::ImageView                              aoDepthView = nullptr;
        VulkanImage                                      aoNormal;               // smooth view-space normals, from the same pre-pass
        vk::raii::ImageView                              aoNormalView = nullptr;
        VulkanImage                                      aoRaw;
        vk::raii::ImageView                              aoRawView = nullptr;
        VulkanImage                                      aoBlur;
        vk::raii::ImageView                              aoBlurView = nullptr;
        vk::raii::DescriptorSet                          aoDepthSet = nullptr;   // depth + normals, for the occlusion pass
        vk::raii::DescriptorSet                          aoBlurSet = nullptr;    // raw occlusion + depth, for the blur pass
        // MSAA (samples > 1): the scene is drawn into these multisampled images, which are
        // then resolved (averaged) into hdrImage and the view's ordinary depth image.
        uint32_t                                         samples = 1;
        VulkanImage                                      msaaColor;
        vk::raii::ImageView                              msaaColorView = nullptr;
        VulkanImage                                      msaaDepth;
        vk::raii::ImageView                              msaaDepthView = nullptr;
    };

    // Creates and resizes PostTargets, and owns what they are built from: the descriptor pool
    // their sets come out of and the two samplers their images are read through.
    class ViewTargetAllocator {
    public:
        /// \param[in] context  Used to create images and sets. Must outlive this object.
        explicit ViewTargetAllocator(VulkanContext& context) : m_context(context) {}

        ViewTargetAllocator(const ViewTargetAllocator&) = delete;
        ViewTargetAllocator& operator=(const ViewTargetAllocator&) = delete;

        /*********************************************************************
         \brief  Creates the samplers and the descriptor pool. Call once,
                 before Create().
         \param[in] maxViews             Most PostTargets alive at once (every
                                        viewport, plus the window).
         \param[in] oneTextureSetLayout  Layout of a set with one sampled texture.
         \param[in] twoTextureSetLayout  Layout of a set with two sampled textures.
         \param[in] depthFormat          Format of the scene's depth images.
         *********************************************************************/
        void Init(uint32_t maxViews, vk::DescriptorSetLayout oneTextureSetLayout,
                  vk::DescriptorSetLayout twoTextureSetLayout, vk::Format depthFormat);

        /*********************************************************************
         \brief  (Re)creates a view's HDR image, MSAA pair, bloom chain and
                 ambient occlusion images at the given size, and points its
                 descriptor sets at them. The sets are allocated the first
                 time and reused after that.
         \param[in,out] targets  The view's targets.
         \param[in] width        Size in pixels, and height (0 is treated as 1).
         \param[in] samples      MSAA samples per pixel: 1, 2 or 4.
         \note  When replacing existing targets, the caller makes sure the GPU
                is idle first.
         *********************************************************************/
        void Create(PostTargets& targets, uint32_t width, uint32_t height, uint32_t samples);

        /// Smooth (linear) sampling, clamped at the edges: for reading a view's colour images.
        const vk::raii::Sampler& LinearSampler() const { return m_viewportSampler; }
        /// No filtering: for reading depth and normals.
        const vk::raii::Sampler& PointSampler() const  { return m_pointSampler; }

    private:
        void CreateSamplers();
        void CreateDescriptorPool();

        VulkanContext&           m_context;
        uint32_t                 m_maxViews = 1;
        vk::DescriptorSetLayout  m_oneTextureSetLayout;
        vk::DescriptorSetLayout  m_twoTextureSetLayout;
        vk::Format               m_depthFormat = vk::Format::eUndefined;

        vk::raii::Sampler        m_viewportSampler = nullptr;
        vk::raii::Sampler        m_pointSampler = nullptr;
        vk::raii::DescriptorPool m_postDescriptorPool = nullptr;
    };

}   // NAMESPACE CRUCIBLE
