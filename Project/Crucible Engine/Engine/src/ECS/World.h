/*************************************************************************
* \file:         World.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.3
* \brief
*                composes EntityManager + ComponentRegistry into the single
*                API systems and gameplay code actually call
*
* \details
*                World is the single front-facing entry point for the
*                ECS. It owns an EntityManager (entity ids/liveness, see
*                ECSCore.h), a ComponentRegistry (per-type ComponentInfo,
*                see ComponentRegistry.h) and an ArchetypeRegistry (the
*                archetype table, see ArchetypeRegistry.h). Gameplay
*                code and systems (see Systems/IECSSystems.h and
*                ECSPhysicsSystem.h for an example system) should only
*                ever talk to World - it hides the archetype-move
*                bookkeeping in MoveEntity() (see World.cpp) behind
*                simple Add/Remove/Get/Has component calls.
*
* \history
*    -v1.0       skeleton
*    -v1.1       added member-level comments for readability, no logic changes
*    -v1.2       AddComponent/RemoveComponent now throw std::invalid_argument
*                on a dead/stale Entity in every build config, instead of
*                relying solely on an assert that NDEBUG strips out
*    -v1.3       hook symmetry (CreateEntity(s) fire add hooks, DestroyEntity
*                fires remove hooks), DestroyAllEntities, EmplaceComponent,
*                SetDeltaTime/DeltaTime, queries stamp change ticks, new
*                components relocated straight into their slot; all existing
*                public names and signatures unchanged
*
**************************************************************************/
#pragma once

#include "EngineExport.h"

//std libs
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

//header files
#include "Core/ECSCore.h"          // Entity, EntityManager
#include "Components/ComponentRegistry.h" // ComponentRegistry
#include "Components/ComponentSignature.h"
#include "Archetype/ArchetypeRegistry.h"
#include "Systems/Query.h"


namespace Crucible
{
	class CommandBuffer;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif

	/*************************************************************************
	 \brief
		Composes EntityManager + ComponentRegistry + ArchetypeRegistry into
		the single API systems and gameplay code actually call.

	 \details
		World is the single front-facing entry point for the ECS. It hides
		the archetype-move bookkeeping in MoveEntity() behind simple
		Add/Remove/Get/Has component calls; gameplay code and systems should
		only ever talk to World, never to EntityManager/Archetype/
		ArchetypeRegistry directly.

	 \note
		Non-copyable: owns all ECS storage (every entity, archetype and
		component column), so copying would require deep-cloning the entire
		ECS state, which is not supported.
	 *************************************************************************/
	class ENGINE_API World
	{
	public:

		/*********************************************************************
		 \brief  Constructs an empty world and registers every known
				 component type.
		 *********************************************************************/
		World();
		~World() = default;

		// World owns ECS storage, so copying it is not allowed
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		/*********************************************************************
		 \brief  Advances the tick every write this frame is stamped with.
				 Call once per frame, before systems update.
		 *********************************************************************/
		void AdvanceTick() { ++m_currentTick; }
		std::uint64_t CurrentTick() const { return m_currentTick; }

		/*********************************************************************
		 \brief  Sets this frame's delta time (seconds). Core calls this once
				 per frame, before systems update.
		 *********************************************************************/
		void SetDeltaTime(float dt) noexcept { m_deltaTime = dt; }

		/*********************************************************************
		 \brief  This frame's delta time in seconds, as set by SetDeltaTime().
		 *********************************************************************/
		float DeltaTime() const noexcept { return m_deltaTime; }

		/*********************************************************************
		 \brief  Registers component type T with this world's registry.
		 \tparam T The component type; must satisfy RegisteredComponent.
		 \return The ComponentId T was registered under.
		 *********************************************************************/
		template <typename T>
		ComponentId RegisterComponent() { return m_components.Register<T>(); }

		/*********************************************************************
		 \brief  This world's component registry.
		 \return Reference to the ComponentRegistry.
		 *********************************************************************/
		ComponentRegistry& Components() { return m_components; }

		/*********************************************************************
		 \brief  This world's archetype registry.
		 \return Reference to the ArchetypeRegistry.
		 *********************************************************************/
		ArchetypeRegistry& Archetypes() { return m_archetypes; }

		/*********************************************************************
		 \brief  Creates a new entity with no components.
		 \return A handle to the new entity, placed in the empty archetype.
		 *********************************************************************/
		Entity CreateEntity();

