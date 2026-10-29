/*************************************************************************
* \file:         CommandBuffer.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          20 September 2026
* \version       1.4
* \brief
*                Declares CommandBuffer: records structural ECS changes
*                (create/destroy entity, add/remove component) made
*                while iterating a Query, and replays them safely once
*                iteration has finished.
*
* \details
*                Records are packed into reusable chunks; each record is a
*                header followed by its payload, aligned for that payload's
*                own type. Replay is tolerant: a command whose target entity
*                died earlier in the same buffer is skipped rather than
*                aborting the rest of the buffer. Spawned entities are
*                created directly in their final archetype, and their
*                OnComponentAdded hooks fire at the end of Execute(), after
*                their With<T>() values have been written.
*
* \history
*    -v1.0       initial chunked-arena command buffer (destroy/add/remove/spawn)
*    -v1.1       tolerant replay (dead targets skipped), spawns created in
*                their final archetype with add hooks deferred to the end of
*                Execute(), per-payload alignment instead of a global 32 bytes
*    -v1.2       spawns are all-or-nothing when Execute() throws (spawns whose
*                add hooks never ran are removed silently); EntitySpawn
*                handles carry a private epoch so a stale handle can't touch
*                a reused spawn token after Execute()/Clear()/move
*    -v1.3       hierarchy commands (SetParent, DetachChildren,
*                SetSiblingIndex, EntitySpawn::WithParent); HierarchyNode
*                can't be written/added through the buffer; mandatory base
*                components can't be removed
*    -v1.4       SetComponentEnabled (typed and by ComponentId);
*                EntitySpawn::WithDisabled (components born switched off)
*
**************************************************************************/

#pragma once

//
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "EngineExport.h"

//
#include "../World.h"

