/*************************************************************************
* \file:         CommandBuffer.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          20 September 2026
* \version       1.1
* \brief
*                Implementation of CommandBuffer's chunk management,
*                DestroyEntity/SpawnEntity queuing, and Execute()/
*                Clear(). See CommandBuffer.h for the public interface,
*                the type-erasure/chunk layout design, and its
*                documented limits.
*
* \details
*                Every chunk is one plain ::operator new allocation
*                (aligned to kPayloadAlign, see CommandBuffer.h);
*                records are packed into it back-to-back and, once
*                written, never move - so unlike ComponentColumn.cpp's
*                Reserve(), there is no relocation step here that could
*                ever need an exception-safety story of its own.
*                Exception safety in THIS file has two separate
*                concerns instead: Execute(), whose job is specifically
*                to run caller-supplied World calls that CAN throw (see
*                World::AddComponent/RemoveComponent's dead-entity/
*                iteration checks, World.h) - see its comment below for
*                how it guarantees no leaks either way; and Emplace()
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
*
*
**************************************************************************/

#include "Commandbuffer.h"

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

	// Steals other's chunk list and spawn-token table wholesale (no
	// per-record work needed - the records themselves never move),
	// then resets 'other' to an empty, unbound state.
	CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
		: m_head(other.m_head), m_tail(other.m_tail), m_count(other.m_count),
		m_spawnedEntities(std::move(other.m_spawnedEntities))
	{
		other.m_head = nullptr;
		other.m_tail = nullptr;
		other.m_count = 0;
	}

	// Destroys/frees this buffer's own current contents first, then
	// steals other's the same way the move constructor does.
	CommandBuffer& CommandBuffer::operator=(CommandBuffer&& other) noexcept
	{
		if (this == &other) return *this;

		DestroyPayloadsFrom(m_head, 0);
		FreeChunks();

		m_head = other.m_head;
		m_tail = other.m_tail;
		m_count = other.m_count;
		m_spawnedEntities = std::move(other.m_spawnedEntities);

		other.m_head = nullptr;
		other.m_tail = nullptr;
		other.m_count = 0;

		return *this;
	}

	// Queues World::DestroyEntity(entity) (see World.h). Trivially
	// destructible payload (just an Entity), so destroyPayload is left
	// null - see Emplace()'s comment in CommandBuffer.h.
	void CommandBuffer::DestroyEntity(Entity entity)
	{
		struct Payload { Entity entity; };

		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer&)
			{
				Payload* p = reinterpret_cast<Payload*>(raw);
				world.DestroyEntity(p->entity);
			},
			Payload{ entity });
	}

	// Reserves the next spawn token (the new size of m_spawnedEntities
	// after appending a kNullEntity placeholder - overwritten once the
	// CreateSpawned command below actually runs), queues the creation
	// command, and returns the token so SpawnEntity() can hand it to
	// the EntitySpawn it returns.
	std::uint32_t CommandBuffer::RecordCreateSpawned()
	{
		const std::uint32_t token = static_cast<std::uint32_t>(m_spawnedEntities.size());
		m_spawnedEntities.push_back(kNullEntity);

		struct Payload { std::uint32_t token; };

		Emplace<Payload>(
			[](std::byte* raw, World& world, CommandBuffer& commands)
			{
				Payload* p = reinterpret_cast<Payload*>(raw);
				const Entity entity = world.CreateEntity();
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

	// Allocates one new chunk with at least 'minCapacity' bytes of
	// storage after its (alignment-padded) header - kChunkBytes if
	// that's bigger, so ordinary small commands still land in a
	// normal-sized, reusable chunk; only a single record that's
	// genuinely larger than kChunkBytes gets an oversized chunk to
	// itself. Placement-constructs the Chunk header at the start of
	// the raw allocation (default member initializers set next=nullptr,
	// used=0) with its actual capacity recorded (see Chunk::capacity
	// in CommandBuffer.h - needed so Allocate() below can correctly
	// tell whether an oversized chunk still has room, rather than
	// assuming every chunk is exactly kChunkBytes), and returns it,
	// NOT yet linked into any list - callers link it in themselves.
	CommandBuffer::Chunk* CommandBuffer::AllocateChunk(std::size_t minCapacity)
	{
		const std::size_t capacity = minCapacity > kChunkBytes ? minCapacity : kChunkBytes;

		auto* raw = static_cast<std::byte*>(
			::operator new(kChunkHeaderBytes + capacity, std::align_val_t{ kPayloadAlign }));

		Chunk* chunk = ::new (static_cast<void*>(raw)) Chunk();
		chunk->capacity = capacity;
		return chunk;
	}

	// Ensures the tail chunk has room for a record of
	// kHeaderBytes + RoundUpToPayloadAlign(payloadBytes) bytes, then
	// claims that space and returns a pointer to the start of it
	// (where the caller, Emplace(), writes the RecordHeader followed
	// by the payload).
	//
	// When the current tail doesn't have room: a chunk already linked
	// at m_tail->next - left there by a PRIOR frame's ResetChunkUsage()
	// (see below), which resets every chunk's 'used' back to 0 but
	// deliberately keeps the whole chain intact for exactly this reuse
	// - is checked first and reused (m_tail just advances to it) if it
	// has enough capacity. Only if there is no such chunk, or it's too
	// small for this specific record, is a new one actually allocated,
	// and even then it is SPLICED in ahead of whatever m_tail->next
	// already was rather than replacing it outright - so a too-small
	// leftover chunk from a bigger previous frame is never discarded,
	// only pushed further down the chain where it can still be reused
	// later. This is the fix for a real bug: the original version
	// always called AllocateChunk() once the tail was full and
	// unconditionally overwrote m_tail->next, silently leaking every
	// chunk that had been linked there by an earlier frame.
	std::byte* CommandBuffer::Allocate(std::size_t payloadBytes)
	{
		const std::size_t recordBytes = kHeaderBytes + RoundUpToPayloadAlign(payloadBytes);

		if (!m_tail || m_tail->used + recordBytes > m_tail->capacity)
		{
			if (m_tail && m_tail->next && m_tail->next->capacity >= recordBytes)
			{
				m_tail = m_tail->next;
			}
			else
			{
				Chunk* chunk = AllocateChunk(recordBytes);

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
			}
		}

		std::byte* recordStart = reinterpret_cast<std::byte*>(m_tail) + kChunkHeaderBytes + m_tail->used;
		m_tail->used += recordBytes;
		return recordStart;
	}

	// Runs every queued command, in order, against 'world'. See the
	// declaration in CommandBuffer.h for the strong exception
	// guarantee this provides; the mechanics are: walk the chunk list
	// exactly once, and for each record call its execute() then
	// immediately its destroyPayload() (if any) - so by the time this
	// loop finishes normally, every payload has already been destroyed
	// one at a time as it was consumed. If execute() throws for some
	// record, that record's own payload is destroyed right there in
	// the catch block, then DestroyPayloadsFrom() cleans up every
	// record still queued AFTER it (records before it were already
	// destroyed by prior loop iterations), the buffer is reset for
	// reuse, and the exception is rethrown.
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

			std::byte* base = reinterpret_cast<std::byte*>(chunk) + kChunkHeaderBytes;
			auto* header = reinterpret_cast<RecordHeader*>(base + offset);
			std::byte* payload = base + offset + kHeaderBytes;
			const std::size_t recordBytes = kHeaderBytes + RoundUpToPayloadAlign(header->payloadSize);

			// Where the NEXT record starts, computed before running
			// this one so the exception path below knows exactly
			// where to resume cleanup regardless of what execute()
			// does to 'world' (it can't touch this buffer's own chunk
			// layout).
			std::size_t nextOffset = offset + recordBytes;
			Chunk* nextChunk = chunk;
			if (nextOffset >= chunk->used)
			{
				nextChunk = chunk->next;
				nextOffset = 0;
			}

			try
			{
				header->execute(payload, world, *this);
			}
			catch (...)
			{
				if (header->destroyPayload) header->destroyPayload(payload);
				DestroyPayloadsFrom(nextChunk, nextOffset);
				ResetForReuse();
				throw;
			}

			if (header->destroyPayload) header->destroyPayload(payload);

			chunk = nextChunk;
			offset = nextOffset;
		}

		// Every payload was already destroyed above as each record
		// ran - only the buffer's own bookkeeping needs resetting.
		ResetForReuse();
	}

	// Public "discard without running" entry point: everything still
	// queued gets destroyed (nothing has run yet, so this is the only
	// place a full DestroyPayloadsFrom(m_head, 0) is correct), then the
	// buffer is reset for reuse.
	void CommandBuffer::Clear() noexcept
	{
		DestroyPayloadsFrom(m_head, 0);
		ResetForReuse();
	}

	// Walks every record from ('chunk', 'offset') to the end of the
	// buffer and destroys its payload (skipping records whose
	// destroyPayload is null - see RecordHeader in CommandBuffer.h),
	// WITHOUT running any of them. Does not touch chunk 'used' counts
	// or free anything - callers (Clear(), Execute()'s exception path)
	// handle resetting/freeing themselves afterward.
	void CommandBuffer::DestroyPayloadsFrom(Chunk* chunk, std::size_t offset) noexcept
	{
		while (chunk)
		{
			std::byte* base = reinterpret_cast<std::byte*>(chunk) + kChunkHeaderBytes;

			while (offset < chunk->used)
			{
				auto* header = reinterpret_cast<RecordHeader*>(base + offset);
				std::byte* payload = base + offset + kHeaderBytes;

				if (header->destroyPayload) header->destroyPayload(payload);

				offset += kHeaderBytes + RoundUpToPayloadAlign(header->payloadSize);
			}

			chunk = chunk->next;
			offset = 0;
		}
	}

	// Marks every chunk empty again (used = 0) without freeing any of
	// them, and rewinds m_tail to m_head so the next Allocate() call
	// starts writing from the very first chunk again. Deliberately
	// leaves every chunk's 'next' pointer untouched - the whole chain
	// stays linked so Allocate() can walk forward and reuse it as
	// storage is needed again, instead of freeing and reallocating
	// every frame (see Allocate()'s comment above for the reuse path
	// itself).
	void CommandBuffer::ResetChunkUsage() noexcept
	{
		for (Chunk* chunk = m_head; chunk; chunk = chunk->next)
		{
			chunk->used = 0;
		}
		m_tail = m_head;
	}

	// Shared tail end of both Clear() and Execute()'s success path:
	// resets chunk usage (see ResetChunkUsage() above), the queued
	// command count, and the spawn-token table (std::vector::clear()
	// keeps its capacity, so a buffer that spawns entities every frame
	// doesn't reallocate that table each time either).
	void CommandBuffer::ResetForReuse() noexcept
	{
		ResetChunkUsage();
		m_count = 0;
		m_spawnedEntities.clear();
	}

	// Frees every chunk's underlying allocation and empties the chunk
	// list. Callers (the destructor, move-assignment) are responsible
	// for having already destroyed every payload via
	// DestroyPayloadsFrom() first.
	void CommandBuffer::FreeChunks() noexcept
	{
		Chunk* chunk = m_head;
		while (chunk)
		{
			Chunk* next = chunk->next;
			chunk->~Chunk();
			::operator delete(static_cast<void*>(chunk), std::align_val_t{ kPayloadAlign });
			chunk = next;
		}
		m_head = nullptr;
		m_tail = nullptr;
	}
}
