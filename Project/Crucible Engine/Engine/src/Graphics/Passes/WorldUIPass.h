/*************************************************************************
  \file       WorldUIPass.h
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    Draws world-space UI (WorldUI.slang) - WorldText and world-space
    canvases - inside the scene pass, after water and particles: into the
    HDR image, depth-tested against the scene.
  \details
    Like ParticlePass, it is not a rendering pass of its own - ScenePass
    calls Record() with its pass open. It holds the frame's submitted panels
    (Renderer::SubmitWorldUI), sorts it for each view (Prepare), copies
    the glyph corners into this frame's vertex buffer (Upload), and owns
    the pipelines, its sampler and one descriptor set per texture (font
    atlases, images).

    Blending is premultiplied (ONE, ONE_MINUS_SRC_ALPHA) like particles;
    no depth writes, so a panel never cuts holes in what is drawn after it.
    Cull mode and depth test are dynamic state, set per draw: doubleSided
    off culls back faces, alwaysOnTop turns the depth test off. Panels are
    drawn far to near as wholes (a panel's own runs keep their order, so a
    canvas's layering holds), with every alwaysOnTop panel after the rest.
*************************************************************************/
#pragma once

#include "Graphics/WorldUITypes.h"
#include "Graphics/Vulkan/VulkanResource.h"

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace Crucible {

    class VulkanContext;
    class RenderResources;
    class Camera;

    class WorldUIPass {
    public:
        /*********************************************************************
         \param[in] context    Used to create buffers and sets. Must outlive this object.
         \param[in] resources  The textures (font atlases, images) panels are drawn with.
         *********************************************************************/
        WorldUIPass(VulkanContext& context, const RenderResources& resources) : m_context(context), m_resources(resources) {}

        /*********************************************************************
         \brief  Creates the pipelines (one per MSAA level), the sampler, the
                 texture-set pool and one vertex buffer slot per frame in flight.
         \param[in] frameSetLayout    Layout of set 0 (the per-frame data).
         \param[in] textureSetLayout  Layout of set 1 (one sampled texture).
         \param[in] colourFormat      Format of the HDR scene image.
         \param[in] depthFormat       Format of the scene's depth image.
         \param[in] framesInFlight    How many frames can be in progress at once.
         *********************************************************************/
        void Create(vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout textureSetLayout,
                    vk::Format colourFormat, vk::Format depthFormat, uint32_t framesInFlight);

        /// Queues one panel for this frame (see Renderer::SubmitWorldUI).
        void Submit(const WorldUIDesc& desc, std::span<const WorldUIVertex> vertices, std::span<const WorldUIRun> runs);
        /// Forgets everything submitted. Call once the frame has been drawn.
        void ClearSubmitted();

        /*********************************************************************
         \brief  Culls this frame's panels against one view, fades them by
                 the camera's distance, and lists their draws far to near
                 (every alwaysOnTop panel last).
         \param[in] camera  The view's camera.
         \param[out] out    Receives the view's draws.
         *********************************************************************/
        void Prepare(const Camera& camera, std::vector<WorldUIDraw>& out);

        /// Copies this frame's vertices into frame `frameIndex`'s buffer and makes the texture sets.
        void Upload(uint32_t frameIndex);

        /*********************************************************************
         \brief  Draws a view's list into the rendering pass that is open.
         \param[in] cmd       Command buffer, inside the scene pass (viewport set).
         \param[in] samples   MSAA samples of the target being drawn into.
         \param[in] frameSet  The view's set 0.
         \param[in] draws     From Prepare(), this frame.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, uint32_t samples, vk::DescriptorSet frameSet, const std::vector<WorldUIDraw>& draws) const;

    private:
        vk::DescriptorSet EnsureTextureSet(TextureHandle texture);

        struct Panel {
            WorldUIDesc desc;
            uint32_t      firstRun = 0;
            uint32_t      runCount = 0;
        };
        struct Order {
            bool     onTop = false;
            float    distance = 0.0f;
            float    opacity = 1.0f;
            uint32_t panel = 0;
        };

        static constexpr uint32_t m_maxVertices    = 2'000'000;   // per frame, all panels together
        static constexpr uint32_t m_maxTextureSets = 256;         // different textures in use (atlases, images)

        VulkanContext&         m_context;
        const RenderResources& m_resources;

        // This frame's panels. Runs' firstVertex are into m_vertices.
        std::vector<WorldUIVertex> m_vertices;
        std::vector<WorldUIRun>    m_runs;
        std::vector<Panel>           m_panels;
        std::vector<Order>           m_order;            // scratch for Prepare()
        bool                         m_warnedOverflow = false;
        bool                         m_warnedTextureSets = false;

        // One host-visible vertex buffer per frame in flight, grown on demand. m_uploadedFrame is
        // the frame Upload() last filled: Record() draws from that one.
        std::vector<VulkanBuffer> m_vertexBuffers;
        std::vector<void*>        m_vertexMapped;
        std::vector<uint32_t>     m_vertexCapacity;      // in vertices
        uint32_t                  m_uploadedFrame = UINT32_MAX;
        uint32_t                  m_uploadedCount = 0;

        // Linear, clamped, trilinear (as the UI's), for every texture.
        vk::raii::Sampler m_sampler = nullptr;

        // One set per texture used. Pool BEFORE the sets it allocates (destroyed after them).
        vk::DescriptorSetLayout                               m_textureSetLayout;
        vk::raii::DescriptorPool                              m_texturePool = nullptr;
        std::unordered_map<uint32_t, vk::raii::DescriptorSet> m_textureSets;   // by TextureHandle::id

        vk::raii::PipelineLayout          m_layout = nullptr;   // set 0 + texture set + WorldUIPush
        std::array<vk::raii::Pipeline, 3> m_pipelines{ nullptr, nullptr, nullptr };   // 1, 2, 4 samples
    };

}   // NAMESPACE CRUCIBLE
