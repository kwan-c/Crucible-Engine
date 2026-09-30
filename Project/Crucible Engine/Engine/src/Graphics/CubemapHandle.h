#pragma once
/*************************************************************************
* \file:         CubemapHandle.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Opaque handle type identifying a cubemap uploaded to the GPU.
*
* \details
*                Declares CubemapHandle, a cheap-to-copy integer handle
*                returned by Renderer::CreateCubemap()/
*                CreateCubemapFromEquirect() (see RenderSystem.h). Kept a
*                distinct type from TextureHandle (rather than a flag on a
*                TextureHandle) so a cubemap can never be bound into a 2D
*                material texture slot, or a 2D texture used as a skybox,
*                by accident - that mismatch would be a compile error
*                instead of a Vulkan validation error. Zero-dependency for
*                the same reason as the other handle headers: the ECS
*                Skybox component (Components.h) stores one.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>

namespace Crucible {

    /*************************************************************************
     \brief
        Opaque handle to a cubemap living on the GPU (see RenderSystem.h's
        Renderer::CreateCubemap()).

     \details
        Cheap to copy; this is what the ECS Skybox component and
        EnvironmentSettings (RenderTypes.h) store.
     *************************************************************************/
    struct CubemapHandle {
        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;

        /*********************************************************************
         \brief  Whether this handle refers to anything other than the
                 reserved invalid value.
         \return true if id is not CubemapHandle::Invalid.
         *********************************************************************/
        bool IsValid() const { return id != Invalid; }

        /*********************************************************************
         \brief  Compares two handles by their underlying id.
         \param[in] a First handle.
         \param[in] b Second handle.
         \return true if both handles carry the same id.
         *********************************************************************/
        friend bool operator==(CubemapHandle a, CubemapHandle b) { return a.id == b.id; }

        /*********************************************************************
         \brief  Orders two handles by their underlying id (e.g. for use as
                 a std::map key).
         \param[in] a First handle.
         \param[in] b Second handle.
         \return true if a's id is less than b's id.
         *********************************************************************/
        friend bool operator<(CubemapHandle a, CubemapHandle b)  { return a.id <  b.id; }
    };

}   // NAMESPACE CRUCIBLE
