/*************************************************************************
* \file:         Archetype.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Implementation of Archetype's row/column management.
*                See Archetype.h for the public interface and member
*                comments.
*
* \details
*                The constructor builds one ComponentColumn per set bit
*                in the signature (see ComponentColumn.h). Add/RemoveEntity
*                keep m_entities and every column in lock-step (see
*                ValidateRows()); World::MoveEntity (World.cpp) is the
*                main caller that shuffles rows between two Archetypes
*                when an entity's component set changes.
*
* \history
*    -v1.0       initial column build + row add/remove/lookup
*
*
**************************************************************************/
#include <cassert>

#include "Archetype.h"

namespace Crucible
{
	Archetype::Archetype(ComponentSignature signature, const ComponentRegistry& registry) :m_signature(signature)
	{
		m_columnLookup.assign(kMaxComponents, -1);

		for (ComponentId id = 0; id < kMaxComponents; ++id)
		{
			if (!m_signature.test(id)) continue;
			assert(registry.IsRegistered(id) && " archetype build from unregistered component");
			m_columnLookup[id] = static_cast<int>(m_componentIds.size());
			m_componentIds.push_back(id);
			m_columns.emplace_back(registry.Get(id));
		}

		m_edges.resize(kMaxComponents);
	}

	uint32_t Archetype::AddEntityUninitialized(Entity e)
	{
		m_entities.push_back(e);
		return static_cast<uint32_t>(m_entities.size() - 1);
	}

	uint32_t Archetype::AddEntityDefault(Entity e)
	{
		const uint32_t row = AddEntityUninitialized(e);

		for (ComponentColumn& column : m_columns) column.PushBackDefault();
		assert(ValidateRows());
		return row;
	}
	
	Entity Archetype::RemoveEntity(uint32_t row)
	{
		assert(row < m_entities.size());
		assert(ValidateRows());

		const uint32_t lastRow = static_cast<uint32_t>(m_entities.size() - 1);
		for (ComponentColumn& column : m_columns) column.SwapRemove(row);

		Entity relocated = kNullEntity;
		if (row != lastRow)
		{
			relocated = m_entities[lastRow];
			m_entities[row] = relocated;
		}
		m_entities.pop_back();

		assert(ValidateRows());
		return relocated;
	}

	int Archetype::ColumnIndex(ComponentId id)const
	{
		if (id >= m_columnLookup.size()) return -1;
		return m_columnLookup[id];
	}

	ComponentColumn& Archetype::GetColumn(ComponentId id)
	{
		const int index = ColumnIndex(id);
		assert(index >= 0 &&"component not present in this archetype");
		return m_columns[static_cast<std::size_t>(index)];
	}

	void* Archetype::GetColumnData(ComponentId id) { return GetColumn(id).Data(); }
	void* Archetype::GetComponentPtr(ComponentId id, uint32_t row) { return GetColumn(id).At(row); }

	bool Archetype::ValidateRows()const
	{
		for (const ComponentColumn& column : m_columns)
		{
			if (column.Size() != m_entities.size()) return false;
		}
		return true;
	}

	void Archetype::Reserve(uint32_t additionalRows)
	{
		m_entities.reserve(m_entities.size() + additionalRows);
		for (ComponentColumn& column : m_columns)
		{
			column.Reserve(column.Size() + additionalRows);
		}
	}
}