#pragma once

//std libs
#include <vector>
#include <tuple>
#include <utility>
#include <cstdint>

// header files
#include "../Core/ECSCore.h"
#include "../Components/ComponentSignature.h"
#include "../Components/ComponentTypeId.h"
#include "../Archetype/ArchetypeRegistry.h"

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
	struct QueryDescription
	{
		ComponentSignature required;
		ComponentSignature excluded;
		ComponentSignature optional; // reserved -- not used by matching yet
		bool Matches(const ComponentSignature& archetype) const
		{
			return SignatureMatches(archetype, required)
				&& (archetype & excluded).none();
		}
	};

	// Query caching policies; Cached and Auto are reserved for later.
	enum class QueryCachePolicy { None, Cached, Auto };
	

	//query class
	class QueryBase
	{
	public:
		QueryBase(QueryDescription description,ArchetypeRegistry& registry ):m_description(std::move (description)),m_registry(&registry){}

		const std::vector<Archetype*>& MatchingArchetypes()const{return m_matching;}
		bool MatchesArchetype(const Archetype& archetype)const
		{
			return m_description.Matches(archetype.Signature());
		}

		void AddArchetype(Archetype* archetype) { m_matching.push_back(archetype); }

		void Rebuild()
		{
			m_matching.clear();
			for (Archetype* archetype : m_registry->All())
			{
				if (m_description.Matches(archetype->Signature())) m_matching.push_back(archetype);
			}
		}

		void Refresh() { if (m_policy == QueryCachePolicy::None) Rebuild(); }

	protected:
		QueryDescription m_description;
		ArchetypeRegistry* m_registry = nullptr;
		std::vector<Archetype*> m_matching;
		QueryCachePolicy m_policy = QueryCachePolicy::None;
	};

	template <typename... Components>
	class Query : public QueryBase
	{
	public:
		using QueryBase::QueryBase;

		template<typename Fn>
		void ForEach(Fn&& fn)
		{
			Refresh();
			for (Archetype* archetype : m_matching)
			{
				const uint32_t count = archetype->EntityCount();
				if (count == 0)continue;

				std::tuple<Components*...> columns{ static_cast<Components*>(archetype->GetColumnData(ComponentTypeId<Components>()))... };

				const std::vector<Entity>& entities = archetype->Entities();

				for (uint32_t row = 0; row < count; ++row)
				{
					fn(entities[row], std::get<Components*>(columns)[row]...);
				}
			}
		}

		uint32_t CountEntities()
		{
			Refresh();
			uint32_t total = 0;
			for (Archetype* archetype : m_matching)
			{
				total += archetype->EntityCount();
			}
			return total;
		}
	};

}