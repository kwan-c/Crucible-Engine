/*************************************************************************
* \file:         World.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.1
* \brief
*                composes EntityManager + ComponentRegistry into the single
*                API systems and gameplay code actually call
*
* \details
*                World is the single front-facing entry point for the
*                ECS. It owns an EntityManager (entity ids/liveness, see
*                ECSCore.h), a ComponentRegistry (per-type ComponentInfo,
*                see ComponentRegistry.h) and an ArchetypeRegistry (the
*                archetype table, see ArchetypeRegistry.h). Gameplay
*                code and systems (see Systems/IECSSystems.h and
*                ECSPhysicsSystem.h for an example system) should only
*                ever talk to World - it hides the archetype-move
*                bookkeeping in MoveEntity() (see World.cpp) behind
*                simple Add/Remove/Get/Has component calls.
*
* \history
*    -v1.0       skeleton
*    -v1.1       added member-level comments for readability, no logic changes
*    -v1.2       AddComponent/RemoveComponent now throw std::invalid_argument
*                on a dead/stale Entity in every build config, instead of
*                relying solely on an assert that NDEBUG strips out
*
**************************************************************************/
#pragma once

#include "EngineExport.h"

//std libs
#include<cstdint>
#include <cassert>
#include <utility>
#include <stdexcept>

//header files
#include "Core/ECSCore.h"          // Entity, EntityManager, IndexedArray
#include "Components/ComponentRegistry.h" // ComponentRegistry
#include "Components/ComponentSignature.h"
#include "Archetype/ArchetypeRegistry.h"
#include "Systems/Query.h"





namespace Crucible
{
	/*************************************************************************
	 \brief
		Composes EntityManager + ComponentRegistry + ArchetypeRegistry into
		the single API systems and gameplay code actually call.

	 \details
		World is the single front-facing entry point for the ECS. It hides
		the archetype-move bookkeeping in MoveEntity() behind simple
		Add/Remove/Get/Has component calls; gameplay code and systems should
		only ever talk to World, never to EntityManager/Archetype/
		ArchetypeRegistry directly.

	 \note
		Non-copyable: owns all ECS storage (every entity, archetype and
		component column), so copying would require deep-cloning the entire
		ECS state, which is not supported.
	 *************************************************************************/
	class ENGINE_API World
	{
	public:

		/*********************************************************************
		 \brief  Constructs an empty world and registers every known
				 component type.
		 *********************************************************************/
		World();
		~World() = default;

		// World owns ECS storage, so copying it is not allowed
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		/*********************************************************************
		 \brief  Registers component type T with this world's registry.
		 \tparam T The component type; must satisfy RegisteredComponent.
		 \return The ComponentId T was registered under.
		 *********************************************************************/
		template <typename T>
		ComponentId RegisterComponent() { return m_components.Register<T>(); }

		/*********************************************************************
		 \brief  This world's component registry.
		 \return Reference to the ComponentRegistry.
		 *********************************************************************/
		ComponentRegistry& Components() { return m_components; }

		/*********************************************************************
		 \brief  This world's archetype registry.
		 \return Reference to the ArchetypeRegistry.
		 *********************************************************************/
		ArchetypeRegistry& Archetypes() { return m_archetypes; }

		/*********************************************************************
		 \brief  Creates a new entity with no components.
		 \return A handle to the new entity, placed in the empty archetype.
		 *********************************************************************/
		Entity CreateEntity();

		/*********************************************************************
		 \brief
			Creates a new entity, placed directly into the archetype for a
			given component signature.

		 \param[in]
			signature The exact component set the new entity should start
			with.
		 \return
			A handle to the new entity. Every one of its components is
			default-constructed.

		 \details
			Creates the matching archetype first if it doesn't exist yet.
			Slightly cheaper than CreateEntity() + repeated AddComponent<T>()
			calls when the full component set is already known up front,
			since it avoids the intermediate archetype moves.
		 *********************************************************************/
		Entity CreateEntity(const ComponentSignature& signature);

		/*********************************************************************
		 \brief  Destroys an entity, freeing its slot for reuse.
		 \param[in] entity The entity to destroy.
		 \note  A no-op (not an error) if entity is already dead or stale.
		 *********************************************************************/
		void DestroyEntity(Entity entity);

