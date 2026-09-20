/*************************************************************************
* \file:         ECSCore.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          17 September 2026
* \version       1.2
* \brief
*                core data structures and management systems for ecs
*
* \details
*                Defines the Entity structure used to uniquely identify entities
*                through index and generation value. provide entity manager for
*                creating, destroying and validating entity.
*                World (see World.h) owns the single EntityManager for a
*                given ECS instance; Archetype (see Archetype.h) is where
*                an entity's actual component data lives, and
*                EntityRecord below is the link between the two (which
*                archetype + row a given entity currently occupies).
* \history
*    -v1.0       implemented entity struct and implemented entity manager
*    -v1.1       added member-level comments for readability, no logic changes
*    -v1.2       fixed comment typos (brief line + the Entity/gens note
*                below), no logic changes
*
*
**************************************************************************/
#pragma once
#include <cstdint>
#include <vector>
#include <iostream>



namespace Crucible 
{

	// stuff that could be added hash for entities if we use them as keys in unordered map as sam i with 2 different keys 
	// should have different keys when they have different gens

	/*************************************************************************
	 \brief
		Uniquely identifies one entity via a slot index plus a generation
		counter.

	 \details
		index selects a slot in EntityManager's record table. generation is
		incremented every time that slot is destroyed and its index reused,
		so an old Entity handle referring to a since-recycled slot compares
		unequal to (and is rejected by) the new occupant - this is what lets
		EntityManager::IsAlive() detect a stale handle instead of silently
		resolving it to the wrong entity.

	 \note
		Index 0 is reserved and never assigned to a real entity (see
		EntityManager's constructor), so a default-constructed Entity is
		always invalid.
	 *************************************************************************/
	struct Entity
	{
		uint32_t index = 0;
		uint32_t generation =0;
		
		/*********************************************************************
		\brief  Constructs a default, invalid entity handle (index 0).
		*********************************************************************/
		Entity() = default;

		/*********************************************************************
		 \brief
			Constructs an entity handle from an explicit slot index and
			generation.

		 \param[in]
			ind The slot index.
		 \param[in]
			gen The generation the slot must currently be on for this handle
			to be considered alive.
		 *********************************************************************/
		Entity(uint32_t ind, uint32_t gen) : index(ind), generation(gen) {}


		/*********************************************************************
		 \brief  Compares two entity handles for equality.
		 \param[in] E The handle to compare against.
		 \return true if both the index and generation match.
		 *********************************************************************/
		bool operator==(const Entity& E)const
		{
			return index == E.index && generation == E.generation;
		}

		/*********************************************************************
		 \brief
			Whether this handle is anything other than the reserved
			null/default value.

		 \return
			true if index is non-zero.

		 \note
			This only rules out the default-constructed handle - it does NOT
			confirm the entity is currently alive in a World. Use
			World::IsAlive()/EntityManager::IsAlive() for that.
		 *********************************************************************/
		//0 index is reserved
		bool IsValid() const
		{
			return index != 0;//to catch default initialized entities
		}
	};

	/*********************************************************************
	 \brief  Shared constant representing "no entity".
	 *********************************************************************/
	inline constexpr Entity kNullEntity{};


}

//for entity functions
namespace Crucible
{
	//mention archetype 
	class Archetype;

	/*************************************************************************
	 \brief
		Per-entity bookkeeping linking an Entity slot to where its component
		data actually lives.

	 \details
		Owned by EntityManager, one instance per entity slot. archetype/row
		together say "this entity's components are row `row` inside
		`archetype`'s ComponentColumns"; generation is the value an Entity
		handle must match for this slot to be considered the same entity
		that created it.

	 \note
		archetype is nullptr both before an entity has been placed into any
		archetype and after it has been destroyed - EntityManager::IsAlive()
		relies on this.
	 *************************************************************************/
	//entityrecord for book keeping own may entity manager
	struct EntityRecord
	{
		Archetype* archetype = nullptr; // which archetype does stores it components
		uint32_t generation = 0;// which life time is this slot
		uint32_t row = 0; // which row inside archetype 
	};


