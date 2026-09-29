/*************************************************************************
* \file:         Archetype.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.3
* \brief
*                Declares Archetype: a single table of entities that all
*                share the exact same ComponentSignature, storing one
*                ComponentColumn per component type in that signature.
*
* \details
*                An Archetype is essentially a row-per-entity table:
*                m_entities[row] is the Entity that owns that row, and
*                m_columns[i]'s row 'row' holds that entity's instance
*                of component m_componentIds[i]. Archetypes are created
*                and looked up by ArchetypeRegistry (see
*                ArchetypeRegistry.h/.cpp) and are never created
*                directly by gameplay code. See Archetype.cpp for the
*                implementation of row add/remove (column lookup is inline
*                here), and World.cpp for how entities are moved between
*                archetypes when their component set changes.
*
* \history
*    -v1.0       initial archetype table (columns + entity list + edges)
*    -v1.1       ColumnIndex() is now an O(1) lookup via m_columnLookup
*                instead of a linear scan; EntityAt() now bounds-asserts
*                like ComponentColumn::At() does
*    -v1.2       strong exception guarantee for row appends, noexcept
*                removal, mutators private (World/ArchetypeRegistry/Query
*                only), signature validated in all builds
*    -v1.3       inlined column lookup (ColumnIndex/GetColumn/GetComponentPtr/
*                GetColumnData); m_columnLookup is a fixed std::array
*
**************************************************************************/
#pragma once

#include <array>
#include <cassert>
#include <vector>

#include "EngineExport.h"

#include "../Components/ComponentColumn.h"
#include "../Components/ComponentRegistry.h"
#include "../Components/ComponentSignature.h"
#include "../Core/ECSCore.h"

namespace Crucible
{

	//forward declare 
	class Archetype;

	/*************************************************************************
	 \brief
		Caches the destination archetype reached by adding or removing one
		component type from a given archetype.

	 \details
		One ArchetypeEdge exists per (archetype, ComponentId) pair, stored
		in Archetype::m_edges. ArchetypeRegistry::GetAddDestination/
		GetRemoveDestination populate add/remove lazily on first use and
		reuse the cached pointer afterwards, turning a repeated
		AddComponent<T>/RemoveComponent<T> transition into an O(1) pointer
		read instead of a signature-copy + hash-map lookup.

	 \note
		Both pointers are null until the corresponding transition has
		actually been requested at least once.
	 *************************************************************************/
	struct ArchetypeEdge
	{
		Archetype* add = nullptr;
		Archetype* remove = nullptr;
	};

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
	/*************************************************************************
	 \brief
		A single table of entities that all share the exact same
		ComponentSignature, storing one ComponentColumn per component type
		in that signature.

	 \details
		An Archetype is a row-per-entity table: Entities()[row] is the
		Entity that owns that row, and each column's row `row` holds that
		entity's instance of the matching component type. Archetypes are
		created and looked up by ArchetypeRegistry and never constructed
		directly by gameplay code - see World::AddComponent/RemoveComponent
		for how entities move between archetypes when their component set
		changes.

	 \note
		Non-copyable in practice: it owns ComponentColumns, which are
		move-only.
	 *************************************************************************/
	class ENGINE_API Archetype
	{
	public:

		/*********************************************************************
		 \brief
			Builds an archetype's column set for a given signature.

		 \param[in]
			Signature The exact component set this archetype represents.
		 \param[in]
			registry Used to fetch each set bit's ComponentInfo when
			constructing its ComponentColumn.

		 \details
			Allocates one ComponentColumn per bit set in Signature, and
			builds the O(1) id -> column-index lookup table
			(m_columnLookup) used by ColumnIndex().

		 \note
			Throws std::invalid_argument (all builds) if the signature
			contains a component id that is not registered.
		 *********************************************************************/
		Archetype(ComponentSignature Signature, const ComponentRegistry& registry);

		Archetype(const Archetype&) = delete;
		Archetype& operator=(const Archetype&) = delete;
		Archetype(Archetype&&) = default;
		Archetype& operator=(Archetype&&) = default;

		/*********************************************************************
		 \brief  This archetype's exact component set.
		 \return Reference to the signature it was constructed with.
		 *********************************************************************/
		const ComponentSignature& Signature() const { return m_signature; }

		/*********************************************************************
		 \brief  The component ids this archetype stores, in column order.
		 \return Reference to the id list.
		 *********************************************************************/
		const std::vector<ComponentId>& ComponentIds()const { return m_componentIds; }