		/*********************************************************************
		 \brief  Whether an entity handle is still alive.
		 \param[in] e The handle to check.
		 \return true if e refers to a currently-alive entity.
		 *********************************************************************/
		bool IsAlive(Entity e)const { return m_entities.IsAlive(e); }

		/*********************************************************************
		 \brief  The exact component set an entity currently has.
		 \param[in] entity The entity to query.
		 \return Its archetype's signature, or an empty signature if entity
				 is dead/stale.
		 *********************************************************************/
		ComponentSignature GetSignature(Entity entity)const;

		/*********************************************************************
		 \brief  Whether an entity currently has a given component type.
		 \tparam T The component type to check for.
		 \param[in] entity The entity to check.
		 \return true if entity is alive and its archetype contains T.
		 *********************************************************************/
		template <typename T>
		bool HasComponent(Entity entity)const;

		/*********************************************************************
		 \brief  Fetches a pointer to an entity's instance of a component.
		 \tparam T The component type to fetch.
		 \param[in] entity The entity to query.
		 \return Pointer to the component, or nullptr if entity is dead/
				 stale or does not have T.
		 *********************************************************************/
		template <typename T>
		T* GetComponent(Entity entity);

		/*********************************************************************
		 \brief
			Adds (or overwrites) a component on an entity.

		 \tparam T
			The component type to add.
		 \param[in]
			entity The entity to modify.
		 \param[in]
			value The value to give the component; defaults to T{}.
		 \return
			Reference to the entity's (new or existing) T instance.

		 \details
			If entity already has a T, overwrites it in place with no
			archetype move. Otherwise moves the entity to the archetype for
			its signature plus T (via ArchetypeRegistry::GetAddDestination
			and MoveEntity()), then writes value into the new column slot.

		 \note
			Throws std::invalid_argument if entity is dead or stale.
		 *********************************************************************/
		template <typename T> T& AddComponent(Entity entity, T value = T{});

		/*********************************************************************
		 \brief
			Removes a component from an entity, if it has one.

		 \tparam T
			The component type to remove.
		 \param[in]
			entity The entity to modify.

		 \details
			A no-op if entity does not currently have a T. Otherwise moves
			the entity to the archetype for its signature minus T (via
			ArchetypeRegistry::GetRemoveDestination and MoveEntity()).

		 \note
			Throws std::invalid_argument if entity is dead or stale.
		 *********************************************************************/
		template <typename T> void RemoveComponent(Entity entity);


		/*********************************************************************
		 \brief
			Builds a Query over a fixed set of required component types.

		 \tparam T
			The component types the query requires and will iterate.
		 \return
			A Query<T...> whose matching-archetype list has already been
			built (Rebuild() is called before returning).
		 *********************************************************************/
		template<typename ...T>
		Query<T... > CreateQuery();

	private:
		/*********************************************************************
		 \brief
			Relocates an entity's row from its current archetype into
			destination, carrying over every shared component and
			default-constructing any newly added ones.

		 \param[in,out]
			record The entity's EntityRecord; updated in place to point at
			its new archetype/row.
		 \param[in]
			entity The entity being moved (used only to place it into
			destination's row list).
		 \param[in,out]
			destination The archetype to move the entity into. Must differ
			from record.archetype.

		 \details
			For each component destination requires: if the source
			archetype already has it, moves the existing value across via
			ComponentColumn::MoveElementTo(); otherwise default-constructs a
			fresh value in the destination column. Afterwards removes the
			entity's old row from the source archetype (which is what
			actually destroys any moved-from values) and fixes up the row
			index of whichever entity got swapped into that now-vacant row.
		 *********************************************************************/
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
		if (m_archetypes.IsIterating())
		{
		throw std::logic_error("World::AddComponent called while a Query is iterating - use a CommandBuffer instead (see CommandBuffer.h)");
		}

		assert(m_entities.IsAlive(entity) && "AddComponent on a dead/stale Entity");

		if (!m_entities.IsAlive(entity)) throw std::invalid_argument("World::AddComponent called with a dead/stale Entity");
		

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
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error("World::RemoveComponent called while a Query is iterating - use a CommandBuffer instead (see CommandBuffer.h)");
		}

		assert(m_entities.IsAlive(entity) && "Remove Component on dead/stale Entity");

		if (!m_entities.IsAlive(entity)) throw std::invalid_argument("World::RemoveComponent called with a dead/stale Entity");

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