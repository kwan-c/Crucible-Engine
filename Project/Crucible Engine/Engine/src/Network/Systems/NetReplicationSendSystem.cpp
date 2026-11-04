/*************************************************************************
* \file:         NetReplicationSendSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                NetReplicationSendSystem: the host's lifecycle stream
*                (world on join, spawns, despawns).
*
* \history
*    -v1.0       lifecycle only (Networking Guide 9.3, 9.4, PR 8)
*
**************************************************************************/
#include "Network/Systems/NetReplicationSendSystem.h"

#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Network/ByteStream.h"
#include "Network/NetComponents.h"
#include "Network/NetContent.h"
#include "Network/NetworkSession.h"

#include <algorithm>
#include <array>
#include <format>

namespace Crucible
{
	namespace
	{
		std::uint8_t FullMask(const NetPrefabDesc& desc) noexcept
		{
			return static_cast<std::uint8_t>((1u << desc.replicated.size()) - 1u);
		}

		std::size_t VarU32Bytes(std::uint32_t v) noexcept
		{
			std::size_t n = 1;
			while (v >= 0x80u) { v >>= 7; ++n; }
			return n;
		}

		// u8 type, u32 lifecycleSeq, u32 serverTick, u16 count
		constexpr std::size_t kSpawnHeaderBytes = 1 + 4 + 4 + 2;
	}

	NetReplicationSendSystem::NetReplicationSendSystem(NetworkSession& session, const NetContent& content)
		: m_session(session), m_content(content), m_message(kMaxReliableBytes)
	{
	}

	void NetReplicationSendSystem::Describe(SystemDesc& desc) const
	{
		desc.Exclusive();   // serializers read any component; failed spawns are destroyed here
	}

	void NetReplicationSendSystem::Update(World& world, SystemContext&)
	{
		// Per-client replication state for destroyed ids arrives with snapshots; until then the
		// list is only drained, in every role, so it cannot grow.
		m_session.TakeDestroyedThisFrame(m_destroyed);
		m_destroyed.clear();
		if (!m_session.IsServer())
		{
			// A session that stopped mid-frame may leave these behind; they belong to no one now.
			m_session.TakeSpawned(m_spawned);
			m_spawned.clear();
			return;
		}

		m_session.TakeSpawned(m_spawned);
		m_session.TakePendingDespawns(m_despawned);

		// Decided before anything is sent: a peer that gets the world this frame is not an
		// incremental receiver until next frame.
		m_joining.clear();
		m_incremental.clear();
		for (const PeerState& peer : m_session.Peers())
		{
			if (!peer.IsJoined()) continue;
			if (peer.needsWorld) m_joining.push_back(peer.peer);
			else m_incremental.push_back(peer.peer);
		}

		m_state.clear();
		m_failed.clear();

		if (!m_joining.empty())
		{
			m_worldIds.clear();
			for (const auto& [id, entity] : m_session.Entities()) m_worldIds.push_back(id);
			std::ranges::sort(m_worldIds);   // the same order on every join: easier to read in logs and tests
			BuildRecords(world, m_worldIds, m_worldRecords);
			for (const Net::PeerId peer : m_joining) SendWorld(peer, m_worldRecords);
		}

		if (!m_incremental.empty() && (!m_spawned.empty() || !m_despawned.empty()))
		{
			BuildRecords(world, m_spawned, m_spawnRecords);
			for (const Net::PeerId peer : m_incremental)
				if (SendSpawnBatches(peer, m_spawnRecords)) SendDespawnBatches(peer, m_despawned);
		}

		DestroyFailed(world);
		m_spawned.clear();
		m_despawned.clear();
	}

	void NetReplicationSendSystem::BuildRecords(const World& world, std::span<const Net::NetworkId> ids, std::vector<Record>& out)
	{
		out.clear();
		const Net::NetTick tick = m_session.Clock().Tick();
		std::array<std::byte, kMaxRecordBytes> body;
		for (const Net::NetworkId id : ids)
		{
			const Entity entity = m_session.Entities().Find(id);
			if (!world.IsAlive(entity)) continue;   // spawned and destroyed in the same frame
			const NetworkIdentity* identity = world.Read<NetworkIdentity>(entity);
			if (identity == nullptr) continue;

			const NetPrefabDesc* desc = m_content.Prefabs().Desc(identity->prefab);
			if (desc == nullptr)
			{
				m_failed.push_back({ id, identity->prefab, "not a registered network prefab" });
				continue;
			}
			const std::uint8_t mask = FullMask(*desc);
			ByteWriter writer(body);
			std::string error;
			if (!m_content.Serializers().WriteRecord(world, entity, mask, ReplicationContext{ .desc = desc, .tick = tick }, writer, &error))
			{
				m_failed.push_back({ id, identity->prefab, "spawn record: " + error });
				continue;
			}

			Record record;
			record.id = id;
			record.prefab = identity->prefab;
			record.owner = identity->owner;
			record.mask = mask;
			record.offset = static_cast<std::uint32_t>(m_state.size());
			record.size = static_cast<std::uint8_t>(writer.Size());   // at most kMaxRecordBytes: the buffer's size
			m_state.insert(m_state.end(), writer.Written().begin(), writer.Written().end());
			out.push_back(record);
		}
	}

