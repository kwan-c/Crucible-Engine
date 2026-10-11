/*************************************************************************
  \file       Viewports.h
  \brief
    The offscreen views the editor draws into (its Scene and Game panels,
    and a UI editor tab): each one's role, images, camera, per-frame uniform
    data and settings, and the list of them. Empty in the standalone game,
    which draws to the window.
  \details
    Moved out of the Renderer class. Editor code still uses the Renderer's
    CreateViewport / ResizeViewport / ... functions, which forward here.
*************************************************************************/
#pragma once

#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanResource.h"
#include "ViewTargets.h"
#include "Passes/VolumetricFogPass.h"   // FogVolumeTargets
#include "DrawList.h"
#include "RenderTypes.h"
#include "Camera.h"

#include <unordered_map>
#include <vector>

namespace Crucible {

    // An offscreen render target: its own color+depth image, camera, and per-frame-in-flight
    // uniform buffer/descriptor set, so several can be drawn per frame with different cameras
    // (Scene view vs. Game view) without stepping on each other's data.
    struct Viewport {
        vk::Extent2D        extent{};
        ViewportRole        role = ViewportRole::Scene;   // which camera, whose UI (see ViewportRole)

        VulkanImage         colorImage;
        vk::raii::ImageView colorView = nullptr;
        VulkanImage         depthImage;
        vk::raii::ImageView depthView = nullptr;

        Camera              camera;   // independent from the main m_camera and from other viewports (a Game viewport's is overwritten with it every frame)

        std::vector<VulkanBuffer>            uniformBuffers;
        std::vector<void*>                   uniformBuffersMapped;
        std::vector<vk::raii::DescriptorSet> descriptorSets;

        bool                debugDraw = false;   // draw DebugDraw lines into this view (SetViewportDebugDraw)
        bool                outline = false;     // draw the selection outline (SetViewportOutline)
        bool                active = true;       // false: not culled, not rendered (SetViewportActive)
        glm::vec4           clearColour{ 0.0f, 0.0f, 0.0f, 1.0f };   // sRGB; shown where no scene is drawn (SetViewportClearColour)
        glm::vec4           uiTransform{ 1.0f, 1.0f, 0.0f, 0.0f };   // UI canvas pixels -> image pixels: xy scale, zw offset (SetViewportUITransform)
        RenderCullStats     cullStats;           // last frame's culling result for this view

        // Selection outline: R8 mask of the selected object (same size as colorImage), and
        // the composite pass's descriptor set pointing at it.
        VulkanImage             maskImage;
        vk::raii::ImageView     maskView = nullptr;
        vk::raii::DescriptorSet outlineSet = nullptr;

        ViewDrawList        draws;               // this frame's batches (PrepareView)
        PostTargets         post;                // HDR scene image + bloom chain, same size as colorImage
        FogVolumeTargets    fog;                 // volumetric fog volumes (made on first use; VolumetricFogPass)
    };

    class ViewportSet {
    public:
        static constexpr uint32_t kMaxViewports = 8;

        /*********************************************************************
         \param[in] context  Used to create images, buffers and sets. Must outlive this object.
         \param[in] targets  Creates each viewport's PostTargets. Must outlive this object.
         *********************************************************************/
        ViewportSet(VulkanContext& context, ViewTargetAllocator& targets) : m_context(context), m_targets(targets) {}

        ViewportSet(const ViewportSet&) = delete;
        ViewportSet& operator=(const ViewportSet&) = delete;

        /*********************************************************************
         \brief  Creates the descriptor pools, sized for kMaxViewports. Call once.
         \param[in] framesInFlight  How many frames can be in progress at once.
         \param[in] frameSetLayout  Layout of set 0 (the per-frame data).
         \param[in] maskSetLayout   Layout of the outline composite's set (the mask).
         \param[in] colourFormat    Format of a viewport's final colour image.
         \param[in] depthFormat     Format of a viewport's depth image.
         *********************************************************************/
        void Init(uint32_t framesInFlight, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout maskSetLayout,
                  vk::Format colourFormat, vk::Format depthFormat);

        /*********************************************************************
         \brief  Creates a viewport: its images, uniform buffers and sets.
         \param[in] width    Size in pixels, and height.
         \param[in] samples  MSAA samples per pixel: 1, 2 or 4.
         \param[in] role     What it is for (see ViewportRole).
         \return Its handle.
         \throws std::runtime_error past kMaxViewports.
         \note  Only binding 0 of its set 0 (the view's own uniforms) is
                written. The caller points the shared bindings at the
                per-frame buffers.
         *********************************************************************/
        ViewportHandle Create(uint32_t width, uint32_t height, uint32_t samples, ViewportRole role);
        /// Recreates a viewport's images at a new size (waits for the GPU first). No-op if the
        /// size is unchanged or zero.
        void Resize(ViewportHandle handle, uint32_t width, uint32_t height, uint32_t samples);
        /// Destroys a viewport (waits for the GPU first). No-op for an unknown handle.
        void Destroy(ViewportHandle handle);

        /// The viewport behind a handle. Throws std::runtime_error for an unknown or destroyed one.
        Viewport&       Get(ViewportHandle handle);
        const Viewport& Get(ViewportHandle handle) const;

        /// Every live viewport, keyed by ViewportHandle::id.
        std::unordered_map<uint32_t, Viewport>&       All()       { return m_viewports; }
        const std::unordered_map<uint32_t, Viewport>& All() const { return m_viewports; }

    private:
        void CreateDescriptorPools();
        void CreateViewportImages(Viewport& vp, uint32_t width, uint32_t height, uint32_t samples);
        void CreateViewportUniformsAndDescriptors(Viewport& vp);
        void WriteOutlineDescriptor(Viewport& vp);

        VulkanContext&       m_context;
        ViewTargetAllocator& m_targets;
        uint32_t                m_framesInFlight = 1;
        vk::DescriptorSetLayout m_frameSetLayout;
        vk::DescriptorSetLayout m_maskSetLayout;
        vk::Format              m_colourFormat = vk::Format::eUndefined;
        vk::Format              m_depthFormat  = vk::Format::eUndefined;

        // The pools are declared BEFORE m_viewports so they are destroyed after the viewports'
        // descriptor sets (same destruction-order rule as the material pools in RenderResources).
        vk::raii::DescriptorPool               m_viewportDescriptorPool = nullptr;
        vk::raii::DescriptorPool               m_outlineDescriptorPool = nullptr;
        std::unordered_map<uint32_t, Viewport> m_viewports;
        uint32_t                               m_nextViewportId = 0;
    };

}   // NAMESPACE CRUCIBLE
