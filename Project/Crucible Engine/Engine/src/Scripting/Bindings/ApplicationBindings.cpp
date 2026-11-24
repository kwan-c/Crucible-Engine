/*************************************************************************
* \file:         ApplicationBindings.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          11 November 2026
* \version       1.0
* \brief         Internal call behind the C# Application class
*                (ManagedScripts/Runtime/Application.cs).
*
* \details
*    Application.IsPlaying is EntityContext::InPlaySession(): true while
*    Playing or Paused, false in an Inspector [EditorButton] call.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/Bindings/EntityContext.h"

#if CRUCIBLE_WITH_MONO

#include <cstdint>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		int32_t ManagedApplicationIsPlaying() noexcept
		{
			try
			{
				return EntityContext::InPlaySession() ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}
	}

	void RegisterApplicationBindings()
	{
		mono_add_internal_call("Crucible.Application::IsPlayingInternal", reinterpret_cast<const void*>(&ManagedApplicationIsPlaying));
	}
}

#endif
