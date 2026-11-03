/*************************************************************************
* \file:         AnimatorBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          30 October 2026
* \version       1.0
* \brief         Internal call behind the C# Animator class
*                (ManagedScripts/Runtime/Animator.cs).
*
* \details
*    C# Animator.SetBool(...) -> ManagedAnimatorParameter (here)
*      -> IScriptAnimatorBinding::SetParameter (ScriptAnimatorBinding.h)
*      -> AnimatedModelSystem, which queues the write for its next update.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/ScriptAnimatorBinding.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		// The adapter set by SetAnimator(). thread_local, so calls arriving on any
		// other thread (e.g. a GC finalizer thread) see nullptr and do nothing.
		thread_local IScriptAnimatorBinding* animatorBinding = nullptr;

		/*********************************************************************
		 \brief  C# Animator.SetBool/SetFloat/SetInteger/SetTrigger/ResetTrigger end
		         up here. Validates the arguments, converts the name to UTF-8 and
		         forwards to the animation system's IScriptAnimatorBinding.
		 \return 1 if the write was queued, 0 otherwise (int, not bool, so the
		         size is unambiguous across the C#/C++ boundary).
		 *********************************************************************/
		int32_t ManagedAnimatorParameter(int32_t index, int32_t generation, MonoString* name,
		                                 int32_t operation, int32_t integer, float number) noexcept
		{
			try
			{
				// Thread-local registration rejects worker/finalizer threads before
				// touching a managed object or the ECS. All native exceptions stay here.
				if (!animatorBinding || !name || operation < 0 || operation > 4 || !std::isfinite(number))
					return 0;
				if (mono_string_length(name) > 1024)
					return 0;
				GcRoot root(reinterpret_cast<MonoObject*>(name));
				ScriptAnimatorCommand command;
				if (!FromManagedString(reinterpret_cast<MonoString*>(root.Get()), command.name))
					return 0;
				command.operation = static_cast<ScriptAnimatorOperation>(operation);
				command.integer = integer;
				command.number = number;
				return animatorBinding->SetParameter(static_cast<uint32_t>(index),
				                                     static_cast<uint32_t>(generation), command)
				           ? 1
				           : 0;
			}
			catch (...)
			{
				return 0;
			}
		}
	}

	void SetAnimator(IScriptAnimatorBinding* binding)
	{
		animatorBinding = binding;
	}

	void RegisterAnimatorBindings()
	{
		mono_add_internal_call("Crucible.Animator::SetParameterInternal",
		                       reinterpret_cast<const void*>(ManagedAnimatorParameter));
	}
}
#endif
