/*************************************************************************
* \file:         ScriptAnimatorBinding.h
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          30 October 2026
* \version       1.0
* \brief         Interface that lets C# scripts set Animator parameters.
*
* \details
*    This is the "adapter" half of the Animator binding:
*
*      C# Animator.SetBool(...)                    (ManagedScripts/Runtime/Animator.cs)
*        -> internal call ManagedAnimatorParameter  (Bindings/AnimatorBindings.cpp)
*        -> IScriptAnimatorBinding::SetParameter    (this interface)
*        -> AnimatedModelSystem::SetParameter       (the implementation)
*
*    The interface contains no Mono types, so the animation system never
*    needs the Mono SDK. Core registers the implementation with
*    ScriptRuntime::SetAnimatorBinding() once the animation system exists.
*    Use this file as the template for future bindings (Transform, Audio...).
*
**************************************************************************/
#pragma once
#include <cstdint>
#include <string>

namespace Crucible
{
	/*********************************************************************
	 \brief  Which Animator call the script made. The integer values match
	         the "operation" numbers used in Animator.cs (0..4).
	 *********************************************************************/
	enum class ScriptAnimatorOperation
	{
		Bool,
		Float,
		Integer,
		Trigger,
		ResetTrigger
	};

	/*********************************************************************
	 \brief  One parameter write requested by a script, already converted
	         to native types (UTF-8 name, plain numbers).
	 *********************************************************************/
	struct ScriptAnimatorCommand
	{
		std::string name;                                                  // parameter name in the controller
		ScriptAnimatorOperation operation = ScriptAnimatorOperation::Bool; // which setter was called
		int32_t integer = 0;                                               // value for Bool (0/1) and Integer
		float number = 0;                                                  // value for Float
	};

	// SDK-free, non-owning adapter. The animation system validates owner lifetime
	// and queues parameter writes for its next pose evaluation.
	class IScriptAnimatorBinding
	{
	public:
		virtual ~IScriptAnimatorBinding() = default;

		/*********************************************************************
		 \brief  Queues an Animator parameter write for an entity.
		 \param  index, generation  The script's entity (both are needed so a
		                            destroyed-and-reused entity slot is rejected).
		 \param  command            The parameter name, type and value.
		 \return true if the entity is alive, has an animator with a matching
		         parameter, and the write was queued; false otherwise.
		 *********************************************************************/
		virtual bool SetParameter(uint32_t index, uint32_t generation,
		                          const ScriptAnimatorCommand& command) = 0;
	};
}
