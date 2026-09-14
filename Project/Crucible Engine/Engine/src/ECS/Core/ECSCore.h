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

	class EntityManager
	{
	public:
		EntityManager() { generations.push_back(0); }// default initilize to 1 to reserve index 0 so it is not a real entity

		inline Entity Create()
		{
			
			if (!freeList.empty())
			{
				uint32_t idx = freeList.back();
				freeList.pop_back();
				return{ idx, generations[idx] };
			}
			generations.emplace_back(0);
			uint32_t index = static_cast<uint32_t>(generations.size() - 1);
			return{ index,generations[index] };

		}

		//to check is the entity is 
		inline bool IsAlive(Entity e)const
		{

			if (!e.IsValid() || e.index >= (generations.size())) return false;

			return generations[e.index] == e.generation;
		}

		inline bool Destroy(Entity e)
		{
			if (!IsAlive(e)) return false;//prevent double deletion

			generations[e.index]++;
			freeList.push_back(e.index);
			return true;
		}

	private:
		std::vector<uint32_t> generations;// which version of entity index is  being used 
		std::vector<uint32_t> freeList;// available indexes that can be reused which have been desroes previously
	};
}