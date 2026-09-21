/*************************************************************************
* \file:         CommandBuffer.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          20 September 2026
* \version       1.0
* \brief
*                Declares CommandBuffer: records structural ECS changes
*                (create/destroy entity, add/remove component) made
*                while iterating a Query, and replays them safely once
*                iteration has finished.
*
* \details
*
* \history
*    -v1.0       initial chunked-arena command buffer (destroy/add/remove/spawn)
*
*
**************************************************************************/

#pragma once

//
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
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

		void DestroyEntity(Entity entity);


		template <typename T>
		void AddComponent(Entity enity, T value = T{});

		template <typename T>
		void RemoveComponent(Entity entity);


		class EntitySpawn;

		EntitySpawn SpawnEntity();

		void Execute(World& world);

		void Clear()noexcept;

		bool Empty()const noexcept { return m_count == 0; }
		std::size_t CommandCount()const noexcept { return m_count; }

	private:

		struct RecordHeader
		{
			void(*execute)(std::byte* payload, World& world, CommandBuffer& commands);

			void(*destroyPayload)(std::byte* payload);

			std::uint32_t payloadSize;
		};

		struct Chunk
		{
			Chunk* next = nullptr;
			std::size_t used = 0;
			std::size_t capacity = 0;
		};

		static constexpr std::size_t kPayloadAlign = (alignof(std::max_align_t) > 32) ? alignof(std::max_align_t) : 32;

		static constexpr std::size_t kHeaderBytes = (sizeof(RecordHeader) + kPayloadAlign - 1) & ~(kPayloadAlign - 1);

		static constexpr std::size_t kChunkHeaderBytes = (sizeof(Chunk) + kPayloadAlign - 1) & ~(kPayloadAlign - 1);

		static constexpr std::size_t kChunkBytes = 4096 - kChunkHeaderBytes;

		static constexpr std::size_t RoundUpToPayloadAlign(std::size_t bytes)
		{
			return (bytes + kPayloadAlign - 1) & ~(kPayloadAlign - 1);
		}

		static Chunk* AllocateChunk(std::size_t minCapacity);

		std::byte* Allocate(std::size_t payloadBytes);

		template<typename Payload,typename ExecuteFn , typename ... Args>
		void Emplace(ExecuteFn, Args&&... args);

		void DestroyPayloadsFrom(Chunk* chunk, std::size_t offset)noexcept;

		void ResetChunkUsage()noexcept;

		void ResetForReuse() noexcept;

		void FreeChunks()noexcept;

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
		void RecordSetSpawnedComponent(std::uint32_t token, T value);

		Chunk* m_head = nullptr;
		Chunk* m_tail = nullptr;
		std::size_t m_count = 0;

		std::vector<Entity> m_spawnedEntities;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif


	class CommandBuffer::EntitySpawn
	{
	public:
		// Queues "add component T (default-valued unless given) to
		// the entity this spawn creates" and returns *this so calls
		// can be chained.
		template <typename T>
		EntitySpawn& With(T value = T{})
		{
			m_buffer->RecordSetSpawnedComponent<T>(m_token, std::move(value));
			return *this;
		}

	private:
		friend class CommandBuffer;
		EntitySpawn(CommandBuffer& buffer, std::uint32_t token) :m_buffer(&buffer), m_token(token) {}

		CommandBuffer* m_buffer;
		std::uint32_t m_token;
	};

	template <typename T>
	void CommandBuffer::AddComponent(Entity entity, T value)
	{
		struct Payload { Entity entity; T value; };

		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer&)
			{
				Payload* p = reinterpret_cast<Payload*>(raw);
				world.AddComponent<T>(p->entity, std::move(p->value));
			},
			Payload{ entity, std::move(value) });
	}

	// See declaration above.
	template <typename T>
	void CommandBuffer::RemoveComponent(Entity entity)
	{
		struct Payload { Entity entity; };

		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer&)
			{
				Payload* p = reinterpret_cast<Payload*>(raw);
				world.RemoveComponent<T>(p->entity);
			},
			Payload{ entity });
	}

	template <typename T>
	void CommandBuffer::RecordSetSpawnedComponent(std::uint32_t token, T value)
	{
		struct Payload { std::uint32_t token; T value; };

		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer& commands)
			{
				Payload* p = reinterpret_cast<Payload*>(raw);
				world.AddComponent<T>(commands.GetSpawnedEntity(p->token), std::move(p->value));
			},
			Payload{ token, std::move(value) });
	}


	template <typename Payload, typename ExecuteFn, typename... Args>
	void CommandBuffer::Emplace(ExecuteFn executeFn, Args&&... args)
	{
		static_assert(alignof(Payload) <= kPayloadAlign,
			"CommandBuffer payload requires stricter alignment than kPayloadAlign supports - see the LIMITS note in CommandBuffer.h");

		const std::size_t recordBytes = kHeaderBytes + RoundUpToPayloadAlign(sizeof(Payload));
		std::byte* recordStart = Allocate(sizeof(Payload));
		std::byte* payload = recordStart + kHeaderBytes;

		try
		{
			::new (static_cast<void*>(payload)) Payload(std::forward<Args>(args)...);
		}
		catch (...)
		{
			// Construction failed - nothing valid exists at 'payload'
			// (any subobjects of Payload that DID finish constructing
			// before the failure are already unwound by the language
			// itself). Hand the space Allocate() just claimed back to
			// the chunk so the next successful Emplace() call reuses
			// it, and let the caller see the real exception rather
			// than leaving a RecordHeader for a payload that was never
			// actually built.
			m_tail->used -= recordBytes;
			throw;
		}

		auto* header = reinterpret_cast<RecordHeader*>(recordStart);
		header->execute = executeFn;
		header->payloadSize = static_cast<std::uint32_t>(sizeof(Payload));

		if constexpr (std::is_trivially_destructible_v<Payload>)
		{
			header->destroyPayload = nullptr;
		}
		else
		{
			header->destroyPayload = [](std::byte* p) { reinterpret_cast<Payload*>(p)->~Payload(); };
		}

		++m_count;
	}
}