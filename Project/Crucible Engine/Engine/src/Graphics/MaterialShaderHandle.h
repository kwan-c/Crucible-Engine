#pragma once
/*************************************************************************
* \file:         MaterialShaderHandle.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          16 November 2026
* \version       1.0
* \brief
*                Opaque handle type identifying a mesh shader a material is
*                drawn with.
*
* \details
*                Declares MaterialShaderHandle. A material (MaterialData)
*                names the shader that draws it with one of these; many
*                materials share one shader. The default-constructed
*                (Invalid) handle means the built-in shader
*                (DefaultShader.slang), so a material that never mentions a
*                shader is drawn as it always was. Mirrors MaterialHandle.h's
*                pattern; see Renderer::LoadMaterialShader in RenderSystem.h
*                and MaterialShaders.h.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>

namespace Crucible {

    /*************************************************************************
     \brief
        Opaque handle to a mesh shader (see Renderer::LoadMaterialShader()).

     \details
        Cheap to copy. Invalid = the built-in shader.
     *************************************************************************/
    struct MaterialShaderHandle {

        static constexpr uint32_t Invalid = 0xFFFFFFFFu;
        uint32_t id = Invalid;

        /*********************************************************************
         \brief  Whether this handle names a shader of its own (false = the
                 built-in shader).
         \return true if id is not MaterialShaderHandle::Invalid.
         *********************************************************************/
        bool IsValid() const { return id != Invalid; }

        /*********************************************************************
         \brief  Compares two handles by their underlying id.
         \param[in] a First handle.
         \param[in] b Second handle.
         \return true if both handles carry the same id.
         *********************************************************************/
        friend bool operator==(MaterialShaderHandle a, MaterialShaderHandle b) { return a.id == b.id; }

        /*********************************************************************
         \brief  Orders two handles by their underlying id.
         \param[in] a First handle.
         \param[in] b Second handle.
         \return true if a's id is less than b's id.
         *********************************************************************/
        friend bool operator<(MaterialShaderHandle a, MaterialShaderHandle b)  { return a.id <  b.id; }

    };

}   // NAMESPACE CRUCIBLE
