/*************************************************************************
* \file:         ArchetypeRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.2
* \brief
*                Declares ArchetypeRegistry: owns every Archetype that
*                currently exists and maps a ComponentSignature to its
*                (unique) Archetype instance.
*
* \details
*                World (see World.h) owns exactly one
*                ArchetypeRegistry. Every archetype that has ever been
*                needed (i.e. requested via FindOrCreate, directly or
*                through GetAddDestination/GetRemoveDestination) lives
*                here for the lifetime of the World, keyed by its
*                ComponentSignature (see ComponentSignature.h). See
*                ArchetypeRegistry.cpp for the implementation, and
*                Archetype.h/.cpp for what an individual archetype
*                stores. Version() is a cheap change counter (bumped
*                only when a brand new archetype is created) that
*                Query (see Systems/Query.h) uses to skip redundant
*                rebuilds of its matching-archetype list.
*
* \history
*    -v1.0       initial signature->archetype map + add/remove destination lookup
*    -v1.1       GetAdd/RemoveDestination now use Archetype's edge cache
*                instead of always hashing+looking up; added a Version()
*                change counter for Query's rebuild-skipping; exposed
*                ArchetypesWith()
*    -v1.2       archetype creation private to World, IterationGuard only
*                constructible by World/Query, atomic iteration depth
*
**************************************************************************/
#pragma once

#include <atomic>
#include <cassert>
#include <memory>
#include <unordered_map>
#include <vector>


#include "EngineExport.h"

#include "Archetype.h"


