#pragma once
/*************************************************************************
* \file:         MaterialHandle.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Opaque handle type identifying a material uploaded to the GPU.
*
* \details
*                Declares MaterialHandle: a bundle of texture bindings
*                (albedo/normal/metallic-roughness/emissive/occlusion) plus
*                scalar factors, exposed to the ECS as this one handle (see
*                Renderer::CreateMaterial in RenderSystem.h). Mirrors
*                MeshHandle.h/TextureHandle.h's pattern; see
*                ECS/Components/Components.h's MeshRenderer::material for
*                the ECS-visible use of this type.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>

namespace Crucible {

    /*************************************************************************
     \brief
        Opaque handle to a Material living on the GPU (see RenderSystem.h's
        Renderer::CreateMaterial()).

     \details
        Cheap to copy; this is what ECS components (MeshRenderer,
        Components.h) store instead of any real renderer type.
     *************************************************************************/
    struct MaterialHandle {
        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;

        /*********************************************************************
         \brief  Whether this handle refers to anything other than the
                 reserved invalid value.
         \return true if id is not MaterialHandle::Invalid.
         *********************************************************************/
        bool IsValid() const { return id != Invalid; }

        /*********************************************************************
         \brief  Compares two handles by their underlying id.
         \param[in] a First handle.
         \param[in] b Second handle.
         \return true if both handles carry the same id.
         *********************************************************************/
        friend bool operator==(MaterialHandle a, MaterialHandle b) { return a.id == b.id; }

        /*********************************************************************
         \brief  Orders two handles by their underlying id (e.g. for use as
                 a std::map key).
         \param[in] a First handle.
         \param[in] b Second handle.
         \return true if a's id is less than b's id.
         *********************************************************************/
        friend bool operator<(MaterialHandle a, MaterialHandle b)  { return a.id <  b.id; }
    };

}   // NAMESPACE CRUCIBLE
