/*************************************************************************
* \file:         World.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.3
* \brief
*                Implementation of World: entity creation/destruction
*                and the archetype-move logic used by
*                AddComponent/RemoveComponent. See World.h for the
*                public interface and member comments.
*
* \details
*                World's constructor wires the ArchetypeRegistry to the
*                ComponentRegistry and registers every known component
*                type (see ComponentRegistry::RegisterAll in
*                ComponentRegistry.cpp). CreateEntity/DestroyEntity are
*                thin wrappers around EntityManager (see ECSCore.h) and
*                Archetype (see Archetype.cpp). MoveEntity() is the
*                shared relocation routine used whenever an entity's
*                component set changes (Add/RemoveComponent in
*                World.h).
*
*                Failure handling: every structural operation does all of
*                its throwing work (allocation, component construction)
*                before it changes anything observable, and undoes partial
*                work on failure. Lifecycle hooks are isolated: a throwing
*                hook is logged and counted, never propagated.
*
* \history
*    -v1.0       initial CreateEntity/DestroyEntity/GetSignature/MoveEntity
*    -v1.1       hook symmetry (creation fires add hooks, destruction fires
*                per-component remove hooks), DestroyAllEntities, hooks
*                cannot be registered mid-iteration, MoveEntity relocates
*                the added value instead of default-construct + assign
*    -v1.2       DestroyEntity body moved into DestroyEntityImpl(entity,
*                fireHooks) so CommandBuffer can silently roll back spawns
*                whose add hooks never ran
*    -v1.3       exception-safe creation (slot released on failure),
*                all-or-nothing CreateEntities, reserve-then-move
*                MoveEntity, signature/count validation, catch-log-continue
*                hook dispatch, structural-change enforcement for running
*                systems
*
**************************************************************************/

#include "World.h"

#include <array>
#include <cassert>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>

#include <Diagnostics/Logger.h>

namespace Crucible
{

	//build archetype registry over m_components
	World::World() :m_archetypes(m_components)
	{
		m_components.RegisterAll();
	}

