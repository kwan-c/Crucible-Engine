/*************************************************************************
* \file:         Query.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.4
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
*                A `const T` component is read-only access; a non-const
*                `T` is write access and gets its change tick stamped for
*                every row the query visits.
*
* \history
*    -v1.0       skeleton
*    -v1.1       added member-level comments for readability, no logic changes
*    -v1.2       const/non-const access (write columns stamp change ticks),
*                ForEachChangedSince<F>(), rebuild narrowed to the rarest
*                required component's archetype list; optional world-tick
*                constructor parameter (existing constructor calls unchanged)
*    -v1.3       static AppendAccess(SystemAccess&) so a system can derive
*                its scheduler read/write access from its query type
*    -v1.4       iteration checks the running system's declared access;
*                a Query object rejects use from a second thread while in
*                use (copies/moves get a fresh guard)
*
**************************************************************************/
#pragma once

//std libs
#include <array>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// header files
#include "../Core/ECSCore.h"
#include "../Components/ComponentSignature.h"
#include "../Components/ComponentTypeId.h"
#include "../Archetype/ArchetypeRegistry.h"
#include "SystemAccess.h"


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
	enum class QueryCachePolicy { None, Cached, Auto };


	/*************************************************************************
	 \brief
		Untyped base holding a Query's matching-archetype list and the logic
		to keep it in sync with the ArchetypeRegistry.

	 \details
		Query<Components...> derives from this to add typed iteration on
		top; everything about deciding WHICH archetypes match lives here,
		independent of which component types are actually being read.

	 \note
		Holds pointers into the owning World (archetype registry and, when
		built through World, its tick) - a query must not outlive the World
		it was created from.
	 *************************************************************************/
	class QueryBase
	{
	public:

		/*********************************************************************
		 \brief  Constructs a query over a given description and registry.
		 \param[in] description What components to require/exclude.
		 \param[in] registry The archetype registry to query against; must
					outlive this object.
		 \param[in] worldTick Optional pointer to the World's current tick.
					When null, iteration does not stamp change ticks.
		 *********************************************************************/
		QueryBase(QueryDescription description, ArchetypeRegistry& registry, const std::uint64_t* worldTick = nullptr)
			: m_description(std::move(description)), m_registry(&registry), m_worldTick(worldTick) {}

		// Copies/moves share nothing mutable with the source: each gets a fresh in-use guard.
		QueryBase(const QueryBase& other)
			: m_description(other.m_description), m_registry(other.m_registry), m_worldTick(other.m_worldTick),
			m_matching(other.m_matching), m_policy(other.m_policy), m_lastSeenVersion(other.m_lastSeenVersion) {}

		QueryBase(QueryBase&& other) noexcept
			: m_description(std::move(other.m_description)), m_registry(other.m_registry), m_worldTick(other.m_worldTick),
			m_matching(std::move(other.m_matching)), m_policy(other.m_policy), m_lastSeenVersion(other.m_lastSeenVersion) {}

		QueryBase& operator=(const QueryBase& other)
		{
			if (this == &other) return *this;
			m_description = other.m_description;
			m_registry = other.m_registry;
			m_worldTick = other.m_worldTick;
			m_matching = other.m_matching;
			m_policy = other.m_policy;
			m_lastSeenVersion = other.m_lastSeenVersion;
			return *this;
		}

		QueryBase& operator=(QueryBase&& other) noexcept
		{
			if (this == &other) return *this;
			m_description = std::move(other.m_description);
			m_registry = other.m_registry;
			m_worldTick = other.m_worldTick;
			m_matching = std::move(other.m_matching);
			m_policy = other.m_policy;
			m_lastSeenVersion = other.m_lastSeenVersion;
			return *this;
		}

		~QueryBase() = default;

		/*********************************************************************
		 \brief  The archetypes this query currently matches.
		 \return Reference to the cached matching-archetype list.
		 \note  May be stale until Refresh()/Rebuild() has been called at
				least once (World::CreateQuery() does this for you).
		 *********************************************************************/
		const std::vector<Archetype*>& MatchingArchetypes() const { return m_matching; }

		/*********************************************************************
		 \brief  Whether a given archetype satisfies this query's description.
		 \param[in] archetype The archetype to test.
		 \return true if it matches.
		 *********************************************************************/
		bool MatchesArchetype(const Archetype& archetype) const
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
			Fully rebuilds the matching-archetype list.

		 \details
			Scans only the archetype list of the rarest required component
			(ArchetypeRegistry::ArchetypesWith) instead of every archetype,
			then records the registry's Version() so Refresh() can skip a
			redundant rebuild next time nothing has changed.
		 *********************************************************************/
		void Rebuild()
		{
			m_matching.clear();

			const std::vector<Archetype*>* candidates = &m_registry->All();
			for (ComponentId id = 0; id < m_description.required.size(); ++id)
			{
				if (!m_description.required.test(id)) continue;
				const std::vector<Archetype*>& with = m_registry->ArchetypesWith(id);
				if (with.size() < candidates->size()) candidates = &with;
			}

			for (Archetype* archetype : *candidates)
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
			archetype created). Called automatically by
			Query::ForEach()/ForEachChangedSince()/CountEntities().
		 *********************************************************************/
		void Refresh()
		{
			if (m_policy != QueryCachePolicy::None) return;
			if (m_registry->Version() == m_lastSeenVersion) return;
			Rebuild();
		}

	protected:
		/*********************************************************************
		 \brief
			Marks this Query object as being iterated by the calling thread.

		 \details
			Iterating refreshes the matching list and walks it, so one Query
			object must never be used by two threads at once (give each
			system its own query). Re-entrant use from the same thread (a
			nested ForEach on the same query) is allowed.

		 \note
			Throws std::logic_error if another thread is using the query.
		 *********************************************************************/
		class UseGuard
		{
		public:
			explicit UseGuard(QueryBase& query) : m_query(query)
			{
				const std::thread::id self = std::this_thread::get_id();
				std::thread::id owner{};
				// Claim the query, or accept a nested use by the thread that already owns it.
				if (!m_query.m_user.compare_exchange_strong(owner, self) && owner != self)
				{
					throw std::logic_error("Query: the same Query object is being iterated by two threads - give each system its own Query");
				}
				++m_query.m_useDepth;
			}

			~UseGuard()
			{
				if (--m_query.m_useDepth == 0) m_query.m_user.store(std::thread::id{});
			}

			UseGuard(const UseGuard&) = delete;
			UseGuard& operator=(const UseGuard&) = delete;

		private:
			QueryBase& m_query;
		};

		QueryDescription m_description;
		ArchetypeRegistry* m_registry = nullptr;
		const std::uint64_t* m_worldTick = nullptr;
		std::vector<Archetype*> m_matching;
		QueryCachePolicy m_policy = QueryCachePolicy::None;

		std::uint64_t m_lastSeenVersion = ~std::uint64_t{ 0 };

		std::atomic<std::thread::id> m_user{};   // thread currently iterating (id{} = none)
		int m_useDepth = 0;                      // nesting depth, touched only by m_user
	};

	/*************************************************************************
	 \brief
		Typed query over a fixed set of component types, iterating every
		matching entity's components directly from archetype storage.

	 \tparam Components
		The component types this query reads. `const T` is read-only access;
		a non-const `T` is write access.

	 \details
		For every row visited, each writable component's change tick is
		stamped with the World's current tick (conservative - the same model
		as Unity's read/write access). Declare components you only read as
		const so they are not reported as changed. No per-entity virtual
		dispatch or per-component lookup happens inside the row loop.
	 *************************************************************************/
	template <typename... Components>
	class Query : public QueryBase
	{
		static_assert(sizeof...(Components) > 0, "Query needs at least one component type");
		static_assert(((std::is_const_v<Components> || !WorldManaged<std::remove_const_t<Components>>::value) && ...),
			"This component is managed by World (e.g. HierarchyNode): query it as const and change it through World's API");

		template <typename C> using Bare = std::remove_const_t<C>;
		using PointerArray = std::array<void*, sizeof...(Components)>;
		using TickArray = std::array<std::uint64_t*, sizeof...(Components)>;

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
		 *********************************************************************/
		template<typename Fn>
		void ForEach(Fn&& fn)
		{
			Iterate(fn, kInvalidComponentId, 0, std::index_sequence_for<Components...>{});
		}

		/*********************************************************************
		 \brief
			Like ForEach(), but only visits rows whose Filter component was
			written after sinceTick.

		 \param[in]
			sinceTick The tick you last processed at.
		 \param[in]
			fn The callback to invoke per entity.

		 \note
			Filter on a component this query reads as const. Filtering on a
			component the query writes re-marks it on every visit, so the
			query would keep re-triggering itself.
		 *********************************************************************/
		template <typename Filter, typename Fn>
		void ForEachChangedSince(std::uint64_t sinceTick, Fn&& fn)
		{
			Iterate(fn, ComponentTypeId<Bare<Filter>>(), sinceTick, std::index_sequence_for<Components...>{});
		}

		/*********************************************************************
		 \brief  Counts how many entities currently match this query.
		 \return Total entity count across every matching archetype.
		 \details Calls Refresh() first, same as ForEach().
		 *********************************************************************/
		uint32_t CountEntities()
		{
			UseGuard inUse(*this);
			Refresh();
			uint32_t total = 0;
			for (Archetype* archetype : m_matching)
			{
				total += archetype->EntityCount();
			}
			return total;
		}

		/*********************************************************************
		 \brief
			Adds this query type's component access to a system's access
			declaration: `const T` is a read, non-const T is a write.

		 \param[in,out] access The access to extend (e.g. SystemDesc::access).

		 \note
			Static, so a system can declare its access in Describe() before
			the query itself has been created. Marks the access as declared.
		 *********************************************************************/
		static void AppendAccess(SystemAccess& access)
		{
			(AppendComponentAccess<Components>(access), ...);
			access.declared = true;
		}

	private:
		template <typename C>
		static void AppendComponentAccess(SystemAccess& access)
		{
			// const C is read-only; non-const C is written (and change-stamped) by ForEach.
			if constexpr (std::is_const_v<C>) access.reads.set(ComponentTypeId<Bare<C>>());
			else access.writes.set(ComponentTypeId<Bare<C>>());
		}

		template <typename C>
		static std::uint64_t* WritableTicks(Archetype& archetype, bool stamping)
		{
			if constexpr (std::is_const_v<C>) return nullptr;
			else return stamping ? archetype.GetColumn(ComponentTypeId<C>()).ChangedTicksData() : nullptr;
		}

		template <typename C>
		static void StampIfWritable(std::uint64_t* ticks, uint32_t row, std::uint64_t tick)
		{
			if constexpr (!std::is_const_v<C>)
			{
				if (ticks) ticks[row] = tick;
			}
		}

		template <typename Fn, std::size_t... I>
		void Iterate(Fn& fn, ComponentId filterId, std::uint64_t sinceTick, std::index_sequence<I...>)
		{
			// The running system must have declared everything this query touches.
			SystemAccess queryAccess;
			AppendAccess(queryAccess);
			CheckDeclaredAccess(queryAccess.reads, queryAccess.writes, "iterates a Query that");

			UseGuard inUse(*this);
			ArchetypeRegistry::IterationGuard guard(*m_registry);
			Refresh();

			const bool stamping = (m_worldTick != nullptr);
			const std::uint64_t tick = stamping ? *m_worldTick : 0;

			for (Archetype* archetype : m_matching)
			{
				const uint32_t count = archetype->EntityCount();
				if (count == 0) continue;

				const std::uint64_t* filterTicks = nullptr;
				if (filterId != kInvalidComponentId)
				{
					if (!archetype->Contains(filterId)) continue;
					filterTicks = archetype->GetColumn(filterId).ChangedTicksData();
				}

				const PointerArray columns{ archetype->GetColumnData(ComponentTypeId<Bare<Components>>())... };
				const TickArray writeTicks{ WritableTicks<Components>(*archetype, stamping)... };
				const std::vector<Entity>& entities = archetype->Entities();

				for (uint32_t row = 0; row < count; ++row)
				{
					if (filterTicks && filterTicks[row] <= sinceTick) continue;
					fn(entities[row], static_cast<Components*>(columns[I])[row]...);
					(StampIfWritable<Components>(writeTicks[I], row, tick), ...);
				}
			}
		}
	};

	/*************************************************************************
	 \brief
		Fluent builder for a Query<Required...> with Exclude<...>().
	 *************************************************************************/
	template <typename... Required>
	class QueryBuilder
	{
	public:
		explicit QueryBuilder(ArchetypeRegistry& registry, const std::uint64_t* worldTick = nullptr)
			: m_registry(&registry), m_worldTick(worldTick)
		{
			m_description.required = MakeSignature<std::remove_const_t<Required>...>();
		}

		//entities containing any of Excluded... will be skipped
		template <typename... Excluded>
		QueryBuilder& Exclude()
		{
			m_description.excluded = MakeSignature<Excluded...>();
			return *this;
		}

		//optional (reserved - recorded but not used by matching yet)
		template <typename... Optional_>
		QueryBuilder& Optional()
		{
			m_description.optional = MakeSignature<Optional_...>();
			return *this;
		}

		Query<Required...> Build()
		{
			Query<Required...> query(std::move(m_description), *m_registry, m_worldTick);
			query.Rebuild();
			return query;
		}

	private:
		QueryDescription m_description;
		ArchetypeRegistry* m_registry;
		const std::uint64_t* m_worldTick;
	};

}
