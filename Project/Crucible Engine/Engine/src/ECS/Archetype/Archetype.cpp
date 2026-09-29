/*************************************************************************
* \file:         Archetype.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.2
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
*                Appends reserve every array first and undo partially
*                appended columns on failure, so the lock-step invariant
*                holds even when an allocation or constructor throws.
*
* \history
*    -v1.0       initial column build + row add/remove/lookup
*    -v1.1       strong exception guarantee for AddEntityDefault,
*                ReserveForAppend/AppendEntityReserved for migrations,
*                signature validated in all builds
*    -v1.2       column lookup (ColumnIndex/GetColumn/GetComponentPtr/
*                GetColumnData) moved inline into Archetype.h
*
**************************************************************************/
#include <algorithm>
#include <cassert>

#include "Archetype.h"

namespace Crucible
{
	Archetype::Archetype(ComponentSignature signature, const ComponentRegistry& registry) :m_signature(signature)
	{
		registry.ValidateSignature(m_signature, "Archetype");

		m_columnLookup.fill(-1);

		for (ComponentId id = 0; id < kMaxComponents; ++id)
		{
			if (!m_signature.test(id)) continue;
			m_columnLookup[id] = static_cast<int>(m_componentIds.size());
			m_componentIds.push_back(id);
			m_columns.emplace_back(registry.Get(id));
		}

		m_edges.resize(kMaxComponents);
	}

	void Archetype::ReserveForAppend()
	{
		// Grow geometrically; reserve(size + 1) alone would reallocate on every append.
		if (m_entities.size() == m_entities.capacity())
		{
			m_entities.reserve(std::max<std::size_t>(8, m_entities.capacity() * 2));
		}
		for (ComponentColumn& column : m_columns) column.ReserveForAppend();
	}

	uint32_t Archetype::AppendEntityReserved(Entity entity) noexcept
	{
		assert(m_entities.size() < m_entities.capacity() && "ReserveForAppend() must come first");
		m_entities.push_back(entity);
		assert(ValidateRows());
		return static_cast<uint32_t>(m_entities.size() - 1);
	}

	uint32_t Archetype::AddEntityDefault(Entity e)
	{
		ReserveForAppend();   // afterwards only component constructors can throw

		std::size_t constructed = 0;
		try
		{
			for (; constructed < m_columns.size(); ++constructed) m_columns[constructed].PushBackDefault();
		}
		catch (...)
		{
			// Undo the columns already appended so every column keeps the same length.
			while (constructed > 0) m_columns[--constructed].PopBack();
			throw;
		}

		return AppendEntityReserved(e);
	}

	Entity Archetype::RemoveEntity(uint32_t row) noexcept
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
