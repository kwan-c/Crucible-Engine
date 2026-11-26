/*************************************************************************
* \file:         RagdollBindings.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          23 November 2026
* \version       1.2
* \brief         Internal call behind the C# Ragdoll class
*                (ManagedScripts/Runtime/Ragdoll.cs).
*
* \details
*    C# Ragdoll.Spawn(entity, velocity, owner) -> ManagedRagdollSpawn (here)
*      -> ECSRagdollSystem::Active()->RequestRagdoll, which checks the model
*         at once and builds the ragdoll at the next PreRender (the same
*         frame, for a script's Update). C# Ragdoll.Count ->
*         ManagedRagdollCount.
*
* \history
*    -v1.0       Ragdoll.Spawn(entity, velocity)
*    -v1.1       owner (removed with the ragdoll); Ragdoll.Count
*    -v1.2       passes the scripts' World, so RequestRagdoll can refuse a
*                model it cannot build while the caller can still topple
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#if CRUCIBLE_WITH_MONO
// STL
#include <cmath>

// Crucible engine
#include "ECS/ECSRagdollSystem.h"
#include "Scripting/Bindings/EntityContext.h"

#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;   // ScriptVec3, the C# Vector3's layout

		/*********************************************************************
		 \brief  C# Ragdoll.Spawn. Checks the velocity is a real number and the
		         entity is alive, then queues the ragdoll.
		 \return 1 if queued; 0 with no ragdoll system, a dead entity, a NaN or
		         infinite velocity, or one already asked for (int, not bool, so
		         the size is unambiguous across the C#/C++ boundary).
		 *********************************************************************/
		int32_t ManagedRagdollSpawn(int32_t index, int32_t generation, const ScriptVec3* velocity,
		                            int32_t ownerIndex, int32_t ownerGeneration) noexcept
		{
			try
			{
				ECSRagdollSystem* ragdolls = ECSRagdollSystem::Active();
				const World* world = EntityContext::GetWorld();   // the system checks the model in it now
				if (!ragdolls || !world || !velocity) return 0;
				if (!std::isfinite(velocity->x) || !std::isfinite(velocity->y) || !std::isfinite(velocity->z)) return 0;
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity)) return 0;
				// No owner (-1), or one already gone: the model itself is removed with the ragdoll.
				Entity owner = kNullEntity;
				if (ownerIndex >= 0 && !EntityContext::ResolveEntity(ownerIndex, ownerGeneration, owner)) owner = kNullEntity;
				return ragdolls->RequestRagdoll(*world, entity, Math::vec3(velocity->x, velocity->y, velocity->z), owner) ? 1 : 0;
			}
			catch (...)
			{
				return 0;   // a C++ exception must never cross into Mono
			}
		}   // ManagedRagdollSpawn()

		/*********************************************************************
		 \brief  C# Ragdoll.Count: the ragdolls alive now (0 with no system).
		 *********************************************************************/
		int32_t ManagedRagdollCount() noexcept
		{
			const ECSRagdollSystem* ragdolls = ECSRagdollSystem::Active();
			return ragdolls ? static_cast<int32_t>(ragdolls->RagdollCount()) : 0;
		}   // ManagedRagdollCount()
	}   // ANONYMOUS NAMESPACE

	void RegisterRagdollBindings()
	{
		mono_add_internal_call("Crucible.Ragdoll::SpawnInternal", reinterpret_cast<const void*>(ManagedRagdollSpawn));
		mono_add_internal_call("Crucible.Ragdoll::CountInternal", reinterpret_cast<const void*>(ManagedRagdollCount));
	}   // RegisterRagdollBindings()

}   // NAMESPACE CRUCIBLE::SCRIPTBINDINGS
#endif
