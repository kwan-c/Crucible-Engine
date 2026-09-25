/*************************************************************************
* \file:         World.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.2
* \brief
*                Implementation of World: entity creation/destruction
*                and the archetype-move logic used by
*                AddComponent/RemoveComponent. See World.h for the
*                public interface and member comments.
*
* \details
*                World's constructor wires the ArchetypeRegistry to the
*                ComponentRegistry and registers every known component
*                type (see ComponentRegistry::RegisterAll in
*                ComponentRegistry.cpp). CreateEntity/DestroyEntity are
*                thin wrappers around EntityManager (see ECSCore.h) and
*                Archetype (see Archetype.cpp). MoveEntity() is the
*                shared relocation routine used whenever an entity's
*                component set changes (Add/RemoveComponent in
*                World.h) - it moves shared columns between archetypes
*                via ComponentColumn::MoveElementTo (see
*                ComponentColumn.cpp), relocates a newly added value
*                straight into its slot, and default-constructs any other
*                new columns.
*
* \history
*    -v1.0       initial CreateEntity/DestroyEntity/GetSignature/MoveEntity
*    -v1.1       hook symmetry (creation fires add hooks, destruction fires
*                per-component remove hooks), DestroyAllEntities, hooks
*                cannot be registered mid-iteration, MoveEntity relocates
*                the added value instead of default-construct + assign
*    -v1.2       DestroyEntity body moved into DestroyEntityImpl(entity,
*                fireHooks) so CommandBuffer can silently roll back spawns
*                whose add hooks never ran
*
**************************************************************************/

