#pragma once
/*************************************************************************
* \file:         TextureHandle.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Opaque handle type identifying a 2D texture uploaded to the GPU.
*
* \details
*                Declares TextureHandle, a cheap-to-copy integer handle
*                returned by Renderer::CreateTexture()/CreateTextureFromMemory()/
*                CreateTextureFromPixels() (see RenderSystem.h), and the
*                TexturePixelFormat the last one takes. Mirrors MeshHandle.h's pattern
*                exactly, including why it's split into its own zero-
*                dependency header: MaterialHandle (see MaterialHandle.h)
*                is referenced from ECS/Components/Components.h, which must
*                not transitively pull in Vulkan.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>

namespace Crucible {

    /*************************************************************************
     \brief
        Opaque handle to a 2D texture living on the GPU (see RenderSystem.h's
        Renderer::CreateTexture()).

     \details
        Cheap to copy; this is what MaterialData (RenderTypes.h) stores for
        each of its five PBR texture slots.
     *************************************************************************/
    struct TextureHandle {
        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;

        /*********************************************************************
         \brief  Whether this handle refers to anything other than the
                 reserved invalid value.
         \return true if id is not TextureHandle::Invalid.
         *********************************************************************/
        bool IsValid() const { return id != Invalid; }

        /*********************************************************************
         \brief  Compares two handles by their underlying id.
         \param[in] a First handle.
         \param[in] b Second handle.
         \return true if both handles carry the same id.
         *********************************************************************/
        friend bool operator==(TextureHandle a, TextureHandle b) { return a.id == b.id; }

        /*********************************************************************
         \brief  Orders two handles by their underlying id (e.g. for use as
                 a std::map key).
         \param[in] a First handle.
         \param[in] b Second handle.
         \return true if a's id is less than b's id.
         *********************************************************************/
        friend bool operator<(TextureHandle a, TextureHandle b)  { return a.id <  b.id; }
    };

    /*************************************************************************
     \brief
        How the texels given to Renderer::CreateTextureFromPixels() are laid
        out, and how shaders see them.

     \details
        Rows are tightly packed, top row first.
     *************************************************************************/
    enum class TexturePixelFormat : uint8_t {
        RGBA8_sRGB,   ///< 4 bytes per texel, colour images (icons, sprites, UI art). Read back as linear colour, like CreateTexture(path, true).
        RGBA8,        ///< 4 bytes per texel, data stored as is (MSDF font atlases, masks). Like CreateTexture(path, false).
        Alpha8,       ///< 1 byte per texel, read back as (1, 1, 1, value): font coverage or SDF atlases. Not for material slots.
    };

}   // NAMESPACE CRUCIBLE
