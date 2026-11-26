/*************************************************************************
* \file:         GraphicsBindings.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          17 November 2026
* \version       1.0
* \brief         Internal calls behind the C# Graphics class
*                (ManagedScripts/Runtime/Graphics.cs).
*
* \details
*    PendingShaderBuilds is Renderer::GetPendingMaterialShaderBuilds: the
*    material shaders whose pipelines are still being built in the
*    background. The renderer is the one the C# Camera class is given
*    (SetRenderer, CameraBindings.cpp; CurrentRenderer).
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Graphics/RenderSystem.h"
#if CRUCIBLE_WITH_MONO
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		/*********************************************************************
		 \brief  C# Graphics.PendingShaderBuilds.
		 \return 0 (and nothing written) when there is no renderer.
		 *********************************************************************/
		int32_t ManagedGraphicsGetPendingShaderBuilds(int32_t* out) noexcept
		{
			const Renderer* renderer = CurrentRenderer();
			if (!out || !renderer)
				return 0;
			*out = static_cast<int32_t>(renderer->GetPendingMaterialShaderBuilds());
			return 1;
		}
	}

	void RegisterGraphicsBindings()
	{
		mono_add_internal_call("Crucible.Graphics::GetPendingShaderBuildsInternal",
		                       reinterpret_cast<const void*>(ManagedGraphicsGetPendingShaderBuilds));
	}
}
#endif
