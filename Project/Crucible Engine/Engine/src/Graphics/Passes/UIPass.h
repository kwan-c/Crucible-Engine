/*************************************************************************
  \file       UIPass.h
  \brief
    Draws screen-space UI (UI.slang) over a view's finished picture: the
    game's HUD and menus in the Game view and the game window, and any UI
    submitted for one particular viewport (the UI editor).
  \details
    Collects the frame's UI lists (Renderer::SubmitUI / SubmitViewportUI),
    owns the vertex and index buffers they are copied into, one small
    descriptor set per texture used, its sampler and its pipeline.

    Runs inside the final pass, after tonemapping and debug lines, so the
    UI is never exposed, bloomed, fogged or graded. Ready for a world-space
    variant later: the vertex shader only applies a transform it is given.
*************************************************************************/
#pragma once

#include "Graphics/UITypes.h"
#include "Graphics/Vulkan/VulkanResource.h"

#include <glm/glm.hpp>

#include <unordered_map>
#include <vector>

namespace Crucible {

    class VulkanContext;
    class RenderResources;

    class UIPass {
    public:
        /// Target id of the game's UI (SubmitUI): drawn in Game views and the game window.
        /// Any other target id is a ViewportHandle::id (SubmitViewportUI).
        static constexpr uint32_t kScreenTarget = 0xFFFFFFFEu;

        /*********************************************************************
         \param[in] context    Used to create buffers and sets. Must outlive this object.
         \param[in] resources  Where TextureHandles are looked up. Must outlive this object.
         *********************************************************************/
        UIPass(VulkanContext& context, const RenderResources& resources) : m_context(context), m_resources(resources) {}

        UIPass(const UIPass&) = delete;
        UIPass& operator=(const UIPass&) = delete;

        /*********************************************************************
         \brief  Creates the pipeline, sampler and texture-set pool. Call once,
                 before anything is submitted.
         \param[in] textureSetLayout  One combined image sampler at binding 0
                                      (RenderLayouts::GetSingleTextureSetLayout).
         \param[in] colourFormat      Format of the images UI is drawn into
                                      (the swapchain's; viewports use the same).
         \param[in] depthFormat       Format of the depth image the final pass
                                      has attached (unused by UI, but dynamic
                                      rendering requires the pipeline to name it).
         \param[in] framesInFlight    How many frames can be in progress at once.
         *********************************************************************/
        void Create(vk::DescriptorSetLayout textureSetLayout, vk::Format colourFormat, vk::Format depthFormat, uint32_t framesInFlight);

        /*********************************************************************
         \brief  Copies one UI list for this frame. Bad commands (indices out
                 of range) are dropped with a warning.
         \param[in] target  kScreenTarget, or a ViewportHandle::id.
         \param[in] list    The vertices, indices and commands (copied).
         *********************************************************************/
        void Submit(uint32_t target, const UIDrawList& list);

        /// Forgets this frame's lists. The Renderer calls it at the end of Update().
        void ClearSubmitted();

        /*********************************************************************
         \brief  Copies this frame's lists into this frame's buffers (growing
                 them if needed) and makes any texture sets they need.
         \param[in] frameIndex  The frame in flight being built. Its fence
                                must already have been waited on.
         *********************************************************************/
        void Upload(uint32_t frameIndex);

        /// True when something was submitted for `target` this frame.
        bool HasContent(uint32_t target) const;

        /*********************************************************************
         \brief  Draws every list submitted for `target`, in submission order,
                 into the rendering pass that is already open.
         \param[in] cmd         Command buffer, inside the final pass.
         \param[in] frameIndex  The frame in flight being recorded (Upload() ran for it).
         \param[in] extent      Size of the image being drawn into.
         \param[in] transform   Canvas pixels -> image pixels: xy = scale, zw = offset.
         \param[in] target      kScreenTarget, or a ViewportHandle::id.
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, uint32_t frameIndex, vk::Extent2D extent, const glm::vec4& transform, uint32_t target) const;

    private:
        /// The texture's set (made on first use). Invalid or unknown -> the white texture.
        vk::DescriptorSet EnsureTextureSet(TextureHandle texture);

        // One command, already moved into the frame's combined arrays.
        struct Command {
            TextureHandle     texture;
            UIDrawMode        mode = UIDrawMode::Image;
            glm::vec4         clipRect{ 0.0f };
            uint32_t          firstIndex = 0;
            uint32_t          indexCount = 0;
            int32_t           vertexOffset = 0;
            float             pixelRange = 0.0f;   // UIDrawCommand::pixelRange
            vk::DescriptorSet set{};   // filled by Upload()
        };
        // One Submit() call: a run of m_commands, for one target.
        struct Submission {
            uint32_t target = kScreenTarget;
            uint32_t firstCommand = 0;
            uint32_t commandCount = 0;
        };

        static constexpr uint32_t kMaxVertices    = 1'000'000;   // per frame, all lists together
        static constexpr uint32_t kMaxIndices     = 3'000'000;
        static constexpr uint32_t kMaxTextureSets = 512;         // different textures the UI can use

        VulkanContext&         m_context;
        const RenderResources& m_resources;

        // This frame's lists, joined end to end.
        std::vector<UIVertex>   m_vertices;
        std::vector<uint32_t>   m_indices;
        std::vector<Command>    m_commands;
        std::vector<Submission> m_submissions;
        bool                    m_warnedBadCommand = false;
        bool                    m_warnedOverflow = false;
        bool                    m_warnedTextureSets = false;

        // One host-visible vertex and index buffer per frame in flight, grown on demand.
        std::vector<VulkanBuffer> m_vertexBuffers;
        std::vector<void*>        m_vertexMapped;
        std::vector<uint32_t>     m_vertexCapacity;   // in vertices
        std::vector<VulkanBuffer> m_indexBuffers;
        std::vector<void*>        m_indexMapped;
        std::vector<uint32_t>     m_indexCapacity;    // in indices

        // Linear, clamped, trilinear sampler for every UI texture.
        vk::raii::Sampler       m_sampler = nullptr;

        // One set per texture used. Pool BEFORE the sets it allocates (destroyed after them).
        vk::DescriptorSetLayout                               m_textureSetLayout;
        vk::raii::DescriptorPool                              m_texturePool = nullptr;
        std::unordered_map<uint32_t, vk::raii::DescriptorSet> m_textureSets;   // by TextureHandle::id

        vk::raii::PipelineLayout m_layout = nullptr;
        vk::raii::Pipeline       m_pipeline = nullptr;
    };

}   // NAMESPACE CRUCIBLE