		/*********************************************************************
		 \brief
			Creates a new entity, placed directly into the archetype for a
			given component signature.

		 \param[in]
			signature The exact component set the new entity should start
			with.
		 \return
			A handle to the new entity. Every one of its components is
			default-constructed.

		 \details
			Creates the matching archetype first if it doesn't exist yet.
			Slightly cheaper than CreateEntity() + repeated AddComponent<T>()
			calls when the full component set is already known up front,
			since it avoids the intermediate archetype moves.

		 \note
			Fires OnComponentAdded for every component the entity starts with.
		 *********************************************************************/
		Entity CreateEntity(const ComponentSignature& signature);

		/*********************************************************************
		 \brief
			Creates count entities in one archetype, reserving storage once.

		 \param[in] count     Number of entities to create.
		 \param[in] signature Component set every new entity starts with.
		 \return The created entity handles.

		 \note
			Fires OnComponentAdded per entity, like CreateEntity(signature).
		 *********************************************************************/
		std::vector<Entity> CreateEntities(uint32_t count, const ComponentSignature& signature = ComponentSignature{});

		/*********************************************************************
		 \brief  Destroys an entity, freeing its slot for reuse.
		 \param[in] entity The entity to destroy.
		 \note  A no-op (not an error) if entity is already dead or stale.
				Fires OnEntityDestroyed, then OnComponentRemoved for every
				component the entity still owns, before tearing storage down.
		 *********************************************************************/
		void DestroyEntity(Entity entity);

		/*********************************************************************
		 \brief
			Destroys every live entity through DestroyEntity(), so every
			removal hook fires.

		 \note
			Call before tearing down anything those hooks release resources
			into (e.g. the renderer). Throws if called during Query iteration.
		 *********************************************************************/
		void DestroyAllEntities();

		/*********************************************************************
		 \brief  Whether an entity handle is still alive.
		 \param[in] e The handle to check.
		 \return true if e refers to a currently-alive entity.
		 *********************************************************************/
		bool IsAlive(Entity e)const { return m_entities.IsAlive(e); }

		/*********************************************************************
		 \brief  The exact component set an entity currently has.
		 \param[in] entity The entity to query.
		 \return Its archetype's signature, or an empty signature if entity
				 is dead/stale.
		 *********************************************************************/
		ComponentSignature GetSignature(Entity entity)const;

		/*********************************************************************
		 \brief  Whether an entity currently has a given component type.
		 \tparam T The component type to check for.
		 \param[in] entity The entity to check.
		 \return true if entity is alive and its archetype contains T.
		 *********************************************************************/
		template <typename T>
		bool HasComponent(Entity entity)const;

		/*********************************************************************
		 \brief  Fetches a pointer to an entity's instance of a component.
		 \tparam T The component type to fetch.
		 \param[in] entity The entity to query.
		 \return Pointer to the component, or nullptr if entity is dead/
				 stale or does not have T.
		 *********************************************************************/
		template <typename T>
		T* GetComponent(Entity entity);

		/*********************************************************************
		 \brief
			Adds (or overwrites) a component on an entity.

		 \tparam T
			The component type to add.
		 \param[in]
			entity The entity to modify.
		 \param[in]
			value The value to give the component; defaults to T{}.
		 \return
			Reference to the entity's (new or existing) T instance.

		 \details
			If entity already has a T, overwrites it in place with no
			archetype move. Otherwise moves the entity to the archetype for
			its signature plus T and relocates value straight into its new
			column slot (one move - no default-construct-then-assign).

		 \note
			Throws std::invalid_argument if entity is dead or stale, and
			std::logic_error if called while a Query is iterating.
		 *********************************************************************/
		template <typename T> T& AddComponent(Entity entity, T value = T{});

		/*********************************************************************
		 \brief
			Constructs a T from args and adds it (or overwrites an existing
			T), exactly like AddComponent(entity, T(args...)).

		 \tparam T    The component type to add.
		 \tparam Args Constructor argument types for T.
		 \return Reference to the entity's T instance.
		 *********************************************************************/
		template <typename T, typename... Args> T& EmplaceComponent(Entity entity, Args&&... args);

		/*********************************************************************
		 \brief
			Removes a component from an entity, if it has one.

		 \tparam T
			The component type to remove.
		 \param[in]
			entity The entity to modify.

		 \details
			A no-op if entity does not currently have a T. Otherwise fires
			OnComponentRemoved, then moves the entity to the archetype for
			its signature minus T.

		 \note
			Throws std::invalid_argument if entity is dead or stale, and
			std::logic_error if called while a Query is iterating.
		 *********************************************************************/
		template <typename T> void RemoveComponent(Entity entity);