namespace  Crucible
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
	class ENGINE_API CommandBuffer
	{
	public:

		CommandBuffer() = default;
		~CommandBuffer();

		explicit CommandBuffer(std::size_t reserveBytes);

		CommandBuffer(const CommandBuffer&) = delete;
		CommandBuffer& operator=(const CommandBuffer&) = delete;
		CommandBuffer(CommandBuffer&& other) noexcept;
		CommandBuffer& operator=(CommandBuffer&& other) noexcept;

		/*********************************************************************
		 \brief  Queues destruction of entity (no-op on replay if already dead).
		 *********************************************************************/
		void DestroyEntity(Entity entity);

		/*********************************************************************
		 \brief  Queues World::AddComponent<T> (add or overwrite) on entity.
		 \note   Skipped on replay if entity has died by then.
		 *********************************************************************/
		template <typename T>
		void AddComponent(Entity entity, T value = T{});

		/*********************************************************************
		 \brief  Queues World::RemoveComponent<T> on entity.
		 \note   Skipped on replay if entity has died by then. The mandatory
				 base components can't be removed (RemovableComponent).
		 *********************************************************************/
		template <RemovableComponent T>
		void RemoveComponent(Entity entity);

		/*********************************************************************
		 \brief  Queues World::TrySetParent(child, parent, worldPositionStays)
				 (Unity's SetParent; kNullEntity makes child a root).
		 \note   An invalid request (cycle, self-parent, dead parent) is
				 logged and skipped on replay, never thrown, so one bad
				 request can't fail the system that sent it.
		 *********************************************************************/
		void SetParent(Entity child, Entity parent, bool worldPositionStays = true);

		/*********************************************************************
		 \brief  Queues World::DetachChildren(parent). No-op on replay if
				 parent has died; if a child can't keep its world transform
				 (zero-scale parent) nothing is detached and it is logged,
				 never thrown.
		 *********************************************************************/
		void DetachChildren(Entity parent);

		/*********************************************************************
		 \brief  Queues World::SetSiblingIndex(entity, index). No-op on
				 replay if entity has died.
		 *********************************************************************/
		void SetSiblingIndex(Entity entity, std::uint32_t index);

		/*********************************************************************
		 \brief  Queues World::SetComponentEnabled<T>(entity, enabled).
		 \details Replayed in order with every other command in this buffer:
				  switch-off-then-DestroyEntity switches off (hooks fire) and
				  then destroys; DestroyEntity- or RemoveComponent-then-switch
				  is skipped. Repeated switches all replay in order - the last
				  one wins, and every real change of a component's active
				  state fires its hooks. Never moves the entity between
				  archetypes.
		 \note   Skipped on replay if entity has died or no longer has T.
		 *********************************************************************/
		template <ToggleableComponent T>
		void SetComponentEnabled(Entity entity, bool enabled);

		/*********************************************************************
		 \brief  SetComponentEnabled by ComponentId (scripting, tools): queues
				 World::TrySetComponentEnabled. Skipped on replay if the
				 entity is dead, lacks the component, or it can't be
				 switched off.
		 *********************************************************************/
		void SetComponentEnabled(Entity entity, ComponentId id, bool enabled);


		class EntitySpawn;

		/*********************************************************************
		 \brief  Queues creation of a new entity; chain With<T>() on the
				 result to give it components. It always gets the mandatory
				 base components too (see kMandatoryComponents), in the same
				 single archetype placement.
		 \note   The returned EntitySpawn is valid only until this buffer is
				 executed, cleared or moved; With<T>() on it afterwards
				 throws std::logic_error.
		 *********************************************************************/
		EntitySpawn SpawnEntity();

		/*********************************************************************
		 \brief  Replays every queued command in order, then resets the
				 buffer for reuse (chunk storage is kept).
		 \details Spawned entities' OnComponentAdded hooks fire at the end,
				 after every other command in this buffer has run.
		 \note   Lifecycle-hook exceptions never reach here (World catches
				 and logs them - see World::OnComponentAdded). If a command
				 itself fails (e.g. an allocation), commands already replayed
				 stay applied, and every spawned entity whose add hooks have
				 not started yet is removed without firing any hooks, so a
				 spawn is either fully created and hooked or absent. The
				 buffer is reset and the exception is rethrown.
		 *********************************************************************/
		void Execute(World& world);

		void Clear()noexcept;

		bool Empty()const noexcept { return m_count == 0; }
		std::size_t CommandCount()const noexcept { return m_count; }

	private:

		using ExecuteFn = void(*)(std::byte* payload, World& world, CommandBuffer& commands);
		using DestroyFn = void(*)(std::byte* payload);

		struct RecordHeader
		{
			ExecuteFn     execute;
			DestroyFn     destroyPayload;   // null for trivially destructible payloads
			std::uint32_t payloadOffset;    // record start -> payload
			std::uint32_t recordBytes;      // header + padding + payload, multiple of kRecordAlign
		};

		struct Chunk
		{
			Chunk* next = nullptr;
			std::size_t used = 0;
			std::size_t capacity = 0;
		};

		struct RecordSlot
		{
			std::byte*    record = nullptr;
			std::uint32_t payloadOffset = 0;
			std::uint32_t recordBytes = 0;
		};

		static constexpr std::size_t kRecordAlign = alignof(RecordHeader);
		static constexpr std::size_t kChunkAlign = alignof(std::max_align_t);

		static constexpr std::size_t RoundUp(std::size_t value, std::size_t align)
		{
			return (value + align - 1) & ~(align - 1);
		}

		// Spelled out rather than RoundUp(): a static member initializer can't call a
		// constexpr member function while the class is still incomplete.
		static constexpr std::size_t kChunkHeaderBytes = (sizeof(Chunk) + kChunkAlign - 1) & ~(kChunkAlign - 1);
		static constexpr std::size_t kChunkBytes = 4096 - kChunkHeaderBytes;

		static std::byte* ChunkData(Chunk* chunk) noexcept { return reinterpret_cast<std::byte*>(chunk) + kChunkHeaderBytes; }

		static Chunk* AllocateChunk(std::size_t minCapacity);

		static bool TryPlace(Chunk* chunk, std::size_t payloadSize, std::size_t payloadAlign, RecordSlot& out) noexcept;

		RecordSlot AllocateRecord(std::size_t payloadSize, std::size_t payloadAlign);

		template<typename Payload, typename Fn, typename ... Args>
		void Emplace(Fn executeFn, Args&&... args);

		void DestroyPayloadsFrom(Chunk* chunk, std::size_t offset)noexcept;

		void ResetChunkUsage()noexcept;

		void ResetForReuse() noexcept;

		void FreeChunks()noexcept;

		// Silently removes spawned entities [firstUnnotified, end) that are still
		// alive - their add hooks never ran, so no remove hooks may run either.
		void DiscardUnnotifiedSpawns(World& world, std::size_t firstUnnotified) noexcept;

		Entity GetSpawnedEntity(std::uint32_t token) const
		{
			assert(token < m_spawnedEntities.size());
			return m_spawnedEntities[token];
		}

		void SetSpawnedEntity(std::uint32_t token, Entity entity)
		{
			assert(token < m_spawnedEntities.size());
			m_spawnedEntities[token] = entity;
		}


		std::uint32_t RecordCreateSpawned();


		template <typename T>
		void RecordSetSpawnedComponent(std::uint32_t token, std::uint32_t epoch, T value);

		void RecordSpawnParent(std::uint32_t token, std::uint32_t epoch, Entity parent, bool worldPositionStays);
		void RecordSpawnDisabled(std::uint32_t token, std::uint32_t epoch, ComponentId id);

		// Throws std::logic_error if an EntitySpawn handle outlived its buffer epoch.
		void RequireLiveSpawn(std::uint32_t token, std::uint32_t epoch, const char* operation) const;

		Chunk* m_head = nullptr;
		Chunk* m_tail = nullptr;
		std::size_t m_count = 0;

		std::vector<Entity> m_spawnedEntities;
		std::vector<ComponentSignature> m_spawnSignatures;   // final component set per spawn token
		std::vector<ComponentSignature> m_spawnDisabled;     // per spawn token: components born switched off (WithDisabled)

		// Bumped whenever spawn tokens are reset (Execute/Clear) or the buffer
		// is moved from, so an EntitySpawn from an earlier epoch is rejected.
		std::uint32_t m_spawnEpoch = 0;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif


	class CommandBuffer::EntitySpawn
	{
	public:
		/*********************************************************************
		 \brief  Queues "the entity this spawn creates starts with component
				 T = value (default-valued unless given)"; returns *this so
				 calls can be chained.
		 \details For a mandatory component (Transform, WorldTransform) the
				  entity has it anyway: this only sets its initial value, it
				  never adds a second one. OnComponentAdded<T> fires once, at
				  the end of Execute(), and sees this value (see
				  World::OnComponentAdded).
		 *********************************************************************/
		template <typename T>
		EntitySpawn& With(T value = T{})
		{
			static_assert(!WorldManaged<T>::value, "HierarchyNode links are managed by World: a spawn always starts as a root - use WithParent() (or CommandBuffer::SetParent) to parent it");
			m_buffer->RecordSetSpawnedComponent<T>(m_token, m_epoch, std::move(value));
			return *this;
		}

		/*********************************************************************
		 \brief  Queues SetParent(spawned entity, parent, worldPositionStays)
				 right after the With<T>() values given so far.
		 \details The default false keeps the Transform given to
				  With<Transform>() as LOCAL to parent, like Unity's
				  Instantiate(prefab, parent). Call it after With<Transform>():
				  a later With<Transform>() would overwrite the solved local.
				  A refused request (dead parent) is logged and the spawn
				  stays a root. If the same buffer later destroys parent,
				  the spawn goes with it (DestroyEntity cascades) - its
				  remove hooks then run although its add hooks never did,
				  which hooks must already tolerate (World::OnComponentAdded).
		 *********************************************************************/
		EntitySpawn& WithParent(Entity parent, bool worldPositionStays = false)
		{
			m_buffer->RecordSpawnParent(m_token, m_epoch, parent, worldPositionStays);
			return *this;
		}

		/*********************************************************************
		 \brief  The spawned entity's T starts switched off; its data is
				 still whatever With<T>() gave it. Call after With<T>().
		 \details Applied the moment the entity is created, before anything
				  else in the buffer can see it and before its add hooks
				  run, so it is never seen switched on. Born switched off is
				  not a change: no OnComponentActiveChanged and no event.
				  Several components may be switched off; repeating it is
				  harmless.
		 \note   Throws std::logic_error if With<T>() was not called first.
		 *********************************************************************/
		template <ToggleableComponent T>
		EntitySpawn& WithDisabled()
		{
			m_buffer->RecordSpawnDisabled(m_token, m_epoch, ComponentTypeId<T>());
			return *this;
		}

	private:
		friend class CommandBuffer;
		EntitySpawn(CommandBuffer& buffer, std::uint32_t token, std::uint32_t epoch)
			:m_buffer(&buffer), m_token(token), m_epoch(epoch) {}

		CommandBuffer* m_buffer;
		std::uint32_t m_token;
		std::uint32_t m_epoch;   // buffer epoch this token belongs to
	};

	template <typename T>
	void CommandBuffer::AddComponent(Entity entity, T value)
	{
		static_assert(!WorldManaged<T>::value, "This component is managed by World (e.g. HierarchyNode): use CommandBuffer::SetParent");
		struct Payload { Entity entity; T value; };

		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer&)
			{
				Payload* p = std::launder(reinterpret_cast<Payload*>(raw));
				// Deferred commands race earlier deferred destroys in the same
				// buffer: a target that died first is a no-op, not an error.
				if (!world.IsAlive(p->entity)) return;
				world.AddComponent<T>(p->entity, std::move(p->value));
			},
			Payload{ entity, std::move(value) });
	}

	template <RemovableComponent T>
	void CommandBuffer::RemoveComponent(Entity entity)
	{
		struct Payload { Entity entity; };

		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer&)
			{
				const Payload* p = std::launder(reinterpret_cast<Payload*>(raw));
				if (!world.IsAlive(p->entity)) return;
				world.RemoveComponent<T>(p->entity);
			},
			Payload{ entity });
	}

	template <ToggleableComponent T>
	void CommandBuffer::SetComponentEnabled(Entity entity, bool enabled)
	{
		struct Payload { Entity entity; bool enabled; };
		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer&)
			{
				const Payload* p = std::launder(reinterpret_cast<Payload*>(raw));
				// Destroyed, or lost T, earlier in the buffer (or since recording): nothing to switch.
				if (!world.HasComponent<T>(p->entity)) return;
				world.SetComponentEnabled<T>(p->entity, p->enabled);
			},
			Payload{ entity, enabled });
	}

	template <typename T>
	void CommandBuffer::RecordSetSpawnedComponent(std::uint32_t token, std::uint32_t epoch, T value)
	{
		RequireLiveSpawn(token, epoch, "With");

		// The spawn is created with T already in its archetype (see RecordCreateSpawned).
		// For a mandatory T the bit is already implied, so this changes nothing.
		m_spawnSignatures[token].set(ComponentTypeId<T>());

		struct Payload { std::uint32_t token; T value; };

		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer& commands)
			{
				Payload* p = std::launder(reinterpret_cast<Payload*>(raw));
				const Entity entity = commands.GetSpawnedEntity(p->token);
				if (!world.IsAlive(entity)) return;
				world.AddComponent<T>(entity, std::move(p->value));   // overwrite in place - no migration
			},
			Payload{ token, std::move(value) });
	}


	template <typename Payload, typename Fn, typename... Args>
	void CommandBuffer::Emplace(Fn executeFn, Args&&... args)
	{
		const RecordSlot slot = AllocateRecord(sizeof(Payload), alignof(Payload));
		std::byte* payload = slot.record + slot.payloadOffset;

		try
		{
			::new (static_cast<void*>(payload)) Payload(std::forward<Args>(args)...);
		}
		catch (...)
		{
			// Construction failed - nothing valid exists at 'payload'. Hand the
			// record's space back (it is always the last thing placed in the
			// tail chunk) so no header is left pointing at an unbuilt payload.
			m_tail->used -= slot.recordBytes;
			throw;
		}

		RecordHeader* header = ::new (static_cast<void*>(slot.record)) RecordHeader{};
		header->execute = executeFn;
		header->payloadOffset = slot.payloadOffset;
		header->recordBytes = slot.recordBytes;

		if constexpr (std::is_trivially_destructible_v<Payload>)
		{
			header->destroyPayload = nullptr;
		}
		else
		{
			header->destroyPayload = [](std::byte* p) { std::launder(reinterpret_cast<Payload*>(p))->~Payload(); };
		}

		++m_count;
	}
}
