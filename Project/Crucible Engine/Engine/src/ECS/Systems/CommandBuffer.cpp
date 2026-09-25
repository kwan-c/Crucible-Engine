/*************************************************************************
* \file:         CommandBuffer.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          20 September 2026
* \version       1.2
* \brief
*                Implementation of CommandBuffer's chunk management,
*                DestroyEntity/SpawnEntity queuing, and Execute()/
*                Clear(). See CommandBuffer.h for the public interface,
*                the type-erasure/chunk layout design, and its
*                documented limits.
*
* \details
*                Every chunk is one plain ::operator new allocation
*                (aligned to kChunkAlign, see CommandBuffer.h); records
*                are packed into it back-to-back, each record's payload
*                aligned for its own type, and once written they never
*                move - so unlike ComponentColumn.cpp's Reserve(), there is
*                no relocation step here that could ever need an
*                exception-safety story of its own. Exception safety in
*                THIS file has two separate concerns instead: Execute(),
*                whose job is specifically to run caller-supplied World
*                calls that CAN throw - see its comment below for how it
*                guarantees no leaks either way; and Emplace()
*                (CommandBuffer.h), whose payload construction can also
*                throw for any non-trivially-constructible payload type
*                - see its comment there for how a failed construction
*                is rolled back instead of leaving a header with no
*                real payload behind it.
*
* \history
*    -v1.0       initial chunked-arena command buffer (destroy/add/remove/spawn)
*    -v1.1       fixed two bugs: (1) Allocate() always allocated a brand
*                new chunk once the tail was full, silently discarding
*                (leaking) any chunk already linked from a prior frame's
*                Clear()/Execute() reset - it now checks m_tail->next
*                for a reusable chunk first, and Chunk now tracks its
*                own capacity so an oversized chunk's real size is used
*                instead of the kChunkBytes constant; (2) Emplace() (see
*                CommandBuffer.h) used to write a fully "valid" header
*                before attempting to construct the payload, so a
*                throwing payload constructor left a record header with
*                no real payload behind it for Execute()/
*                DestroyPayloadsFrom() to misread later
*    -v1.2       per-payload alignment (AllocateRecord/TryPlace replace
*                Allocate), spawns created directly in their final
*                archetype with add hooks fired at the end of Execute()
*
**************************************************************************/

#include "CommandBuffer.h"

namespace Crucible
{
	CommandBuffer::CommandBuffer(std::size_t reserveBytes)
	{
		if (reserveBytes > 0)
		{
			m_head = m_tail = AllocateChunk(reserveBytes);
		}
	}

	// Every payload still queued gets destroyed (never executed) via
	// DestroyPayloadsFrom(), then every chunk's storage is freed.
	CommandBuffer::~CommandBuffer()
	{
		DestroyPayloadsFrom(m_head, 0);
		FreeChunks();
	}

	CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
		: m_head(other.m_head), m_tail(other.m_tail), m_count(other.m_count),
		m_spawnedEntities(std::move(other.m_spawnedEntities)),
		m_spawnSignatures(std::move(other.m_spawnSignatures))
	{
		other.m_head = nullptr;
		other.m_tail = nullptr;
		other.m_count = 0;
	}

	CommandBuffer& CommandBuffer::operator=(CommandBuffer&& other) noexcept
	{
		if (this == &other) return *this;

		DestroyPayloadsFrom(m_head, 0);
		FreeChunks();

		m_head = other.m_head;
		m_tail = other.m_tail;
		m_count = other.m_count;
		m_spawnedEntities = std::move(other.m_spawnedEntities);
		m_spawnSignatures = std::move(other.m_spawnSignatures);

		other.m_head = nullptr;
		other.m_tail = nullptr;
		other.m_count = 0;

		return *this;
	}