		/*********************************************************************
		 \brief
			Registers a callback fired for every entity that gains a T,
			AFTER the component's value has been written.

		 \details
			Fires for AddComponent/EmplaceComponent (when T is newly added),
			CreateEntity(signature), CreateEntities and CommandBuffer spawns.
			Overwriting an existing T does not fire it.

		 \note
			Runs under the iteration guard: hooks may read, but must queue
			structural changes through a CommandBuffer. Registering a hook
			during Query iteration or hook dispatch throws std::logic_error.
		 *********************************************************************/
		template <typename T>
		void OnComponentAdded(std::function<void(World&, Entity, T&)> callback);

		/*********************************************************************
		 \brief
			Registers a callback fired for every entity about to lose a T,
			BEFORE the component is torn down (so the callback can still
			read it - e.g. release a GPU handle it holds).

		 \details
			Fires on RemoveComponent AND on DestroyEntity, so resource-owning
			hooks always pair up with their add.
		 *********************************************************************/
		template <typename T>
		void OnComponentRemoved(std::function<void(World&, Entity, T&)> callback);

		/*********************************************************************
		 \brief
			Registers a callback fired for every entity about to be
			destroyed, BEFORE its component data is torn down.
		 *********************************************************************/
		void OnEntityDestroyed(std::function<void(World&, Entity)> callback);

		/*********************************************************************
		 \brief
			Builds a Query over a fixed set of required component types.

		 \tparam T
			The component types the query requires and will iterate.
			`const T` is read-only access; non-const T is write access and
			gets its change tick stamped on iteration.
		 \return
			A Query<T...> whose matching-archetype list has already been
			built (Rebuild() is called before returning).

		 \note
			Build once (e.g. on a system's first Update) and keep it - the
			query only re-scans archetypes when a new one has been created.
		 *********************************************************************/
		template<typename ...T>
		Query<T... > CreateQuery();

		/*********************************************************************
		 \brief  Fluent builder for a query with Exclude<...>().
		 *********************************************************************/
		template<typename ...T>
		QueryBuilder<T...> BuildQuery() { return QueryBuilder<T...>(m_archetypes, &m_currentTick); }

		/*********************************************************************
		 \brief  Tick T was last written at for entity, or 0 if never (or if
				 entity is dead / doesn't have T).
		 *********************************************************************/
		template <typename T>
		std::uint64_t LastChanged(Entity entity) const;

		/*********************************************************************
		 \brief  Stamps T's change tick for entity with the current tick.
		 *********************************************************************/
		template <typename T>
		void MarkChanged(Entity entity);

	private:
		friend class CommandBuffer;
		using ComponentHookFn = std::function<void(World&, Entity, void*)>;

		EntityRecord& RequireMutableEntity(Entity entity, const char* operation);
		Entity CreateEntityImpl(const ComponentSignature& signature, bool fireAddHooks);
		void NotifyComponentsAdded(Entity entity);

		/*********************************************************************
		 \brief
			Relocates an entity's row from its current archetype into
			destination, carrying over every shared component.

		 \details
			For each component destination requires: if the source archetype
			already has it, moves the existing value across; if it is
			addedId, relocates *addedValue straight into the new slot;
			otherwise default-constructs it. Afterwards removes the entity's
			old row from the source archetype and fixes up the row index of
			whichever entity got swapped into that now-vacant row.
		 *********************************************************************/
		void MoveEntity(EntityRecord& record, Entity entity, Archetype& destination,
			ComponentId addedId = kInvalidComponentId, void* addedValue = nullptr);

		template <typename T> T& OverwriteComponent(EntityRecord& record, ComponentId id, T&& value);
		template <typename T> T& InsertComponent(EntityRecord& record, Entity entity, ComponentId id, T&& value);

		void FireAddHooks(ComponentId id, Entity entity, void* component);
		void FireRemoveHooks(ComponentId id, Entity entity, void* component);
		void FireAddHooksForRow(Archetype& archetype, uint32_t row, Entity entity);
		void FireRemoveHooksForRow(Archetype& archetype, uint32_t row, Entity entity);

		EntityManager     m_entities;
		ComponentRegistry m_components;
		ArchetypeRegistry m_archetypes;
		std::uint64_t     m_currentTick = 1;
		float             m_deltaTime = 0.f;

		std::array<std::vector<ComponentHookFn>, kMaxComponents> m_onAddHooks;
		std::array<std::vector<ComponentHookFn>, kMaxComponents> m_onRemoveHooks;
		std::vector<std::function<void(World&, Entity)>> m_onDestroyHooks;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