namespace Crucible
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
	/*************************************************************************
	 \brief
		Owns every Archetype that currently exists and maps a
		ComponentSignature to its (unique) Archetype instance.

	 \details
		World owns exactly one ArchetypeRegistry. Every archetype that has
		ever been needed (via FindOrCreate, directly or through
		GetAddDestination/GetRemoveDestination) lives here for the lifetime
		of the World, keyed by its ComponentSignature.

	 \note
		Non-copyable in practice: owns Archetypes via std::unique_ptr and
		exposes only non-owning Archetype* elsewhere.
	 *************************************************************************/
	class ENGINE_API ArchetypeRegistry
	{
	public:
		/*********************************************************************
		 \brief
			Constructs the registry and creates the empty (no-components)
			archetype.

		 \param[in]
			components The ComponentRegistry used to build every archetype's
			ComponentColumns; must outlive this registry.
		 *********************************************************************/
		explicit ArchetypeRegistry(const ComponentRegistry& components);

		ArchetypeRegistry(const ArchetypeRegistry&) = delete;
		ArchetypeRegistry& operator=(const ArchetypeRegistry&) = delete;
		ArchetypeRegistry(ArchetypeRegistry&&) = delete;
		ArchetypeRegistry& operator=(ArchetypeRegistry&&) = delete;

		/*********************************************************************
		 \brief  Looks up an existing archetype by signature.
		 \param[in] signature The exact component set to look up.
		 \return Pointer to the matching archetype, or nullptr if none
				 exists yet.
		 *********************************************************************/
		Archetype* Find(const ComponentSignature& signature);

		/*********************************************************************
		 \brief  The archetype with no components.
		 \return Pointer to the empty archetype, created in the constructor.
		 *********************************************************************/
		Archetype* EmptyArchetype()const { return m_empty; }

		/*********************************************************************
		 \brief  Every archetype that currently exists.
		 \return Reference to the full archetype list, in creation order.
		 *********************************************************************/
		const std::vector<Archetype*>& All()const { return m_all; }

		/*********************************************************************
		\brief  Every archetype that contains a given component type.
		\param[in] id The component type to filter by.
		\return Reference to the matching archetype list.
		*********************************************************************/
		const std::vector<Archetype*>& ArchetypesWith(ComponentId id)const { return m_componentToArchetypes[id]; }


		/*********************************************************************
		 \brief
			A change counter, incremented each time a brand-new archetype is
			created.

		 \return
			The current version.

		 \details
			Query compares this against the version it last rebuilt from, so
			it can skip re-scanning every archetype when nothing new has
			been created since.
		 *********************************************************************/
		std::uint64_t Version()const { return m_version; }


		// True while at least one IterationGuard below is currently
		// alive for this registry - i.e. some Query is mid-ForEach
		// over it right now. World's structural mutators (see
		// World.h/.cpp) check this before touching any archetype.
		bool IsIterating()const noexcept { return m_iterationDepth.load(std::memory_order_acquire) > 0; }

		// RAII reentrancy guard.While an IterationGuard is alive,
		// IsIterating() above returns true for this registry.
		// Query<T...>::ForEach (see Query.h) holds one for the
		// duration of its loop, guaranteeing - even if the caller's
		// per-row callback throws - that the depth is always
		// decremented again once the loop actually finishes. Depth is
		// a counter, not a single flag, so a Query::ForEach started
		// from INSIDE another Query::ForEach's callback (a legitimate,
		// read-only thing to do) nests correctly: the outer guard's
		// protection isn't dropped early just because an inner one
		// also finished. The counter is atomic so queries running on
		// different threads can hold guards at the same time.
		class IterationGuard
		{
		public:
			~IterationGuard() { m_registry.EndIteration(); }

			// A second guard for the same iteration would double-count
			// depth on copy/move without a matching second callback
			// lifetime to justify it - there is exactly one guard per
			// ForEach() call, held by value on its stack, so neither
			// operation is meaningful.
			IterationGuard(const IterationGuard&) = delete;
			IterationGuard& operator=(const IterationGuard&) = delete;
			IterationGuard(IterationGuard&&) = delete;
			IterationGuard& operator=(IterationGuard&&) = delete;

		private:
			// Only the ECS itself (queries and World's hook dispatch) opens guards.
			friend class World;
			template <typename... Components> friend class Query;

			explicit IterationGuard(ArchetypeRegistry& registry) :m_registry(registry) { m_registry.BeginIteration(); }

			ArchetypeRegistry& m_registry;
		};

	private:
		// Archetypes are created only as a side effect of World's structural changes.
		friend class World;

		/*********************************************************************
		 \brief  Looks up an archetype by signature, creating and
				 registering it first if it doesn't exist yet (bumps
				 Version()).
		 \note  Throws std::invalid_argument for an unregistered component.
		 *********************************************************************/
		Archetype* FindOrCreate(const ComponentSignature& signature);

		/*********************************************************************
		 \brief  The archetype reached by adding id to source; uses and
				 fills the edge cache in both directions.
		 *********************************************************************/
		Archetype* GetAddDestination(Archetype& source, ComponentId id);

		/*********************************************************************
		 \brief  The archetype reached by removing id from source; uses and
				 fills the edge cache in both directions.
		 *********************************************************************/
		Archetype* GetRemoveDestination(Archetype& source, ComponentId id);

		// Nested classes have access to their enclosing class's
		// private members, so IterationGuard above can call these
		// directly - kept private themselves since incrementing/
		// decrementing outside a properly RAII-scoped guard would be
		// exactly the kind of easy-to-mismatch bookkeeping this whole
		// mechanism exists to avoid.
		void BeginIteration() noexcept { m_iterationDepth.fetch_add(1, std::memory_order_acq_rel); }
		void EndIteration() noexcept
		{
			[[maybe_unused]] const std::uint32_t previous = m_iterationDepth.fetch_sub(1, std::memory_order_acq_rel);
			assert(previous > 0 && "IterationGuard end/begin mismatch");
		}

		/*********************************************************************
		 \brief  Indexes a newly created archetype into
				 m_componentToArchetypes.
		 \param[in] archetype The archetype that was just created.
		 *********************************************************************/
		void OnArchetypeCreated(Archetype& archetype);

		const ComponentRegistry& m_components;

		std::unordered_map < ComponentSignature, std::unique_ptr <Archetype>, SignatureHash> m_archetypes;

		std::vector<Archetype*> m_all;
		Archetype* m_empty = nullptr;

		std::vector<std::vector<Archetype*>> m_componentToArchetypes;

		std::uint64_t m_version = 0;

		// Depth of currently-alive IterationGuards for this registry.
		// See IsIterating()/IterationGuard above.
		std::atomic<std::uint32_t> m_iterationDepth{ 0 };
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