		/*********************************************************************
		 \brief  Whether this archetype stores a given component type.
		 \param[in] id The component id to check.
		 \return true if that bit is set in this archetype's signature.
		 *********************************************************************/
		bool Contains(ComponentId id) const { return m_signature.test(id); }


		/*********************************************************************
		 \brief  How many entities (rows) this archetype currently holds.
		 \return The row count.
		 *********************************************************************/
		uint32_t EntityCount() const { return static_cast<uint32_t>(m_entities.size()); }

		/*********************************************************************
		\brief  Every entity currently stored, in row order.
		\return Reference to the entity list.
		*********************************************************************/
		const std::vector<Entity>& Entities() const { return m_entities; }

		/*********************************************************************
		 \brief  The entity occupying a given row.
		 \param[in] row The row to look up.
		 \return The Entity at that row.
		 \note  No bounds check - row must be < EntityCount().
		 *********************************************************************/
		Entity EntityAt(uint32_t row) const { assert(row < m_entities.size()); return m_entities[row]; }

		/*********************************************************************
		 \brief  Read-only view of the column storing one component type.
		 \param[in] id The component type to fetch.
		 \return Reference to that column.
		 \note  Asserts (debug only) that this archetype contains id.
		 *********************************************************************/
		const ComponentColumn& GetColumn(ComponentId id) const
		{
			const int index = ColumnIndex(id);
			assert(index >= 0 && "component not present in this archetype");
			return m_columns[static_cast<std::size_t>(index)];
		}

		/*********************************************************************
		 \brief  Debug check that every column's size matches the entity
				 count.
		 \return true if all columns are in lock-step with m_entities; false
				 indicates corrupted archetype state.
		 *********************************************************************/
		bool ValidateRows()const;

	private:
		// Structural changes and mutable data access go through World only.
		friend class World;
		friend class ArchetypeRegistry;
		template <typename... Components> friend class Query;

		/*********************************************************************
		 \brief  Appends a new row for entity, default-constructing every
				 column's value.
		 \return The row index it was placed at (always the last row).
		 \note  Strong guarantee: if an allocation or a component
				constructor throws, the archetype is unchanged.
		 *********************************************************************/
		uint32_t AddEntityDefault(Entity entity);

		/*********************************************************************
		 \brief  Makes room for one more row in the entity list and every
				 column, so a following migration cannot fail on allocation.
		 *********************************************************************/
		void ReserveForAppend();

		/*********************************************************************
		 \brief  Records entity as the owner of the row every column has
				 just been appended to (after ReserveForAppend()).
		 \return The new row index.
		 *********************************************************************/
		uint32_t AppendEntityReserved(Entity entity) noexcept;

		/*********************************************************************
		 \brief  Removes a row, swapping the last row into its place.
		 \return The entity relocated into row (previously the last row's
				 occupant), or kNullEntity if row already was the last row.
		 \note  Callers must update the relocated entity's record.
		 *********************************************************************/
		Entity RemoveEntity(uint32_t row) noexcept;

		void* GetColumnData(ComponentId id) { return GetColumn(id).Data(); }

		ComponentColumn& GetColumn(ComponentId id)
		{
			const int index = ColumnIndex(id);
			assert(index >= 0 && "component not present in this archetype");
			return m_columns[static_cast<std::size_t>(index)];
		}

		void* GetComponentPtr(ComponentId id, uint32_t row) { return GetColumn(id).At(row); }

		void TouchAll(uint32_t row, std::uint64_t tick)
		{
			for (ComponentColumn& column : m_columns) column.Touch(row, tick);
		}

		// Cached add/remove-destination edge for one component type (see ArchetypeRegistry).
		ArchetypeEdge& Edge(ComponentId id) { return m_edges[id]; }

		void Reserve(uint32_t additionalRows);//for bulk creation

		/*********************************************************************
		 \brief  Finds a component type's index into m_columns/m_componentIds.
		 \param[in] id The component type to look up.
		 \return Its column index, or -1 if this archetype does not contain
				 id.
		 \details O(1) via m_columnLookup, built once in the constructor.
		 *********************************************************************/
		int ColumnIndex(ComponentId id) const
		{
			return id < kMaxComponents ? m_columnLookup[id] : -1;
		}

		ComponentSignature m_signature;
		std::vector<ComponentId> m_componentIds;
		std::vector<ComponentColumn> m_columns;
		std::vector<Entity> m_entities;
		std::vector<ArchetypeEdge> m_edges;

		std::array<int, kMaxComponents> m_columnLookup{};   // id -> column index, -1 = absent (inline, no heap hop)
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
