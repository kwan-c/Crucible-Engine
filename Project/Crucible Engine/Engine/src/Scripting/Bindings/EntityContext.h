/*************************************************************************
* \file:         EntityContext.h
* \author:       <author>
* \par:          Email: <email>
* \date          1 November 2026
* \version       1.0
* \brief         INTERNAL: the world, Play state and scene that entity
*                bindings (Transform, RigidBody, Entity, ...) work against.
*
* \details
*    Set once by ScriptBindings::SetWorld (through
*    ScriptRuntime::SetWorldBinding). Every binding that receives an
*    (index, generation) from C# should call ResolveEntity before touching
*    the world, so destroyed entities and calls outside Play are rejected.
*
*    The pointers are thread_local: a call from any thread other than the
*    one that set them (e.g. a GC finalizer) sees nullptr and fails.
*
*    Only include this from .cpp files inside Engine/src/Scripting.
*
**************************************************************************/
#pragma once
#include <cstdint>
#include "ECS/Core/ECSCore.h"   // Entity

namespace Crucible
{
	class World;

	namespace ScriptBindings::EntityContext
	{
		/*********************************************************************
		 \brief  The world scripts act on, or nullptr before SetWorld().
		 *********************************************************************/
		World* GetWorld();

		/*********************************************************************
		 \brief  Turns the (index, generation) a script passed in into a live
		         Entity. Fails outside a Play session, before SetWorld(), or if
		         the entity was destroyed (its generation no longer matches,
		         e.g. after a scene reload).
		 *********************************************************************/
		bool ResolveEntity(int32_t index, int32_t generation, Entity& entity);

		/*********************************************************************
		 \brief  The live entity saved under a scene id (Scene::EnsureId), for
		         Inspector-assigned entity fields.
		 \return false for id 0, an id the open scene does not have, or an
		         entity that is no longer alive.
		 \note   Searches the scene's id map - call it when a script is created
		         or a field is edited, not every frame.
		 *********************************************************************/
		bool FromSceneId(uint64_t id, Entity& entity);

		/*********************************************************************
		 \brief  The scene id an entity is saved under (Scene::IdOf), or 0 if it
		         has none yet (e.g. spawned during Play). Never issues a new id.
		 *********************************************************************/
		uint64_t SceneIdOf(Entity entity);

		/*********************************************************************
		 \brief  Finds the GC handle of the C# script running on an entity
		         (0 = none). `context` is passed back unchanged.
		 *********************************************************************/
		using ScriptLookup = uint32_t (*)(void* context, Entity entity);

		/*********************************************************************
		 \brief  Installs (or clears, with nullptr) the script lookup.
		         EntityScriptSystem sets it in Init and clears it in Shutdown.
		 *********************************************************************/
		void SetScriptLookup(ScriptLookup lookup, void* context);

		/*********************************************************************
		 \brief  GC handle of the script on `entity`, or 0 if it has none, it
		         failed, or no lookup is installed. The script may not have had
		         OnCreate yet (see EntityScriptSystem step 5).
		 *********************************************************************/
		uint32_t FindScriptHandle(Entity entity);
	}
}
