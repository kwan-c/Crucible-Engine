/*************************************************************************
  \file       WorldTextPass.h
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    Draws WorldText (WorldText.slang) inside the scene pass, after water
    and particles: into the HDR image, depth-tested against the scene.
  \details
    Like ParticlePass, it is not a rendering pass of its own - ScenePass
    calls Record() with its pass open. It holds the frame's submitted text
    (Renderer::SubmitWorldText), sorts it for each view (Prepare), copies
    the glyph corners into this frame's vertex buffer (Upload), and owns
    the pipelines, its sampler and one descriptor set per atlas texture.

    Blending is premultiplied (ONE, ONE_MINUS_SRC_ALPHA) like particles;
    no depth writes, so text never cuts holes in what is drawn after it.
    Cull mode and depth test are dynamic state, set per draw: doubleSided
    off culls back faces, alwaysOnTop turns the depth test off. Texts are
    drawn far to near, with every alwaysOnTop text after the rest.
*************************************************************************/
#pragma once

#include "Graphics/WorldTextTypes.h"
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

    class WorldTextPass {
    public:
        /*********************************************************************
         \param[in] context    Used to create buffers and sets. Must outlive this object.
         \param[in] resources  The textures (font atlases) text is drawn with.
         *********************************************************************/
        WorldTextPass(VulkanContext& context, const RenderResources& resources) : m_context(context), m_resources(resources) {}

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

        /// Queues one piece of text for this frame (see Renderer::SubmitWorldText).
        void Submit(const WorldTextDesc& desc, std::span<const WorldTextVertex> vertices, std::span<const WorldTextRun> runs);
        /// Forgets everything submitted. Call once the frame has been drawn.
        void ClearSubmitted();

        /*********************************************************************
         \brief  Culls this frame's text against one view, fades it by the
                 camera's distance, and lists its draws far to near (every
                 alwaysOnTop text last).
         \param[in] camera  The view's camera.
         \param[out] out    Receives the view's draws.
         *********************************************************************/
        void Prepare(const Camera& camera, std::vector<WorldTextDraw>& out);

        /// Copies this frame's vertices into frame `frameIndex`'s buffer and makes the texture sets.
        void Upload(uint32_t frameIndex);

        /*********************************************************************
         \brief  Draws a view's list into the rendering pass that is open.
         \param[in] cmd       Command buffer, inside the scene pass (viewport set).
         \param[in] samples   MSAA samples of the target being drawn into.
         \param[in] frameSet  The view's set 0.
         \param[in] draws     From Prepare(), this frame.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, uint32_t samples, vk::DescriptorSet frameSet, const std::vector<WorldTextDraw>& draws) const;

    private:
        vk::DescriptorSet EnsureTextureSet(TextureHandle texture);

        struct Text {
            WorldTextDesc desc;
            uint32_t      firstRun = 0;
            uint32_t      runCount = 0;
        };
        struct Order {
            bool     onTop = false;
            float    distance = 0.0f;
            float    opacity = 1.0f;
            uint32_t text = 0;
        };

        static constexpr uint32_t m_maxVertices    = 2'000'000;   // per frame, all text together
        static constexpr uint32_t m_maxTextureSets = 128;         // different atlases in use

        VulkanContext&         m_context;
        const RenderResources& m_resources;

        // This frame's text. Runs' firstVertex are into m_vertices.
        std::vector<WorldTextVertex> m_vertices;
        std::vector<WorldTextRun>    m_runs;
        std::vector<Text>            m_texts;
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

        // Linear, clamped, trilinear (as the UI's), for every atlas.
        vk::raii::Sampler m_sampler = nullptr;

        // One set per texture used. Pool BEFORE the sets it allocates (destroyed after them).
        vk::DescriptorSetLayout                               m_textureSetLayout;
        vk::raii::DescriptorPool                              m_texturePool = nullptr;
        std::unordered_map<uint32_t, vk::raii::DescriptorSet> m_textureSets;   // by TextureHandle::id

        vk::raii::PipelineLayout          m_layout = nullptr;   // set 0 + texture set + WorldTextPush
        std::array<vk::raii::Pipeline, 3> m_pipelines{ nullptr, nullptr, nullptr };   // 1, 2, 4 samples
    };

}   // NAMESPACE CRUCIBLE