	// Queues World::DestroyEntity(entity) - already a no-op on a dead entity.
	void CommandBuffer::DestroyEntity(Entity entity)
	{
		struct Payload { Entity entity; };

		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer&)
			{
				const Payload* p = std::launder(reinterpret_cast<Payload*>(raw));
				world.DestroyEntity(p->entity);
			},
			Payload{ entity });
	}

	// Reserves the next spawn token and its (initially empty) final
	// signature - With<T>() calls fill the signature in before Execute()
	// runs - and queues the creation command.
	std::uint32_t CommandBuffer::RecordCreateSpawned()
	{
		const std::uint32_t token = static_cast<std::uint32_t>(m_spawnedEntities.size());
		m_spawnedEntities.push_back(kNullEntity);
		m_spawnSignatures.emplace_back();

		struct Payload { std::uint32_t token; };

		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer& commands)
			{
				const Payload* p = std::launder(reinterpret_cast<Payload*>(raw));
				// Created straight into its final archetype (no per-component
				// migrations). Add hooks are deferred to the end of Execute(),
				// once the With<T>() values have been written.
				const Entity entity = world.CreateEntityImpl(commands.m_spawnSignatures[p->token], false);
				commands.SetSpawnedEntity(p->token, entity);
			},
			Payload{ token });

		return token;
	}

	CommandBuffer::EntitySpawn CommandBuffer::SpawnEntity()
	{
		const std::uint32_t token = RecordCreateSpawned();
		return EntitySpawn(*this, token);
	}

	// Allocates one new chunk with at least minCapacity bytes of storage
	// after its header (kChunkBytes if that's bigger), NOT yet linked into
	// any list - callers link it in themselves.
	CommandBuffer::Chunk* CommandBuffer::AllocateChunk(std::size_t minCapacity)
	{
		const std::size_t capacity = RoundUp(minCapacity > kChunkBytes ? minCapacity : kChunkBytes, kRecordAlign);

		auto* raw = static_cast<std::byte*>(
			::operator new(kChunkHeaderBytes + capacity, std::align_val_t{ kChunkAlign }));

		Chunk* chunk = ::new (static_cast<void*>(raw)) Chunk();
		chunk->capacity = capacity;
		return chunk;
	}

	// Computes where a record would land at the end of chunk: the header at
	// the current write offset, the payload after it at the next address
	// aligned for the payload's own type. Returns false if it doesn't fit.
	bool CommandBuffer::TryPlace(Chunk* chunk, std::size_t payloadSize, std::size_t payloadAlign, RecordSlot& out) noexcept
	{
		std::byte* record = ChunkData(chunk) + chunk->used;   // used is always a multiple of kRecordAlign
		const std::size_t recordAddr = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(record));
		const std::size_t payloadAddr = RoundUp(recordAddr + sizeof(RecordHeader), payloadAlign);
		const std::size_t payloadOffset = payloadAddr - recordAddr;
		const std::size_t recordBytes = RoundUp(payloadOffset + payloadSize, kRecordAlign);

		if (chunk->used + recordBytes > chunk->capacity) return false;

		out.record = record;
		out.payloadOffset = static_cast<std::uint32_t>(payloadOffset);
		out.recordBytes = static_cast<std::uint32_t>(recordBytes);
		return true;
	}

	// Claims space for one record. Tries the tail chunk, then a chunk still
	// linked after it from an earlier frame (ResetChunkUsage keeps the chain
	// for reuse), and only then allocates a new chunk - spliced in ahead of
	// any spare chunks rather than replacing them, so none are leaked.
	CommandBuffer::RecordSlot CommandBuffer::AllocateRecord(std::size_t payloadSize, std::size_t payloadAlign)
	{
		RecordSlot slot;

		if (m_tail && TryPlace(m_tail, payloadSize, payloadAlign, slot))
		{
			m_tail->used += slot.recordBytes;
			return slot;
		}

		if (m_tail && m_tail->next && TryPlace(m_tail->next, payloadSize, payloadAlign, slot))
		{
			m_tail = m_tail->next;
			m_tail->used += slot.recordBytes;
			return slot;
		}

		// Worst case: header + maximum alignment padding + payload + tail rounding.
		Chunk* chunk = AllocateChunk(sizeof(RecordHeader) + payloadAlign + payloadSize + kRecordAlign);

		if (m_tail)
		{
			chunk->next = m_tail->next;
			m_tail->next = chunk;
		}
		else
		{
			m_head = chunk;
		}
		m_tail = chunk;

		[[maybe_unused]] const bool placed = TryPlace(chunk, payloadSize, payloadAlign, slot);
		assert(placed && "CommandBuffer: a freshly allocated chunk must fit the record it was sized for");
		m_tail->used += slot.recordBytes;
		return slot;
	}

	// Runs every queued command, in order, against world. Each record's
	// payload is destroyed immediately after it runs. If execute() throws,
	// that record's payload and every record after it are destroyed, the
	// buffer is reset for reuse, and the exception is rethrown - no leaks
	// either way.
	void CommandBuffer::Execute(World& world)
	{
		Chunk* chunk = m_head;
		std::size_t offset = 0;

		while (chunk)
		{
			if (offset >= chunk->used)
			{
				chunk = chunk->next;
				offset = 0;
				continue;
			}

			std::byte* record = ChunkData(chunk) + offset;
			const RecordHeader* header = std::launder(reinterpret_cast<RecordHeader*>(record));
			std::byte* payload = record + header->payloadOffset;
			const std::size_t nextOffset = offset + header->recordBytes;

			try
			{
				header->execute(payload, world, *this);
			}
			catch (...)
			{
				if (header->destroyPayload) header->destroyPayload(payload);
				DestroyPayloadsFrom(chunk, nextOffset);
				ResetForReuse();
				throw;
			}

			if (header->destroyPayload) header->destroyPayload(payload);
			offset = nextOffset;
		}

		// Spawned entities now hold their With<T>() values - fire their add hooks.
		try
		{
			for (Entity spawned : m_spawnedEntities) world.NotifyComponentsAdded(spawned);
		}
		catch (...)
		{
			ResetForReuse();
			throw;
		}

		ResetForReuse();
	}

	// Public "discard without running" entry point.
	void CommandBuffer::Clear() noexcept
	{
		DestroyPayloadsFrom(m_head, 0);
		ResetForReuse();
	}

	// Destroys the payload of every record from (chunk, offset) to the end
	// of the buffer WITHOUT running any of them. Does not touch chunk 'used'
	// counts or free anything - callers handle that afterwards.
	void CommandBuffer::DestroyPayloadsFrom(Chunk* chunk, std::size_t offset) noexcept
	{
		while (chunk)
		{
			while (offset < chunk->used)
			{
				std::byte* record = ChunkData(chunk) + offset;
				const RecordHeader* header = std::launder(reinterpret_cast<RecordHeader*>(record));

				if (header->destroyPayload) header->destroyPayload(record + header->payloadOffset);

				offset += header->recordBytes;
			}

			chunk = chunk->next;
			offset = 0;
		}
	}

	// Marks every chunk empty again without freeing any of them, and
	// rewinds m_tail to m_head - the whole chain stays linked so
	// AllocateRecord() can reuse it instead of reallocating every frame.
	void CommandBuffer::ResetChunkUsage() noexcept
	{
		for (Chunk* chunk = m_head; chunk; chunk = chunk->next)
		{
			chunk->used = 0;
		}
		m_tail = m_head;
	}

	void CommandBuffer::ResetForReuse() noexcept
	{
		ResetChunkUsage();
		m_count = 0;
		m_spawnedEntities.clear();
		m_spawnSignatures.clear();
	}

	// Frees every chunk's underlying allocation. Callers must have already
	// destroyed every payload via DestroyPayloadsFrom().
	void CommandBuffer::FreeChunks() noexcept
	{
		Chunk* chunk = m_head;
		while (chunk)
		{
			Chunk* next = chunk->next;
			chunk->~Chunk();
			::operator delete(static_cast<void*>(chunk), std::align_val_t{ kChunkAlign });
			chunk = next;
		}
		m_head = nullptr;
		m_tail = nullptr;
	}
}
