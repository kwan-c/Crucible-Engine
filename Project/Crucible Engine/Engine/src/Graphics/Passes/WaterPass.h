/*************************************************************************
  \file       WaterPass.h
  \brief
    Water surfaces (the Water component), in the look of the "Umiko"
    reference: coloured by how deep the water is, see-through to a
    wobbling bottom, with shore foam, toon-lit ripples and glints, and the
    sky reflected at grazing angles (Water.slang).
  \details
    Holds the surfaces submitted this frame (Renderer::SubmitWater), culls
    them per view (Prepare), and draws them inside the second half of the
    scene pass (ScenePass), after the opaque meshes and the sky and before
    the particles. The water reads copies of the scene drawn so far
    (PostTargets::sceneCopy and depthCopy, through PostTargets::waterSet)
    instead of blending, so it works out itself how much of the bottom
    shows. It writes depth, and outline data with no line strength.
*************************************************************************/
#pragma once

#include "Graphics/RenderTypes.h"
#include "Graphics/DrawList.h"

#include <array>
#include <vector>

namespace Crucible {

    class VulkanContext;
    class RenderResources;
    class Camera;

    class WaterPass {
    public:
        /// \param[in] resources  The meshes water surfaces are drawn with.
        explicit WaterPass(const RenderResources& resources) : m_resources(resources) {}

        /*********************************************************************
         \brief  Creates the pass's pipelines (one per MSAA level) and their
                 layout. Call once, before recording.
         \details Depth-tested and depth-writing, back faces culled, no
                  blending; two colour outputs like every scene-pass
                  pipeline (colour and outline data). Layout = set 0 + the
                  scene copies (two textures) + WaterPush (128 bytes, vertex
                  and fragment).
         \param[in] context             The VulkanContext to create them through.
         \param[in] frameSetLayout      Layout of set 0 (the per-frame data).
         \param[in] sceneCopySetLayout  Layout of set 1: two sampled textures
                                        (scene colour, scene depth).
         \param[in] colourFormat        Format of the HDR scene image.
         \param[in] depthFormat         Format of the scene's depth image.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout sceneCopySetLayout,
                    vk::Format colourFormat, vk::Format depthFormat);

        /*********************************************************************
         \brief  Queues one water surface for this frame (see
                 Renderer::SubmitWater). Forgotten after the frame.
         \param[in] mesh      The surface's shape: normally the built-in
                              Plane (1 x 1 m, facing +Y), scaled by `model`.
         \param[in] model     Object -> world transform.
         \param[in] settings  How the water looks (colours in sRGB).
         \param[in] sceneHidden  Editor scene visibility: left out of Scene-role views.
         *********************************************************************/
        void Submit(MeshHandle mesh, const glm::mat4& model, const WaterSettings& settings, bool sceneHidden);

        /// Forgets everything submitted. Call once the frame has been drawn.
        void ClearSubmitted() { m_submitted.clear(); }

        /*********************************************************************
         \brief  The submitted surfaces one view can see (frustum-culled by
                 their mesh's bounds; meshes still uploading are skipped).
         \param[in] camera  The view's camera.
         \param[out] out    Receives the view's water draws (cleared first).
         \param[in] hideSceneHidden  true (an editor Scene view): surfaces
                            submitted as sceneHidden are left out.
         *********************************************************************/
        void Prepare(const Camera& camera, std::vector<WaterDraw>& out, bool hideSceneHidden) const;

        /*********************************************************************
         \brief  Draws the surfaces into the rendering pass that is already
                 open (ScenePass's second half).
         \param[in] cmd           Command buffer, inside the scene pass.
         \param[in] samples       MSAA samples of the target being drawn into.
         \param[in] frameSet      The view's set 0.
         \param[in] sceneCopySet  The view's PostTargets::waterSet (the
                                  copies of its scene colour and depth).
         \param[in] draws         From Prepare().
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, uint32_t samples, vk::DescriptorSet frameSet, vk::DescriptorSet sceneCopySet,
                    const std::vector<WaterDraw>& draws) const;

    private:
        const RenderResources& m_resources;

        std::array<vk::raii::Pipeline, 3> m_pipelines{ nullptr, nullptr, nullptr };   // 1, 2, 4 samples
        vk::raii::PipelineLayout          m_layout = nullptr;

        // This frame's surfaces: the draw (mesh + push constant, colours already linear) and the
        // transform its bounds are culled with.
        struct Submitted { WaterDraw draw; glm::mat4 model{ 1.0f }; bool sceneHidden = false; };
        std::vector<Submitted> m_submitted;
    };

}   // NAMESPACE CRUCIBLE
