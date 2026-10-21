/*************************************************************************
  \file       ToonOutlinePass.h
  \brief
    The toon outline pass: thin lines along silhouettes and sharp creases,
    in a darker shade of the surface they sit on (the Wanderburg look).
  \details
    Runs after the scene pass and before bloom. Reads the scene pass's
    second output (PostTargets::lineData: 1 / view depth, the surface's
    direction, and how strong a line may be there - the material's Outline
    x fog x a breakup pattern fixed to the surface) and multiplies a
    darkening factor into the view's HDR image (Post.slang's
    toonOutlineFrag: silhouettes from a two-sided depth test that ignores
    folds, including the half-covered MSAA edge pixel; creases from the
    normals, lighter). Not related to OutlinePass, the editor's selection
    outline.
*************************************************************************/
#pragma once

#include "Graphics/ViewTargets.h"
#include "Graphics/RenderTypes.h"

namespace Crucible {

    class VulkanContext;

    class ToonOutlinePass {
    public:
        /*********************************************************************
         \brief  Creates the pass's pipeline and its layout. Call once, before
                 recording.
         \param[in] context           The VulkanContext to create them through.
         \param[in] textureSetLayout  Layout of a set with one sampled texture.
         \param[in] colourFormat      Format of the HDR image the lines go into.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout textureSetLayout, vk::Format colourFormat);

        /*********************************************************************
         \brief  Darkens one view's HDR image along its lines.
         \param[in] cmd          Command buffer, with no rendering pass open.
         \param[in,out] targets  The view's targets: hdrImage and lineData as
                                 the scene pass left them (shader-readable);
                                 hdrImage is shader-readable again after.
         \param[in] env          The scene's line style (toonLine* settings).
         *********************************************************************/
        void Record(vk::CommandBuffer cmd, PostTargets& targets, const EnvironmentSettings& env) const;

    private:
        vk::raii::Pipeline       m_pipeline = nullptr;   // fullscreen, multiplied into the target
        vk::raii::PipelineLayout m_layout = nullptr;     // one texture (line data) + PostPush
    };

}   // NAMESPACE CRUCIBLE
