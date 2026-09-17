#pragma once

#include "EngineExport.h"

//std libs
#include<cstdint>
#include <cassert>
#include <utility>

//header files
#include "Core/ECSCore.h"          // Entity, EntityManager, IndexedArray
#include "Components/ComponentRegistry.h" // ComponentRegistry
#include "Components/ComponentSignature.h"
#include "Archetype/ArchetypeRegistry.h"
#include "Systems/Query.h"



/*************************************************************************
* \file:         World.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.0
* \brief
*                composes EntityManager + ComponentRegistry into the single
*                API systems and gameplay code actually call
*
* \details
*
* \history
*    -v1.0       skeleton
*
*
**************************************************************************/

namespace Crucible
{
	class ENGINE_API World
	{
	public:
		World();
		~World() = default;

		// World owns ECS storage, so copying it is not allowed
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		template <typename T>
		ComponentId RegisterComponent() { return m_components.Register<T>(); }

		ComponentRegistry& Components() { return m_components; }
		ArchetypeRegistry& Archetypes() { return m_archetypes; }

		Entity CreateEntity();
		Entity CreateEntity(const ComponentSignature& signature);

		void DestroyEntity(Entity entity);

		bool IsAlive(Entity e)const { return m_entities.IsAlive(e); }
		ComponentSignature GetSignature(Entity entity)const;

		template <typename T>
		bool HasComponent(Entity entity)const;

		template <typename T>
		T* GetComponent(Entity entity);

		template <typename T> T& AddComponent(Entity entity, T value = T{});
		template <typename T> void RemoveComponent(Entity entity);

		template<typename ...T>
		Query<T... > CreateQuery();

	private:
		void MoveEntity(EntityRecord& record, Entity entity, Archetype& destination);

		EntityManager     m_entities;
		ComponentRegistry m_components;
		ArchetypeRegistry m_archetypes;

	};

	template <typename T>
	bool World::HasComponent(Entity e)const
	{
		if (!m_entities.IsAlive(e)) return false;
		return m_entities.Record(e).archetype->Contains(ComponentTypeId<T>());
	}

	template <typename T>
	T* World::GetComponent(Entity e)
	{
		if (!m_entities.IsAlive(e)) return nullptr;

		const EntityRecord& record = m_entities.Record(e);
		const ComponentId id = ComponentTypeId<T>();
		
		if (!record.archetype->Contains(id)) return nullptr;
		return static_cast<T*>(record.archetype->GetComponentPtr(id, record.row));
	}
	template<typename T>
	inline T& World::AddComponent(Entity entity, T value)
	{
		assert(m_entities.IsAlive(entity) && "AddComponent on a dead/stale Entity");

		EntityRecord& record = m_entities.Record(entity);
		const ComponentId id = ComponentTypeId<T>();

		if (record.archetype->Contains(id))
		{
			T* existing = static_cast<T*>(record.archetype->GetComponentPtr(id, record.row));
			*existing = std::move(value);
			return *existing;
		}
		
		Archetype* destination = m_archetypes.GetAddDestination(*record.archetype, id);
		MoveEntity(record, entity, *destination);

		T* slot = static_cast<T*>(destination->GetComponentPtr(id, record.row));
		*slot = std::move(value);
		return *slot;
	}
	template<typename T>
	inline void World::RemoveComponent(Entity entity)
	{
		assert(m_entities.IsAlive(entity) && "Remove Component on dead/stale Entity");

		EntityRecord& record = m_entities.Record(entity);
		const ComponentId id = ComponentTypeId<T>();

		if (!record.archetype->Contains(id)) return;

		Archetype* destination = m_archetypes.GetRemoveDestination(*record.archetype, id);
		MoveEntity(record, entity, *destination);
	}

	template<typename... T>
	Query<T...>World::CreateQuery()
	{
		QueryDescription description;
		description.required = MakeSignature<T...>();
		Query<T...>query(std::move(description), m_archetypes);
		query.Rebuild();
		return query;
	}
}