/*************************************************************************
* \file:         EntityContext.cpp
* \author:       <author>
* \par:          Email: <email>
* \date          1 November 2026
* \version       1.0
* \brief         Implementation - see EntityContext.h. Also defines
*                ScriptBindings::SetWorld.
*
**************************************************************************/
#include "Scripting/Bindings/EntityContext.h"
#include "Scripting/ScriptBindings.h"
#include "ECS/World.h"
#include "Scene/SceneManager.h"
#include "Systems/PlayMode.h"
#if CRUCIBLE_WITH_MONO

namespace Crucible::ScriptBindings
{
	namespace
	{
		// Set by SetWorld() from Core (main thread).
		thread_local World* world = nullptr;
		thread_local PlayMode* play = nullptr;
		thread_local SceneManager* scenes = nullptr;

		// Set by EntityScriptSystem. Plain statics (not thread_local): it may be
		// installed from whichever thread initializes systems. Every caller goes
		// through ResolveEntity first, which already fails off the main thread.
		EntityContext::ScriptLookup scriptLookup = nullptr;
		void* scriptLookupContext = nullptr;
	}

	void SetWorld(World* newWorld, PlayMode* newPlay, SceneManager* newScenes)
	{
		world = newWorld;
		play = newPlay;
		scenes = newScenes;
	}

	namespace EntityContext
	{
		World* GetWorld()
		{
			return world;
		}

		bool ResolveEntity(int32_t index, int32_t generation, Entity& entity)
		{
			if (!world || !play || !play->InSession())
				return false;
			entity = Entity{static_cast<uint32_t>(index), static_cast<uint32_t>(generation)};
			return world->IsAlive(entity);
		}

		bool FromSceneId(uint64_t id, Entity& entity)
		{
			if (!world || !scenes || id == 0)
				return false;
			entity = scenes->Current().EntityOf(xresource::instance_guid{id});
			return entity.IsValid() && world->IsAlive(entity);
		}

		uint64_t SceneIdOf(Entity entity)
		{
			if (!world || !scenes || !world->IsAlive(entity))
				return 0;
			return scenes->Current().IdOf(entity).m_Value;
		}

		void SetScriptLookup(ScriptLookup lookup, void* context)
		{
			scriptLookup = lookup;
			scriptLookupContext = context;
		}

		uint32_t FindScriptHandle(Entity entity)
		{
			return scriptLookup ? scriptLookup(scriptLookupContext, entity) : 0;
		}
	}
}
#endif
