/*************************************************************************
* \file:         TimeBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the C# Time class
*                (ManagedScripts/Runtime/Time.cs).
*
* \details
*    DeltaTime is the World's frame delta (the same value OnUpdate gets).
*    Time and FrameCount come from the play clock in EntityContext, which
*    EntityScriptSystem advances once per Playing frame and resets when a
*    Play session starts or ends.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#if CRUCIBLE_WITH_MONO
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		/*********************************************************************
		 \brief  C# Time.DeltaTime: seconds since the previous frame.
		 *********************************************************************/
		int32_t ManagedTimeGetDeltaTime(float* out) noexcept
		{
			try
			{
				World* world = EntityContext::GetWorld();
				if (!out || !world)
					return 0;
				*out = world->DeltaTime();
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Time.ElapsedTime: seconds of Playing time since Play started.
		 *********************************************************************/
		int32_t ManagedTimeGetTime(double* out) noexcept
		{
			if (!out)
				return 0;
			*out = EntityContext::PlayTime();
			return 1;
		}

		/*********************************************************************
		 \brief  C# Time.FrameCount: Playing frames since Play started.
		 *********************************************************************/
		int32_t ManagedTimeGetFrameCount(int32_t* out) noexcept
		{
			if (!out)
				return 0;
			*out = EntityContext::PlayFrameCount();
			return 1;
		}
	}

	void RegisterTimeBindings()
	{
		mono_add_internal_call("Crucible.Time::GetDeltaTimeInternal", reinterpret_cast<const void*>(ManagedTimeGetDeltaTime));
		mono_add_internal_call("Crucible.Time::GetTimeInternal", reinterpret_cast<const void*>(ManagedTimeGetTime));
		mono_add_internal_call("Crucible.Time::GetFrameCountInternal", reinterpret_cast<const void*>(ManagedTimeGetFrameCount));
	}
}
#endif