#include "World.h"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Crucible
{

	//build archetype registry over m_components
	World::World() :m_archetypes(m_components)
	{
		m_components.RegisterAll();
	}

	EntityRecord& World::RequireMutableEntity(Entity entity, const char* operation)
	{
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error(std::string("World::") + operation +
				" called while a Query is iterating - use a CommandBuffer instead (see CommandBuffer.h)");
		}
		if (!m_entities.IsAlive(entity))
		{
			throw std::invalid_argument(std::string("World::") + operation + " called with a dead/stale Entity");
		}
		return m_entities.Record(entity);
	}

	Entity World::CreateEntity()
	{
		return CreateEntityImpl(ComponentSignature{}, true);
	}

	Entity World::CreateEntity(const ComponentSignature& signature)
	{
		return CreateEntityImpl(signature, true);
	}

	Entity World::CreateEntityImpl(const ComponentSignature& signature, bool fireAddHooks)
	{
		//so command buffer and World commands dont get mixed up messing up archetype registry
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error("World::CreateEntity called while a Query is iterating - use a CommandBuffer instead (see CommandBuffer.h)");
		}

		const Entity entity = m_entities.Create();

		Archetype* archetype = m_archetypes.FindOrCreate(signature);
		const uint32_t row = archetype->AddEntityDefault(entity);

		EntityRecord& record = m_entities.RecordAt(entity.index);
		record.archetype = archetype;
		record.row = row;

		archetype->TouchAll(row, m_currentTick);

		if (fireAddHooks) FireAddHooksForRow(*archetype, row, entity);

		return entity;
	}

	std::vector<Entity> World::CreateEntities(uint32_t count, const ComponentSignature& signature)
	{
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error("World::CreateEntities called while a Query is iterating - use a CommandBuffer instead (see CommandBuffer.h)");
		}

		std::vector<Entity> created;
		if (count == 0) return created;
		created.reserve(count);

		Archetype* archetype = m_archetypes.FindOrCreate(signature);
		archetype->Reserve(count);
		m_entities.Reserve(count);

		for (uint32_t i = 0; i < count; ++i)
		{
			const Entity entity = m_entities.Create();
			const uint32_t row = archetype->AddEntityDefault(entity);

			EntityRecord& record = m_entities.RecordAt(entity.index);
			record.archetype = archetype;
			record.row = row;

			archetype->TouchAll(row, m_currentTick);
			FireAddHooksForRow(*archetype, row, entity);

			created.push_back(entity);
		}
		return created;
	}

	void World::NotifyComponentsAdded(Entity entity)
	{
		if (!m_entities.IsAlive(entity)) return;
		const EntityRecord& record = m_entities.Record(entity);
		FireAddHooksForRow(*record.archetype, record.row, entity);
	}

	void World::OnEntityDestroyed(std::function<void(World&, Entity)> callback)
	{
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error("World::OnEntityDestroyed: register hooks outside queries and hook dispatch");
		}
		m_onDestroyHooks.push_back(std::move(callback));
	}

	void World::FireAddHooks(ComponentId id, Entity entity, void* component)
	{
		if (m_onAddHooks[id].empty()) return;
		ArchetypeRegistry::IterationGuard guard(m_archetypes);   // hooks may read, never restructure
		for (auto& hook : m_onAddHooks[id]) hook(*this, entity, component);
	}

	void World::FireRemoveHooks(ComponentId id, Entity entity, void* component)
	{
		if (m_onRemoveHooks[id].empty()) return;
		ArchetypeRegistry::IterationGuard guard(m_archetypes);
		for (auto& hook : m_onRemoveHooks[id]) hook(*this, entity, component);
	}

	void World::FireAddHooksForRow(Archetype& archetype, uint32_t row, Entity entity)
	{
		for (ComponentId id : archetype.ComponentIds())
		{
			if (!m_onAddHooks[id].empty()) FireAddHooks(id, entity, archetype.GetComponentPtr(id, row));
		}
	}

	void World::FireRemoveHooksForRow(Archetype& archetype, uint32_t row, Entity entity)
	{
		for (ComponentId id : archetype.ComponentIds())
		{
			if (!m_onRemoveHooks[id].empty()) FireRemoveHooks(id, entity, archetype.GetComponentPtr(id, row));
		}
	}


	void World::DestroyEntity(Entity entity)
	{
		DestroyEntityImpl(entity, true);
	}

	void World::DestroyEntityImpl(Entity entity, bool fireHooks)
	{
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error("World::DestroyEntity called while a Query is iterating - use a CommandBuffer instead (see CommandBuffer.h)");
		}

		if (!m_entities.IsAlive(entity)) return;

		EntityRecord& record = m_entities.Record(entity);

		if (fireHooks)
		{
			if (!m_onDestroyHooks.empty())
			{
				ArchetypeRegistry::IterationGuard guard(m_archetypes);
				for (auto& hook : m_onDestroyHooks) hook(*this, entity);
			}

			// Every component the entity still owns counts as removed, so
			// resource-owning OnComponentRemoved hooks fire here too - not only
			// on RemoveComponent().
			FireRemoveHooksForRow(*record.archetype, record.row, entity);
		}

		const Entity relocated = record.archetype->RemoveEntity(record.row);
		if (relocated.IsValid())
			m_entities.RecordAt(relocated.index).row = record.row;
		m_entities.Destroy(entity);
	}

	void World::DestroyAllEntities()
	{
		std::vector<Entity> alive;
		alive.reserve(m_entities.AliveCount());
		for (Archetype* archetype : m_archetypes.All())
		{
			alive.insert(alive.end(), archetype->Entities().begin(), archetype->Entities().end());
		}

		for (Entity entity : alive) DestroyEntity(entity);
	}


	ComponentSignature World::GetSignature(Entity entity) const
	{
		if (!m_entities.IsAlive(entity)) return ComponentSignature{};

		return m_entities.Record(entity).archetype->Signature();
	}


	void World::MoveEntity(EntityRecord& record, Entity entity, Archetype& destination, ComponentId addedId, void* addedValue)
	{
		Archetype& source = *record.archetype;
		assert(&source != &destination);

		const uint32_t destinationRow = destination.AddEntityUninitialized(entity);

		for (ComponentId id : destination.ComponentIds())
		{
			ComponentColumn& column = destination.GetColumn(id);

			if (source.Contains(id))
			{
				source.GetColumn(id).MoveElementTo(record.row, column);
			}
			else if (id == addedId && addedValue != nullptr)
			{
				// One relocation instead of default-construct + move-assign.
				const ComponentInfo& info = column.Info();
				void* slot = column.PushBackUninitialized();
				if (info.isTriviallyRelocatable) std::memcpy(slot, addedValue, info.size);
				else info.moveConstruct(slot, addedValue);
			}
			else
			{
				column.PushBackDefault();
			}
		}

		assert(destination.ValidateRows());

		const Entity relocated = source.RemoveEntity(record.row);
		if (relocated.IsValid())
		{
			m_entities.RecordAt(relocated.index).row = record.row;
		}

		record.archetype = &destination;
		record.row = destinationRow;

	}


}
