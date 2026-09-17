#pragma once
#include <cstdint>
#include <vector>
#include <iostream>

/*************************************************************************
* \file:         ECSCore.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          12 Septemeber 2026
* \version       1.0
* \brief
*                core data structures adn managements systems for ecs
*
* \details
*                Defines the Entity structure used to uniquely identify entities
*                through index and generation value. provide entity manager for 
*                creating, destroying and validating entity.
* \history
*    -v1.0       implemented entity struct and implemented entity manager
*
*
**************************************************************************/


namespace Crucible 
{
	// stuff that could be added hash for entites if we use them as keys in unorderd map as sam i with 2 different keys 
	// should have different keys when they have different gens
	struct Entity
	{
		uint32_t index = 0;
		uint32_t generation =0;
		
		
		Entity() = default;
		Entity(uint32_t ind, uint32_t gen) : index(ind), generation(gen) {}

		bool operator==(const Entity& E)const
		{
			return index == E.index && generation == E.generation;
		}

		bool IsValid() const
		{
			return index != 0;//to catch default initialized enitites
		}
	};

	inline constexpr Entity kNullEntity{};

	// no used for now could be deleted later
	template<typename T> 
	class IndexedArray
	{
	public:
		void Set(uint32_t index,T value)
		{
			if (index >= data.size())  data.resize(index + 1);
			if (index >= present.size())  present.resize(index + 1,false);

			data[index] = std::move(value);
			present[index] = true;
			
		}

		T* Get(uint32_t index)
		{
			return (index < present.size() && present[index]) ? &data[index] : nullptr;
		}

		const T* Get(uint32_t index) const
		{
			return (index < present.size() && present[index]) ? &data[index] : nullptr;
		}
	
		void Clear(uint32_t index)
		{
			if (index < present.size()) present[index] = false;
		}
	private:
		std::vector<T> data;
		std::vector<bool>present;

	};
}

//for entity functions
namespace Crucible
{
	//mention archetype 
	class Archetype;

	struct EntityRecord
	{
		Archetype* archetype = nullptr; // which archetype does stores it components
		uint32_t generation = 0;// whihc life time is this slot
		uint32_t row = 0; // which row inside archetype 
	};
	class EntityManager
	{
	public:
		EntityManager() { m_records.emplace_back(); }// default initilize to 1 to reserve index 0 so it is not a real entity

		/*
		* note: use a c++23 explicit object parameter  to provide both read only
		* and read/write access through on function template
		*/
		template <typename Self>
		auto& Record(this Self& self,Entity e)
		{
			return self.m_records[e.index];
		}

		EntityRecord& RecordAt(uint32_t index) { return m_records[index]; }
		uint32_t AliveCount() const { return m_aliveCount; }


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

		//to check is the entity is 
		bool IsAlive(Entity e)const
		{

			return (e.IsValid() && e.index < (m_records.size()) &&
				m_records[e.index].generation == e.generation &&
				m_records[e.index].archetype != nullptr);

		}

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
		std::vector<uint32_t> m_freeList;// available indexes that can be reused which have been desroes previously
		uint32_t m_aliveCount = 0;
	};
}