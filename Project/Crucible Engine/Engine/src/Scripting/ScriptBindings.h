/*************************************************************************
* \file:         ScriptBindings.h
* \author:       <author>
* \par:          Email: <email>
* \date          30 October 2026
* \version       1.0
* \brief         Registers the C++ functions that C# scripts can call
*                ("internal calls").
*
* \details
*    How a binding works:
*      1. C# declares an `extern` method marked
*         [MethodImpl(MethodImplOptions.InternalCall)] in ManagedScripts/Runtime.
*      2. A C++ function with matching parameters lives in Scripting/Bindings/.
*      3. mono_add_internal_call("Namespace.Class::Method", function) links
*         them. Each file in Bindings/ does this in its RegisterXxxBindings().
*
*    To add a binding (e.g. Transform):
*      - create Bindings/TransformBindings.cpp with RegisterTransformBindings()
*      - declare RegisterTransformBindings() below
*      - call it from RegisterAll() in ScriptBindings.cpp
*
*    No Mono types appear in this header.
*
**************************************************************************/
#pragma once

namespace Crucible
{
	class IScriptAnimatorBinding;
	class World;
	class PlayMode;

	namespace ScriptBindings
	{
		/*********************************************************************
		 \brief  Registers every internal call with Mono. Called once by
		         ScriptRuntime::Initialize, after the JIT starts and before any
		         user DLL is loaded.
		 *********************************************************************/
		void RegisterAll();

		/*********************************************************************
		 \brief  Disconnects every engine adapter (so C# calls safely return
		         "failed"). Called by ScriptRuntime::Shutdown.
		 *********************************************************************/
		void ClearAll();

		/*********************************************************************
		 \brief  Sets (or clears, with nullptr) the animation-system adapter used
		         by the C# Animator API. Non-owning; main thread only.
		         Called through ScriptRuntime::SetAnimatorBinding.
		 *********************************************************************/
		void SetAnimator(IScriptAnimatorBinding* binding);

		/*********************************************************************
		 \brief  Gives entity bindings (Transform, ...) access to the ECS and Play
		         state. nullptr clears it. Non-owning; main thread only.
		         Called through ScriptRuntime::SetWorldBinding. Defined in
		         Bindings/TransformBindings.cpp.
		 *********************************************************************/
		void SetWorld(World* world, PlayMode* play);

		// --- Per-feature registration, called by RegisterAll() ---
		void RegisterLogBindings();      // Bindings/LogBindings.cpp      (C# Log)
		void RegisterAnimatorBindings(); // Bindings/AnimatorBindings.cpp (C# Animator)
		void RegisterTransformBindings(); // Bindings/TransformBindings.cpp (C# Transform)
	}
}
