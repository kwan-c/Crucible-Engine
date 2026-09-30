#pragma once
/*************************************************************************
* \file:         MeshHandle.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Opaque handle type identifying a mesh uploaded to the GPU.
*
* \details
*                Declares MeshHandle, a cheap-to-copy integer handle
*                returned by Renderer::CreateMesh()/CreateMeshAsync()
*                (see RenderSystem.h) and stored by ECS components such as
*                MeshRenderer (ECS/Components/Components.h).
*
*                Deliberately split out from RenderTypes.h into its own,
*                dependency-free header: RenderTypes.h pulls in
*                Vulkan/VulkanPipeline.h (and, through it, the Vulkan RAII
*                headers) for the Vertex/MeshData types, while
*                ECS/Components/Components.h needs to reference MeshHandle
*                WITHOUT dragging any of that into every ECS header that
*                includes Components.h. This header has zero renderer
*                dependencies - just an integer - so it is safe for the ECS
*                to depend on directly.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>

namespace Crucible {

    /*************************************************************************
     \brief
        Opaque handle to a mesh living on the GPU (see RenderSystem.h's
        CreateMesh()/CreateMeshAsync()).

     \details
        Cheap to copy; this is what ECS components (MeshRenderer,
        Components.h) store instead of any real renderer type.
     *************************************************************************/
    struct MeshHandle {
        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;

        /*********************************************************************
         \brief  Whether this handle refers to anything other than the
                 reserved invalid value.
         \return true if id is not MeshHandle::Invalid.
         *********************************************************************/
        bool IsValid() const { return id != Invalid; }

        /*********************************************************************
         \brief  Compares two handles by their underlying id.
         \param[in] a First handle.
         \param[in] b Second handle.
         \return true if both handles carry the same id.
         *********************************************************************/
        friend bool operator==(MeshHandle a, MeshHandle b) { return a.id == b.id; }

        /*********************************************************************
         \brief  Orders two handles by their underlying id (e.g. for use as
                 a std::map key).
         \param[in] a First handle.
         \param[in] b Second handle.
         \return true if a's id is less than b's id.
         *********************************************************************/
        friend bool operator<(MeshHandle a, MeshHandle b)  { return a.id <  b.id; }
    };

}   // NAMESPACE CRUCIBLE
