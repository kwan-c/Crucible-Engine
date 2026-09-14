#pragma once

//std libs
#include <vector>
// header files
#include "ECS/Core/ECSCore.h"
#include "ECS/Components/ComponentRegistry.h"
/*************************************************************************
* \file:         Query.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.0
* \brief
*                iterate entities matching a set of component types
*
* \details
*
* \history
*    -v1.0       skeleton
*
*
**************************************************************************/

namespace Crucible
{
	//helper template to get the first component queried;
	template<typename First, typename ...Rest>
	struct FirstOf
	{
		using Type = First;
	};

	//query class
	template<typename ... Components>
	class Query
	{
		// no queries with zero comps
		static_assert(sizeof...(Components) > 0, "Query must contain at least one compoenent type");

	public:

		explicit Query(ComponentRegistry& registry) :m_registry(registry) {}

		//for each function to Query
		template<typename Fn>
		void ForEach(Fn&& fn)
		{
			using seed = typename FirstOf<Components...>::Type;

			auto& seedPool = m_registry.Pool<seed>();

			std::vector<Entity> entities = seedPool.Raw().Entities();

			//chck if entites contain comps
			for (Entity e : entities)
			{
				const bool hasAllComponents = (m_registry.Pool<Components>().Contains(e)&& ...);

				if (!hasAllComponents)continue;

				fn(e, *m_registry.Pool<Components>().Get(e)...);
			}
		}

	private:

		ComponentRegistry& m_registry;

	};

}