	template <typename T>
	void World::OnComponentAdded(std::function<void(World&, Entity, T&)> callback)
	{
		if (m_archetypes.IsIterating())
			throw std::logic_error("World::OnComponentAdded: register hooks outside queries and hook dispatch");

		m_onAddHooks[ComponentTypeId<T>()].push_back(
			[cb = std::move(callback)](World& world, Entity entity, void* component)
			{
				cb(world, entity, *static_cast<T*>(component));
			});
	}

	template <typename T>
	void World::OnComponentRemoved(std::function<void(World&, Entity, T&)> callback)
	{
		if (m_archetypes.IsIterating())
			throw std::logic_error("World::OnComponentRemoved: register hooks outside queries and hook dispatch");

		m_onRemoveHooks[ComponentTypeId<T>()].push_back(
			[cb = std::move(callback)](World& world, Entity entity, void* component)
			{
				cb(world, entity, *static_cast<T*>(component));
			});
	}

	template <typename T>
	bool World::HasComponent(Entity e)const
	{
		if (!m_entities.IsAlive(e)) return false;
		return m_entities.Record(e).archetype->Contains(ComponentTypeId<T>());
	}

	template <typename T>
	T* World::GetComponent(Entity e)
	{
		if (!m_entities.IsAlive(e)) return nullptr;

		const EntityRecord& record = m_entities.Record(e);
		const ComponentId id = ComponentTypeId<T>();

		if (!record.archetype->Contains(id)) return nullptr;
		return static_cast<T*>(record.archetype->GetComponentPtr(id, record.row));
	}

	template <typename T>
	std::uint64_t World::LastChanged(Entity entity) const
	{
		if (!m_entities.IsAlive(entity)) return 0;
		const EntityRecord& record = m_entities.Record(entity);
		const ComponentId id = ComponentTypeId<T>();
		if (!record.archetype->Contains(id)) return 0;
		return record.archetype->GetColumn(id).LastChanged(record.row);
	}

	template <typename T>
	void World::MarkChanged(Entity entity)
	{
		assert(m_entities.IsAlive(entity) && "MarkChanged on a dead/stale Entity");
		if (!m_entities.IsAlive(entity)) return;

		const EntityRecord& record = m_entities.Record(entity);
		const ComponentId id = ComponentTypeId<T>();
		if (!record.archetype->Contains(id)) return;

		record.archetype->GetColumn(id).Touch(record.row, m_currentTick);
	}

	template<typename T>
	inline T& World::AddComponent(Entity entity, T value)
	{
		EntityRecord& record = RequireMutableEntity(entity, "AddComponent");
		const ComponentId id = ComponentTypeId<T>();

		if (record.archetype->Contains(id)) return OverwriteComponent<T>(record, id, std::move(value));
		return InsertComponent<T>(record, entity, id, std::move(value));
	}

	template <typename T, typename... Args>
	inline T& World::EmplaceComponent(Entity entity, Args&&... args)
	{
		return AddComponent<T>(entity, T(std::forward<Args>(args)...));
	}

	template<typename T>
	inline void World::RemoveComponent(Entity entity)
	{
		EntityRecord& record = RequireMutableEntity(entity, "RemoveComponent");
		const ComponentId id = ComponentTypeId<T>();

		if (!record.archetype->Contains(id)) return;

		FireRemoveHooks(id, entity, record.archetype->GetComponentPtr(id, record.row));

		Archetype* destination = m_archetypes.GetRemoveDestination(*record.archetype, id);
		MoveEntity(record, entity, *destination);
	}

	template <typename T>
	T& World::OverwriteComponent(EntityRecord& record, ComponentId id, T&& value)
	{
		T& existing = *static_cast<T*>(record.archetype->GetComponentPtr(id, record.row));
		existing = std::move(value);
		record.archetype->GetColumn(id).Touch(record.row, m_currentTick);
		return existing;
	}

	template <typename T>
	T& World::InsertComponent(EntityRecord& record, Entity entity, ComponentId id, T&& value)
	{
		Archetype* destination = m_archetypes.GetAddDestination(*record.archetype, id);
		MoveEntity(record, entity, *destination, id, &value);   // value is relocated straight into its slot

		T& slot = *static_cast<T*>(destination->GetComponentPtr(id, record.row));
		destination->GetColumn(id).Touch(record.row, m_currentTick);
		FireAddHooks(id, entity, &slot);
		return slot;
	}

	template<typename... T>
	Query<T...>World::CreateQuery()
	{
		QueryDescription description;
		description.required = MakeSignature<std::remove_const_t<T>...>();
		Query<T...>query(std::move(description), m_archetypes, &m_currentTick);
		query.Rebuild();
		return query;
	}
}