	/*************************************************************************
	 \brief
		Owns the table of entity slots: allocates/recycles entity indices,
		tracks each one's generation and archetype/row, and answers liveness
		queries.

	 \details
		Entity creation reuses a freed slot's index (from m_freeList) before
		growing m_records, and every reuse bumps that slot's generation in
		Destroy() - the classic index+generation scheme that tells a stale
		Entity handle apart from a new entity later allocated the same
		index. World owns exactly one EntityManager; gameplay code should go
		through World's wrappers instead of calling this directly.

	 \note
		Index 0 is permanently reserved (see the constructor) so a
		default-constructed Entity can never be a real, alive entity.
	 *************************************************************************/
	class EntityManager
	{
	public:
		/*********************************************************************
		 \brief
			Constructs an empty manager and reserves entity index 0 so it
			can never be allocated to a real entity.
		 *********************************************************************/
		EntityManager() { m_records.emplace_back(); }//reserves index 0  so index starts at one so i cen use index 0 to check if valid

		/*
		* note: use a c++23 explicit object parameter  to provide both read only
		* and read/write access through on function template
		*/
		/*********************************************************************
		 \brief
			Fetches the EntityRecord for a given entity's slot, with either
			const or mutable access depending on how it is called.

		 \param[in]
			e The entity whose slot to fetch. Only its index is used.
		 \return
			Reference to that slot's EntityRecord.

		 \details
			Uses a C++23 explicit (deducing) `this` parameter so one
			template body serves both mutable and const call sites.

		 *********************************************************************/
		template <typename Self>
		auto& Record(this Self& self,Entity e)
		{
			return self.m_records[e.index];
		}


		/*********************************************************************
		 \brief  Fetches a slot's EntityRecord directly by index.
		 \param[in] index Slot index (not a generation-checked Entity).
		 \return Reference to that slot's EntityRecord.
		 \note  No bounds check - index must be < the number of allocated
				slots.
		 *********************************************************************/
		EntityRecord& RecordAt(uint32_t index) { return m_records[index]; }
		
		/*********************************************************************
		 \brief  How many entities are currently alive.
		 \return Count of entities created and not yet destroyed.
		 *********************************************************************/
		
		uint32_t AliveCount() const { return m_aliveCount; }

		/*********************************************************************
		 \brief
			Allocates a new entity, reusing a destroyed slot if one is free.

		 \return
			A handle to the newly created entity, carrying the slot's
			current generation.

		 \details
			Prefers popping an index off the free list before allocating a
			brand-new slot. The returned entity is not yet placed in any
			archetype - its record's archetype is left null until the
			caller (normally World::CreateEntity) assigns one.
		 *********************************************************************/
		//Allocate new an an reuses slot form free list if one 
		Entity Create()
		{
			uint32_t idx;
			if (!m_freeList.empty())
			{
				idx = m_freeList.back();
				m_freeList.pop_back();
			}
			else
			{
				 idx = static_cast<uint32_t>(m_records.size());
				m_records.emplace_back();
				
			}
			EntityRecord& record = m_records[idx];
			record.archetype = nullptr;
			record.row = 0;


			++m_aliveCount;

			return{ idx,record.generation};

		}

		/*********************************************************************
		 \brief
			Checks whether an entity handle still refers to a live entity.

		 \param[in]
			e The handle to check.
		 \return
			true if e.index is in range, its generation matches the slot's
			current generation, and the slot has an assigned archetype.

		 \note
			A freshly Create()'d entity is NOT yet alive by this check until
			it has been placed into an archetype - see World::CreateEntity.
		 *********************************************************************/
		//to check is the entity is 
		bool IsAlive(Entity e)const
		{

			return (e.IsValid() && e.index < (m_records.size()) &&
				m_records[e.index].generation == e.generation &&
				m_records[e.index].archetype != nullptr);

		}

		/*********************************************************************
		 \brief
			Destroys an entity, incrementing its slot's generation and
			returning the slot to the free list.

		 \param[in]
			e The entity to destroy.
		 \return
			true if e was alive and has been destroyed, false if it was
			already dead/stale (double-destroy is a no-op, not an error).
		 *********************************************************************/
		bool Destroy(Entity e)
		{
			if (!IsAlive(e)) return false;//prevent double deletion

			EntityRecord& record = m_records[e.index];

			++record.generation;
			record.archetype = nullptr;
			record.row = 0;

			--m_aliveCount;
			m_freeList.push_back(e.index);
			return true;
		}

	private:
		std::vector<EntityRecord> m_records;
		std::vector<uint32_t> m_freeList;// available indexes that can be reused which have been destroyed previously
		uint32_t m_aliveCount = 0;
	};
}