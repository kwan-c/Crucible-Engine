#include "World.h"
#include <cassert>

namespace Crucible
{
	World::World() :m_archetypes(m_components)
	{
		m_components.RegisterAll();
	}
	Entity World::CreateEntity()
	{
		return CreateEntity(ComponentSignature{});
	}
	Entity World::CreateEntity(const ComponentSignature& signature)
	{
		const Entity entity = m_entities.Create();

		Archetype* archetype = m_archetypes.FindOrCreate(signature);
		const uint32_t row = archetype->AddEntityDefault(entity);

		EntityRecord& record = m_entities.RecordAt(entity.index);
		record.archetype = archetype;
		record.row = row;

		return entity;
	}
	void World::DestroyEntity(Entity entity)
	{
		if (!m_entities.IsAlive(entity)) return;
		EntityRecord& record = m_entities.Record(entity);
		const Entity relocated = record.archetype->RemoveEntity(record.row);
		if (relocated.IsValid())
			m_entities.RecordAt(relocated.index).row = record.row;
		m_entities.Destroy(entity);
	}
	ComponentSignature World::GetSignature(Entity entity) const
	{
		if (!m_entities.IsAlive(entity)) return ComponentSignature{};

		return m_entities.Record(entity).archetype->Signature();
	}
	void World::MoveEntity(EntityRecord& record, Entity entity, Archetype& destination)
	{
		Archetype& source = *record.archetype;
		assert(&source != &destination);

		const uint32_t destinationRow = destination.AddEntityUninitialized(entity);

		for (ComponentId id : destination.ComponentIds())
		{
			if (source.Contains(id))
			{
				source.GetColumn(id).MoveElementTo(record.row, destination.GetColumn(id));
			}
			else
			{
				destination.GetColumn(id).PushBackDefault();
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