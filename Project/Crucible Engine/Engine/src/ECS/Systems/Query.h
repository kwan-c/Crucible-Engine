/*************************************************************************
* \file:         Query.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.1
* \brief
*                iterate entities matching a set of component types
*
* \details
*                A Query is built by World::CreateQuery<T...>() (see
*                World.h) and caches the list of archetypes (see
*                Archetype.h / ArchetypeRegistry.h) whose signature
*                matches a required/excluded component set. QueryBase
*                holds the untyped matching/rebuild logic; the derived
*                Query<Components...> template adds the typed
*                ForEach()/CountEntities() iteration on top, reading
*                each matching archetype's raw component columns (see
*                Archetype::GetColumnData in Archetype.cpp) directly.
*
* \history
*    -v1.0       skeleton
*    -v1.1       added member-level comments for readability, no logic changes
*
*
**************************************************************************/
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


namespace Crucible
{
	/*************************************************************************
	 \brief
		Describes which components a query requires and which it excludes.

	 \details
		required must be a subset of an archetype's signature for it to
		match; excluded must share no bits with it. optional is reserved for
		future use and does not currently affect matching.
	 *************************************************************************/
	struct QueryDescription
	{
		ComponentSignature required;
		ComponentSignature excluded;
		ComponentSignature optional; // reserved -- not used by matching yet

		/*********************************************************************
		 \brief  Whether an archetype's signature satisfies this description.
		 \param[in] archetype The signature to test.
		 \return true if archetype contains every bit in required and none
				 of the bits in excluded.
		 *********************************************************************/
		bool Matches(const ComponentSignature& archetype) const
		{
			return SignatureMatches(archetype, required)
				&& (archetype & excluded).none();
		}
	};

	/*************************************************************************
	 \brief
		Controls how aggressively a Query re-scans the archetype registry.

	 \note
		Only None is currently implemented (via ArchetypeRegistry::Version()
		checking, see QueryBase::Refresh()); Cached and Auto are reserved
		for later.
	 *************************************************************************/
	// Query caching policies; Cached and Auto are reserved for later.
	enum class QueryCachePolicy { None, Cached, Auto };
	

	/*************************************************************************
	 \brief
		Untyped base holding a Query's matching-archetype list and the logic
		to keep it in sync with the ArchetypeRegistry.

	 \details
		Query<Components...> derives from this to add typed iteration on
		top; everything about deciding WHICH archetypes match lives here,
		independent of which component types are actually being read.
	 *************************************************************************/
	class QueryBase
	{
	public:

		/*********************************************************************
		 \brief  Constructs a query over a given description and registry.
		 \param[in] description What components to require/exclude.
		 \param[in] registry The archetype registry to query against; must
					outlive this object.
		 *********************************************************************/
		QueryBase(QueryDescription description,ArchetypeRegistry& registry ):m_description(std::move (description)),m_registry(&registry){}

		/*********************************************************************
		 \brief  The archetypes this query currently matches.
		 \return Reference to the cached matching-archetype list.
		 \note  May be stale until Refresh()/Rebuild() has been called at
				least once (World::CreateQuery() does this for you).
		 *********************************************************************/
		const std::vector<Archetype*>& MatchingArchetypes()const{return m_matching;}

		/*********************************************************************
		 \brief  Whether a given archetype satisfies this query's description.
		 \param[in] archetype The archetype to test.
		 \return true if it matches.
		 *********************************************************************/
		bool MatchesArchetype(const Archetype& archetype)const
		{
			return m_description.Matches(archetype.Signature());
		}

		/*********************************************************************
		 \brief  Manually adds an archetype to the matching list.
		 \param[in] archetype The archetype to add.
		 \note  Does not check MatchesArchetype() - the caller is
				responsible for only adding genuine matches.
		 *********************************************************************/
		void AddArchetype(Archetype* archetype) { m_matching.push_back(archetype); }

		/*********************************************************************
		 \brief
			Fully rebuilds the matching-archetype list by scanning every
			archetype currently in the registry.

		 \details
			Clears and repopulates m_matching, then records the registry's
			current Version() so Refresh() can skip a redundant rebuild next
			time nothing has changed.
		 *********************************************************************/
		void Rebuild()
		{
			m_matching.clear();
			for (Archetype* archetype : m_registry->All())
			{
				if (m_description.Matches(archetype->Signature())) m_matching.push_back(archetype);
			}
			m_lastSeenVersion = m_registry->Version();
		}

		/*********************************************************************
		 \brief
			Rebuilds the matching-archetype list only if it might be stale.

		 \details
			With QueryCachePolicy::None, skips Rebuild() when the registry's
			Version() hasn't changed since the last rebuild (no new
			archetype created); otherwise always rebuilds. Called
			automatically by Query::ForEach()/CountEntities().
		 *********************************************************************/
		void Refresh() 
		{
			if (m_policy != QueryCachePolicy::None) return;
			if (m_registry->Version() == m_lastSeenVersion) return;
			Rebuild();
		}

	protected:
		QueryDescription m_description;
		ArchetypeRegistry* m_registry = nullptr;
		std::vector<Archetype*> m_matching;
		QueryCachePolicy m_policy = QueryCachePolicy::None;

		std::uint64_t m_lastSeenVersion = 0;
	};

	/*************************************************************************
	 \brief
		Typed query over a fixed set of component types, iterating every
		matching entity's components directly from archetype storage.

	 \tparam Components
		The component types this query reads. Determines both the archetype
		matching signature (via World::CreateQuery<T...>()) and the
		parameter types passed to ForEach()'s callback.

	 \details
		QueryBase holds the untyped matching/rebuild logic; this adds the
		typed ForEach()/CountEntities() iteration on top, reading each
		matching archetype's raw component columns directly - no per-entity
		virtual dispatch or per-component lookup happens inside the row
		loop.
	 *************************************************************************/
	template <typename... Components>
	class Query : public QueryBase
	{
	public:
		using QueryBase::QueryBase;

		/*********************************************************************
		 \brief
			Calls fn once per matching entity, passing its Entity handle and
			a reference to each requested component.

		 \tparam Fn
			Callable compatible with (Entity, Components&...).
		 \param[in]
			fn The callback to invoke per entity.

		 \details
			Calls Refresh() first to ensure the matching-archetype list is
			current, then for each non-empty matching archetype resolves
			each requested component's column pointer once and indexes it
			per row - avoiding a per-entity column lookup.
		 *********************************************************************/
		template<typename Fn>
		void ForEach(Fn&& fn)
		{
			ArchetypeRegistry::IterationGuard guard(*m_registry);

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

		/*********************************************************************
		 \brief  Counts how many entities currently match this query.
		 \return Total entity count across every matching archetype.
		 \details Calls Refresh() first, same as ForEach().
		 *********************************************************************/
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

	template <typename... Required>
	class QueryBuilder
	{
	public:
		explicit QueryBuilder(ArchetypeRegistry& registry) : m_registry(&registry)
		{
			m_description.required = MakeSignature<Required...>();
		}
		
		//entites containing will be skipped
		template <typename... Excluded>
		QueryBuilder& Exclude()
		{
			m_description.excluded = MakeSignature<Excluded...>();
			return *this;
		}

		//optional
		template <typename... Optional_>
		QueryBuilder& Optional()
		{
			m_description.optional = MakeSignature<Optional_...>();
			return *this;
		}

		Query<Required...> Build()
		{
			Query<Required...> query(std::move(m_description), *m_registry);
			query.Rebuild();
			return query;
		}

	private:
		QueryDescription m_description;
		ArchetypeRegistry* m_registry;

	};

}