	void World::RequireStructuralChange(const char* operation) const
	{
		// Structural changes during iteration would invalidate the rows being walked.
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error(std::string("World::") + operation +
				" called while a Query is iterating - use a CommandBuffer instead (see CommandBuffer.h)");
		}
		// Declared systems may run alongside others, so they must defer structural changes.
		// The message is only built when such a system is running (no allocation on the normal path).
		if (Detail::RestrictedSystem())
		{
			CheckStructuralAccess((std::string("World::") + operation).c_str());
		}
	}

	EntityRecord& World::RequireMutableEntity(Entity entity, const char* operation)
	{
		RequireStructuralChange(operation);
		if (!m_entities.IsAlive(entity))
		{
			throw std::invalid_argument(std::string("World::") + operation + " called with a dead/stale Entity");
		}
		return m_entities.Record(entity);
	}

	Entity World::CreateEntity()
	{
		return CreateEntityImpl(ComponentSignature{}, true);
	}

	Entity World::CreateEntity(const ComponentSignature& signature)
	{
		return CreateEntityImpl(signature, true);
	}

	Entity World::CreateEntityImpl(const ComponentSignature& signature, bool fireAddHooks)
	{
		RequireStructuralChange("CreateEntity");
		m_components.ValidateSignature(signature, "World::CreateEntity");

		// Resolve the archetype before taking a slot, so a failure here leaks nothing.
		Archetype* archetype = m_archetypes.FindOrCreate(signature);
		const Entity entity = m_entities.Create();

		uint32_t row = 0;
		try
		{
			row = archetype->AddEntityDefault(entity);
		}
		catch (...)
		{
			// The archetype is unchanged (strong guarantee); give the slot back.
			m_entities.Release(entity);
			throw;
		}

		EntityRecord& record = m_entities.RecordAt(entity.index);
		record.archetype = archetype;
		record.row = row;

		archetype->TouchAll(row, m_currentTick);

		if (fireAddHooks) FireAddHooksForRow(*archetype, row, entity);

		return entity;
	}

	std::vector<Entity> World::CreateEntities(uint32_t count, const ComponentSignature& signature)
	{
		RequireStructuralChange("CreateEntities");
		m_components.ValidateSignature(signature, "World::CreateEntities");

		std::vector<Entity> created;
		if (count == 0) return created;

		if (count > std::numeric_limits<uint32_t>::max() - m_entities.AliveCount())
		{
			throw std::length_error("World::CreateEntities: count " + std::to_string(count) + " exceeds the remaining entity index space");
		}

		created.reserve(count);
		Archetype* archetype = m_archetypes.FindOrCreate(signature);
		archetype->Reserve(count);
		m_entities.Reserve(count);

		// Build every entity first; hooks fire only once the whole batch exists.
		try
		{
			for (uint32_t i = 0; i < count; ++i)
			{
				const Entity entity = m_entities.Create();
				uint32_t row = 0;
				try
				{
					row = archetype->AddEntityDefault(entity);
				}
				catch (...)
				{
					m_entities.Release(entity);
					throw;
				}

				EntityRecord& record = m_entities.RecordAt(entity.index);
				record.archetype = archetype;
				record.row = row;
				archetype->TouchAll(row, m_currentTick);

				created.push_back(entity);   // capacity reserved above
			}
		}
		catch (...)
		{
			// All-or-nothing: remove this call's entities newest first (no hooks have run).
			for (auto it = created.rbegin(); it != created.rend(); ++it) DestroyEntityImpl(*it, false);
			throw;
		}

		for (Entity entity : created)
		{
			const EntityRecord& record = m_entities.Record(entity);
			FireAddHooksForRow(*record.archetype, record.row, entity);
		}
		return created;
	}

	void World::NotifyComponentsAdded(Entity entity)
	{
		if (!m_entities.IsAlive(entity)) return;
		const EntityRecord& record = m_entities.Record(entity);
		FireAddHooksForRow(*record.archetype, record.row, entity);
	}

	void World::OnEntityDestroyed(std::function<void(World&, Entity)> callback)
	{
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error("World::OnEntityDestroyed: register hooks outside queries and hook dispatch");
		}
		m_onDestroyHooks.push_back(std::move(callback));
	}

	void World::ReportHookFailure(const char* hookKind, ComponentId id, Entity entity, const char* what) noexcept
	{
		++m_hookFailures;
		try
		{
			const char* component = id == kInvalidComponentId ? "-" : m_components.Get(id).name.data();
			CRU_ERROR("World: {} hook threw for entity {}:{} (component {}) - {}; the operation completed, the hook's own side effects were not undone",
				hookKind, entity.index, entity.generation, component, what);
		}
		catch (...)
		{
			// Logging must never turn a contained hook failure into a new one.
		}
	}

	void World::FireAddHooks(ComponentId id, Entity entity, void* component) noexcept
	{
		if (m_onAddHooks[id].empty()) return;
		ArchetypeRegistry::IterationGuard guard(m_archetypes);   // hooks may read, never restructure

		// Each hook is isolated: one failing hook doesn't stop the others.
		for (auto& hook : m_onAddHooks[id])
		{
			try { hook(*this, entity, component); }
			catch (const std::exception& e) { ReportHookFailure("OnComponentAdded", id, entity, e.what()); }
			catch (...) { ReportHookFailure("OnComponentAdded", id, entity, "non-std exception"); }
		}
	}

	void World::FireRemoveHooks(ComponentId id, Entity entity, void* component) noexcept
	{
		if (m_onRemoveHooks[id].empty()) return;
		ArchetypeRegistry::IterationGuard guard(m_archetypes);

		for (auto& hook : m_onRemoveHooks[id])
		{
			try { hook(*this, entity, component); }
			catch (const std::exception& e) { ReportHookFailure("OnComponentRemoved", id, entity, e.what()); }
			catch (...) { ReportHookFailure("OnComponentRemoved", id, entity, "non-std exception"); }
		}
	}

	void World::FireDestroyHooks(Entity entity) noexcept
	{
		if (m_onDestroyHooks.empty()) return;
		ArchetypeRegistry::IterationGuard guard(m_archetypes);

		for (auto& hook : m_onDestroyHooks)
		{
			try { hook(*this, entity); }
			catch (const std::exception& e) { ReportHookFailure("OnEntityDestroyed", kInvalidComponentId, entity, e.what()); }
			catch (...) { ReportHookFailure("OnEntityDestroyed", kInvalidComponentId, entity, "non-std exception"); }
		}
	}

	void World::FireAddHooksForRow(Archetype& archetype, uint32_t row, Entity entity) noexcept
	{
		for (ComponentId id : archetype.ComponentIds())
		{
			if (!m_onAddHooks[id].empty()) FireAddHooks(id, entity, archetype.GetComponentPtr(id, row));
		}
	}

	void World::FireRemoveHooksForRow(Archetype& archetype, uint32_t row, Entity entity) noexcept
	{
		for (ComponentId id : archetype.ComponentIds())
		{
			if (!m_onRemoveHooks[id].empty()) FireRemoveHooks(id, entity, archetype.GetComponentPtr(id, row));
		}
	}


	void World::DestroyEntity(Entity entity)
	{
		DestroyEntityImpl(entity, true);
	}

	void World::DestroyEntityImpl(Entity entity, bool fireHooks)
	{
		RequireStructuralChange("DestroyEntity");

		if (!m_entities.IsAlive(entity)) return;

		EntityRecord& record = m_entities.Record(entity);

		if (fireHooks)
		{
			FireDestroyHooks(entity);

			// Every component the entity still owns counts as removed, so
			// resource-owning OnComponentRemoved hooks fire here too - not only
			// on RemoveComponent().
			FireRemoveHooksForRow(*record.archetype, record.row, entity);
		}

		// Nothing below can throw: swap-removal and slot release are noexcept.
		const Entity relocated = record.archetype->RemoveEntity(record.row);
		if (relocated.IsValid())
			m_entities.RecordAt(relocated.index).row = record.row;
		m_entities.Destroy(entity);
	}

	void World::DestroyAllEntities()
	{
		RequireStructuralChange("DestroyAllEntities");

		std::vector<Entity> alive;
		alive.reserve(m_entities.AliveCount());
		for (Archetype* archetype : m_archetypes.All())
		{
			alive.insert(alive.end(), archetype->Entities().begin(), archetype->Entities().end());
		}

		for (Entity entity : alive) DestroyEntity(entity);
	}


	ComponentSignature World::GetSignature(Entity entity) const
	{
		if (!m_entities.IsAlive(entity)) return ComponentSignature{};

		return m_entities.Record(entity).archetype->Signature();
	}


	void World::MoveEntity(EntityRecord& record, Entity entity, Archetype& destination, ComponentId addedId, void* addedValue)
	{
		Archetype& source = *record.archetype;
		assert(&source != &destination);

		// 1. Allocate everything up front: afterwards only constructors can throw.
		destination.ReserveForAppend();

		// 2. Build the destination-only columns (no source value, not the added one) - undone on failure.
		std::array<ComponentId, kMaxComponents> built{};
		std::size_t builtCount = 0;
		try
		{
			for (ComponentId id : destination.ComponentIds())
			{
				if (source.Contains(id) || (id == addedId && addedValue != nullptr)) continue;
				destination.GetColumn(id).PushBackDefault();
				built[builtCount++] = id;
			}
		}
		catch (...)
		{
			while (builtCount > 0) destination.GetColumn(built[--builtCount]).PopBack();
			throw;
		}

		// 3. Non-throwing relocation of shared components and the added value.
		for (ComponentId id : destination.ComponentIds())
		{
			ComponentColumn& column = destination.GetColumn(id);
			if (source.Contains(id))
			{
				source.GetColumn(id).MoveElementTo(record.row, column);
			}
			else if (id == addedId && addedValue != nullptr)
			{
				column.PushBackMoved(addedValue);   // one relocation instead of default-construct + move-assign
			}
		}

		const uint32_t destinationRow = destination.AppendEntityReserved(entity);

		// 4. Drop the old row (noexcept) and fix up whichever entity was swapped into it.
		const Entity relocated = source.RemoveEntity(record.row);
		if (relocated.IsValid())
		{
			m_entities.RecordAt(relocated.index).row = record.row;
		}

		record.archetype = &destination;
		record.row = destinationRow;
	}


}
