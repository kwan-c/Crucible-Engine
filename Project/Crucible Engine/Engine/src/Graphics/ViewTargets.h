/*************************************************************************
  \file       ViewTargets.h
  \brief
    The images and descriptor sets one view renders through before its
    picture reaches the screen (PostTargets). One per editor viewport, plus
    one for the window in a standalone build.
  \details
    Moved out of the Renderer class so render passes (Graphics/Passes/) can
    work on a view's targets without knowing about the Renderer. Created and
    resized by Renderer::CreatePostTargets.
*************************************************************************/
#pragma once

#include "Vulkan/VulkanResource.h"
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

}   // NAMESPACE CRUCIBLE
