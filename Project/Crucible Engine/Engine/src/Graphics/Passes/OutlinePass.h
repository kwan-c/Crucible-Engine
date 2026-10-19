/*************************************************************************
  \file       OutlinePass.h
  \brief
    The selection outline, in two parts: draw the selected object as a flat
    mask (OutlineMask.slang), then, once the view's picture is finished,
    draw a coloured line wherever the mask has an edge.
  \details
    The mask image and its descriptor set are still the viewport's. Owns its pipelines.
*************************************************************************/
#pragma once

#include "Graphics/RenderTypes.h"
#include "Graphics/DrawList.h"

namespace Crucible {

    class VulkanContext;
    class RenderResources;

    class OutlinePass {
    public:
        explicit OutlinePass(const RenderResources& resources) : m_resources(resources) {}

        /// Layout of the set the composite reads the mask through (binding 0 = the mask image).
        /// The Renderer allocates one such set per viewport. Valid after Create().
        const vk::raii::DescriptorSetLayout& GetMaskSetLayout() const { return m_outlineDescriptorSetLayout; }

        /*********************************************************************
         \brief  Creates the pass's pipelines and their layouts. Call once,
                 before recording.
         \param[in] context         The VulkanContext to create them through.
         \param[in] frameSetLayout     Layout of set 0 (the per-frame data).
         \param[in] materialSetLayout  Layout of a material's set (set 1 of the
                                       mask, for cut-out materials).
         \param[in] colourFormat       Format of the view's final colour image.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DescriptorSetLayout frameSetLayout, vk::DescriptorSetLayout materialSetLayout, vk::Format colourFormat);

        /*********************************************************************
         \brief  Draws the selected object into the mask image, barriers
                 included. Leaves the mask ready to sample.
         \param[in] cmd        Command buffer, with no rendering pass open.
         \param[in] maskImage  The view's mask image, and maskView its view.
         \param[in] extent     Size of the view.
         \param[in] frameSet   The view's set 0 (camera and instance data).
         \param[in] meshes     The selected object's meshes.
         *********************************************************************/
        void RecordMask(vk::CommandBuffer cmd, vk::Image maskImage, vk::ImageView maskView, vk::Extent2D extent,
                        vk::DescriptorSet frameSet, const std::vector<DrawBatch>& meshes) const;

        /*********************************************************************
         \brief  Blends the outline over the view's finished picture.
         \param[in] cmd          Command buffer, with no rendering pass open.
         \param[in] colourImage  The view's final colour image (still in the
                                colour attachment layout), and colourView its view.
         \param[in] extent       Size of the view.
         \param[in] outlineSet   Descriptor set that samples the mask.
         \param[in] style        Colour and thickness.
         *********************************************************************/
        void RecordComposite(vk::CommandBuffer cmd, vk::Image colourImage, vk::ImageView colourView, vk::Extent2D extent,
                             vk::DescriptorSet outlineSet, const OutlineCompositePush& style) const;

    private:
        vk::raii::Pipeline            m_outlineMaskPipeline = nullptr;        // selection -> R8 mask
        vk::raii::Pipeline            m_outlineMaskCutoutPipeline = nullptr;  // the same for cut-out materials
        vk::raii::PipelineLayout      m_outlineMaskLayout = nullptr;          // set 0, set 1 (material), CutoutPush
        vk::raii::Pipeline            m_outlineCompositePipeline = nullptr;   // mask -> coloured edge
        vk::raii::PipelineLayout      m_outlineCompositeLayout = nullptr;     // mask set + push constant
        vk::raii::DescriptorSetLayout m_outlineDescriptorSetLayout = nullptr; // binding 0 = mask image
        const RenderResources& m_resources;   // the meshes it draws
    };

}   // NAMESPACE CRUCIBLE
