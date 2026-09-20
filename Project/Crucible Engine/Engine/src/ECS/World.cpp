/*************************************************************************
* \file:         World.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
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
*                World.h) - it copies shared columns between archetypes
*                via ComponentColumn::MoveElementTo (see
*                ComponentColumn.cpp) and default-constructs any newly
*                added columns.
*
* \history
*    -v1.0       initial CreateEntity/DestroyEntity/GetSignature/MoveEntity
*
*
**************************************************************************/

#include "World.h"
#include <cassert>
#include <stdexcept>

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

		return entity;
	}
	void World::DestroyEntity(Entity entity)
	{
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error("World::DestroyEntity called while a Query is iterating - use a CommandBuffer instead (see CommandBuffer.h)");
		}

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