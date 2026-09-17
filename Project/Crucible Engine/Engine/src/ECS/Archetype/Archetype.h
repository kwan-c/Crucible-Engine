#pragma once

#include <vector>

#include "../Components/ComponentColumn.h"
#include "../Components/ComponentRegistry.h"
#include "../Components/ComponentSignature.h"
#include "../Core/ECSCore.h"

namespace Crucible
{

	//forward declare 
	class Archetype;

	// for future optimization
	struct ArchetypeEdge
	{
		Archetype* add = nullptr;
		Archetype* remove = nullptr;
	};

	class Archetype
	{
	public:
		Archetype(ComponentSignature Signature, const ComponentRegistry& registry);

		const ComponentSignature& Signature() const { return m_signature; }
		const std::vector<ComponentId>& ComponentIds()const { return m_componentIds; }

		bool Contains(ComponentId id) const { return m_signature.test(id); }

		uint32_t EntityCount() const { return static_cast<uint32_t>(m_entities.size()); }
		const std::vector<Entity>& Entities() const { return m_entities; }
		Entity EntityAt(uint32_t row) const { return m_entities[row]; }

		uint32_t AddEntityDefault(Entity entity);
		uint32_t AddEntityUninitialized(Entity entity);
		Entity RemoveEntity(uint32_t row);

		void* GetColumnData(ComponentId id);
		ComponentColumn& GetColumn(ComponentId id);
		void* GetComponentPtr(ComponentId id, uint32_t row);

		bool ValidateRows()const;

		ArchetypeEdge& Edge(ComponentId id) { return m_edges[id]; }

	private:
		int ColumnIndex(ComponentId id)const;

		ComponentSignature m_signature;
		std::vector<ComponentId> m_componentIds;
		std::vector<ComponentColumn> m_columns;
		std::vector<Entity> m_entities;
		std::vector<ArchetypeEdge> m_edges;
	};
}