	bool NetReplicationSendSystem::Send(Net::PeerId peer, std::span<const std::byte> message)
	{
		return m_session.SendLifecycle(peer, message);   // false: not sent, and the peer is closed or gone
	}

	bool NetReplicationSendSystem::SendWorld(Net::PeerId peer, std::span<const Record> records)
	{
		ByteWriter begin(m_message);
		if (!Encode(begin, WorldBegin{ .lifecycleSeq = m_session.NextLifecycleSeq(peer) }) || !Send(peer, begin.Written())) return false;
		if (!SendSpawnBatches(peer, records)) return false;

		const std::uint32_t endSeq = m_session.NextLifecycleSeq(peer);
		ByteWriter end(m_message);
		if (!Encode(end, WorldEnd{ .lifecycleSeq = endSeq }) || !Send(peer, end.Written())) return false;
		m_session.MarkWorldSent(peer, endSeq);
		CRU_INFO("[Net] sent peer {} the world: {} entities", peer, records.size());
		return true;
	}

	bool NetReplicationSendSystem::SendSpawnBatches(Net::PeerId peer, std::span<const Record> records)
	{
		std::size_t next = 0;
		while (next < records.size())
		{
			// As many records as fit one reliable message (16 KB, 512 entities).
			m_spawnBatch.spawns.clear();
			std::size_t bytes = kSpawnHeaderBytes;
			while (next < records.size() && m_spawnBatch.spawns.size() < kMaxEntitiesPerMessage)
			{
				const Record& r = records[next];
				const std::size_t recordBytes = VarU32Bytes(r.id) + 2 + 1 + 1 + 1 + r.size;
				if (!m_spawnBatch.spawns.empty() && bytes + recordBytes > kMaxReliableBytes) break;
				SpawnView view;
				view.id = r.id;
				view.prefab = r.prefab;
				view.owner = r.owner;
				view.mask = r.mask;
				view.state = std::span<const std::byte>(m_state).subspan(r.offset, r.size);
				m_spawnBatch.spawns.push_back(view);
				bytes += recordBytes;
				++next;
			}

			m_spawnBatch.lifecycleSeq = m_session.NextLifecycleSeq(peer);
			m_spawnBatch.serverTick = m_session.Clock().Tick();
			ByteWriter writer(m_message);
			if (!Encode(writer, m_spawnBatch))
			{
				// Every record was sized to fit, so this is a bug; the client must not miss it.
				m_session.DropPeer(peer, CloseReason::Generic, std::format("could not encode a SpawnBatch of {} records", m_spawnBatch.spawns.size()));
				return false;
			}
			if (!Send(peer, writer.Written())) return false;
			m_session.MutableStats().spawnRecordsSent += static_cast<std::uint32_t>(m_spawnBatch.spawns.size());
		}
		return true;
	}

	bool NetReplicationSendSystem::SendDespawnBatches(Net::PeerId peer, std::span<const Net::NetworkId> ids)
	{
		for (std::size_t first = 0; first < ids.size(); first += kMaxEntitiesPerMessage)
		{
			const std::size_t count = std::min<std::size_t>(kMaxEntitiesPerMessage, ids.size() - first);   // at most 2.6 KB
			m_despawnBatch.ids.assign(ids.begin() + static_cast<std::ptrdiff_t>(first), ids.begin() + static_cast<std::ptrdiff_t>(first + count));
			m_despawnBatch.lifecycleSeq = m_session.NextLifecycleSeq(peer);
			ByteWriter writer(m_message);
			if (!Encode(writer, m_despawnBatch))
			{
				m_session.DropPeer(peer, CloseReason::Generic, std::format("could not encode a DespawnBatch of {} ids", count));
				return false;
			}
			if (!Send(peer, writer.Written())) return false;
			m_session.MutableStats().despawnIdsSent += static_cast<std::uint32_t>(count);
		}
		return true;
	}

	// Entities whose spawn record could not be written were left out of every message this frame.
	// Destroying them keeps every peer's entity set equal: clients that already had one get its
	// despawn next frame (the identity hook queues it), the rest ignore that despawn.
	void NetReplicationSendSystem::DestroyFailed(World& world)
	{
		for (Failed& failed : m_failed)
		{
			const Entity entity = m_session.Entities().Find(failed.id);
			if (!world.IsAlive(entity)) continue;   // listed twice (world and spawn records), or gone
			world.DestroyEntity(entity);
			m_session.NotifySpawnFailed(failed.id, failed.prefab, std::move(failed.reason));
		}
		m_failed.clear();
	}
}
