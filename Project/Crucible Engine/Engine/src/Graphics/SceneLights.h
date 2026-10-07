/*************************************************************************
  \file       SceneLights.h
  \brief
    The frame's lights, and the grid that tells the shader which point and
    spot lights can reach each part of a view ("clustered" lighting).
  \details
    Lights are submitted every frame. Before drawing they are reordered
    (directional lights first) and uploaded once, shared by every view.
    Then, per view, the screen is divided into a 3D grid of cells and each
    cell gets the list of lights that touch it, so a pixel only pays for
    the few lights near it. Also picks which directional light casts the
    sun shadows. Moved out of the Renderer class; SubmitLight forwards here.
*************************************************************************/
#pragma once

#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanResource.h"
#include "RenderTypes.h"
#include "LightTypes.h"

#include <array>
#include <vector>

namespace Crucible {

    class Camera;

    class SceneLights {
    public:
        /// One cluster grid per view: every viewport, plus the window.
        static constexpr uint32_t kMaxClusterViews   = 9;
        /// Light references per frame, across all views.
        static constexpr uint32_t kMaxClusterIndices = 1u << 19;

        /*********************************************************************
         \brief  Creates the light buffer and the two cluster buffers, one of
                 each per frame in flight. Call once.
         *********************************************************************/
        void Init(VulkanContext& context, uint32_t framesInFlight);

        /// Queues one light for this frame (see Renderer::SubmitLight). Extras past kMaxLights are ignored.
        void Submit(const LightData& light);
        /// Forgets the submitted lights. Call once the frame has been drawn.
        void ClearSubmitted() { m_lightQueue.clear(); m_shadowLightIndex = -1; }

        /*********************************************************************
         \brief  Reorders this frame's lights (directional first), substitutes
                 the fallback sun when none were submitted, and copies them
                 into this frame's light buffer. Call once a frame, before
                 any view is prepared.
         *********************************************************************/
        void BuildFrameLights(uint32_t frameIndex);

        /// Starts the frame's cluster data over. Call before the frame's first view.
        void BeginViews() { m_clusterGridScratch.clear(); m_clusterIndexScratch.clear(); m_preparedViews = 0; }
        /*********************************************************************
         \brief  Bins the frame's point and spot lights into the next view's
                 cluster grid, and writes where that grid sits into `ubo`
                 (clusterDims, clusterZ).
         \return How many lights reach the view.
         *********************************************************************/
        uint32_t BuildClusters(const Camera& camera, vk::Extent2D extent, FrameUniforms& ubo);
        /// Moves on to the next view's grid. Call after each view's BuildClusters().
        void EndView() { ++m_preparedViews; }
        /// Copies every view's cluster data into this frame's buffers.
        void UploadClusters(uint32_t frameIndex);

        uint32_t  LightCount() const       { return m_frameLightCount; }      ///< Lights uploaded this frame.
        uint32_t  DirLightCount() const    { return m_frameDirLightCount; }   ///< Of those, the directional ones (they come first).
        int32_t   ShadowLightIndex() const { return m_shadowLightIndex; }     ///< The light that casts sun shadows, or -1.
        glm::vec3 ShadowLightDir() const   { return m_shadowLightDir; }       ///< Direction its light travels.

        /// The buffers of one frame in flight (set 0, bindings 1, 6 and 7), and how many frames there are.
        size_t     BufferCount() const                    { return m_lightBuffers.size(); }
        vk::Buffer LightBuffer(size_t frame) const        { return *m_lightBuffers[frame].GetBuffer(); }
        vk::Buffer ClusterGridBuffer(size_t frame) const  { return *m_clusterGridBuffers[frame].GetBuffer(); }
        vk::Buffer ClusterIndexBuffer(size_t frame) const { return *m_clusterIndexBuffers[frame].GetBuffer(); }

    private:
        void CreateLightBuffers(VulkanContext& context, uint32_t framesInFlight);
        void CreateClusterBuffers(VulkanContext& context, uint32_t framesInFlight);

        // Submitted this frame, in submission order.
        std::vector<GpuLight>                       m_lightQueue;
        bool                                        m_warnedTooManyLights = false;
        int32_t                                     m_shadowLightIndex = -1;          // into this frame's light buffer
        glm::vec3                                   m_shadowLightDir{ 0.0f, -1.0f, 0.0f };

        // One light storage buffer per frame in flight (kMaxLights GpuLights each), persistently
        // mapped. Shared by every view's set 0 for that frame index - lights are world state,
        // not per-camera.
        std::vector<VulkanBuffer>                   m_lightBuffers;
        std::vector<void*>                          m_lightBuffersMapped;

        // m_frameLights is this frame's light buffer as uploaded (directional lights first).
        // Every prepared view appends its kClustersPerView cells and their light indices to the
        // two scratch arrays, which UploadClusters() copies into this frame's buffers.
        std::vector<GpuLight>                       m_frameLights;
        uint32_t                                    m_frameLightCount = 0;    // lights actually uploaded this frame (after fallback/clamp)
        uint32_t                                    m_frameDirLightCount = 0;
        uint32_t                                    m_preparedViews = 0;      // views prepared so far this frame
        std::vector<ClusterCell>                    m_clusterGridScratch;
        std::vector<uint32_t>                       m_clusterIndexScratch;
        std::vector<uint32_t>                       m_clusterCounts;          // scratch: per cell, one view
        std::vector<std::array<uint16_t, 6>>        m_clusterRanges;          // scratch: per light, cell range
        std::vector<uint32_t>                       m_clusterRangeLights;     // scratch: which light each range is
        std::vector<VulkanBuffer>                   m_clusterGridBuffers;
        std::vector<void*>                          m_clusterGridMapped;
        std::vector<VulkanBuffer>                   m_clusterIndexBuffers;
        std::vector<void*>                          m_clusterIndexMapped;
    };

}   // NAMESPACE CRUCIBLE
