#pragma once
/*************************************************************************
* \file:         LightTypes.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Zero-dependency light enums shared by the ECS Light
*                component and the Renderer.
*
* \details
*                Split into its own header for the same reason as
*                MeshHandle.h/MaterialHandle.h: ECS/Components/Components.h
*                needs LightType, and must not transitively pull in Vulkan
*                (RenderTypes.h does).
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>

namespace Crucible {

    /*************************************************************************
     \brief
        The kind of light a Light component (or a Renderer::SubmitLight
        call) describes.

     \details
        The numeric values are also what the shader sees (GpuLight::
        directionType.w in RenderTypes.h, compared against the matching
        constants in DefaultShader2.slang) - do not reorder.
          - Directional: infinitely far away (the sun). Only direction
                         matters; position and range are ignored.
          - Point:       emits equally in every direction from a position,
                         falling off with distance and reaching zero at
                         `range`.
          - Spot:        a point light restricted to a cone around its
                         direction (inner/outer cone angles).
     *************************************************************************/
    enum class LightType : std::uint8_t {
        Directional = 0,
        Point       = 1,
        Spot        = 2
    };

}   // NAMESPACE CRUCIBLE
