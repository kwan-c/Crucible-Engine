/*************************************************************************
* \file:         NetReplicationSendSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.2
* \brief
*                NetReplicationSendSystem: the host's lifecycle stream
*                (world on join, spawns, despawns) and snapshots.
*
* \history
*    -v1.0       lifecycle only (Networking Guide 9.3, 9.4, PR 8)
*    -v1.1       step-stamped tick, per-client views, confirmed-spawn
*                gating, full-state snapshots, acks (PR 9)
*    -v1.2       gameplay events, after the spawns and despawns (PR 12)
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
		// u8 type, u16 sequence, u32 serverTick, u16 count
		constexpr std::size_t kSnapshotHeaderBytes = 1 + 2 + 4 + 2;
	}

	void NetReplicationSendSystem::ClientView::Reset()
	{
		connection = kInvalidConnection;
		states.clear();
		for (SentSnapshot& s : sent) { s.valid = false; s.ids.clear(); }
		lastSequence = 0;
		confirmedUpTo = 0;
		resumeFrom = 0;
	}

	NetReplicationSendSystem::NetReplicationSendSystem(NetworkSession& session, const NetContent& content, StepSource steps)
		: m_session(session), m_content(content), m_steps(std::move(steps)), m_message(kMaxReliableBytes)
	{
	}

	void NetReplicationSendSystem::Describe(SystemDesc& desc) const
	{
		desc.Exclusive();   // serializers read any component; failed spawns are destroyed here
	}

	const NetReplicationSendSystem::ClientEntityState* NetReplicationSendSystem::ClientState(Net::PeerId peer, Net::NetworkId id) const
	{
		if (peer < 1 || peer > Net::kMaxClients) return nullptr;
		const ClientView& view = m_views[peer - 1u];
		const auto it = view.states.find(id);
		return it == view.states.end() ? nullptr : &it->second;
	}

	void NetReplicationSendSystem::Update(World& world, SystemContext&)
	{
		// A different session: nothing any view says is true any more.
		if (m_session.Generation() != m_generation)
		{
			for (ClientView& view : m_views) view.Reset();
			m_generation = m_session.Generation();
		}

		// The tick counts the fixed steps physics really ran (none while paused), so a snapshot's
		// tick names an exact simulation step (Networking Guide 18.4).
		const bool hosting = m_session.IsServer() && m_session.State() == SessionState::Running;
		if (hosting) m_session.Clock().AdvanceSimulation(m_steps ? m_steps() : 0u);

		// Ids unmapped since last frame: their per-client state goes (issue 9). In every role, so the
		// session's list never grows.
		m_session.TakeDestroyedThisFrame(m_destroyed);
		for (const Net::NetworkId id : m_destroyed)
			for (ClientView& view : m_views) view.states.erase(id);
		m_destroyed.clear();
		if (!hosting)
		{
			// A session that stopped mid-frame may leave these behind; they belong to no one now.
			m_session.TakeSpawned(m_spawned);
			m_spawned.clear();
			m_session.TakeGameplayEvents(m_events);
			m_events.clear();
			return;
		}
		SyncViews();

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
			BuildRecords(world, m_worldIds, m_worldRecords, OnWriteFailure::Destroy);
			for (const Net::PeerId peer : m_joining) SendWorld(peer, m_worldRecords);
		}

		if (!m_incremental.empty() && (!m_spawned.empty() || !m_despawned.empty()))
		{
			BuildRecords(world, m_spawned, m_spawnRecords, OnWriteFailure::Destroy);
			for (const Net::PeerId peer : m_incremental)
				if (SendSpawnBatches(peer, m_spawnRecords)) SendDespawnBatches(peer, m_despawned);
		}

		// Gameplay events, after this frame's spawns and despawns on the same lane: in order, each
		// to everyone or to its one client. A client receiving its world this frame gets none of
		// them: its world is already newer than they are (the events may name entities it never had).
		m_session.TakeGameplayEvents(m_events);
		for (const QueuedGameplayEvent& event : m_events)
		{
			for (const PeerState& peer : m_session.Peers())
			{
				if (!peer.IsJoined() || (event.to != NetworkSession::kEveryone && event.to != peer.peer)) continue;
				if (std::ranges::find(m_incremental, peer.peer) == m_incremental.end())
				{
					++m_session.MutableStats().gameplayEventsSkipped;
					continue;
				}
				const std::uint32_t seq = m_session.NextLifecycleSeq(peer.peer);
				if (seq == 0) continue;   // closed by a failed send earlier this frame
				m_eventBuffer.resize(kMaxReliableBytes);
				ByteWriter writer(m_eventBuffer);
				const GameplayEventView view{ .lifecycleSeq = seq, .kind = event.kind, .payload = event.payload };
				if (!Encode(writer, view))
				{
					// SendGameplayEvent checked the kind and the size, so this cannot happen.
					CRU_ERROR("[Net] gameplay event kind {} could not be encoded", event.kind);
					continue;
				}
				if (Send(peer.peer, writer.Written())) ++m_session.MutableStats().gameplayEventsSent;
			}
		}
		m_events.clear();

		// Confirmations and acks for every client, every frame; snapshots when one is due.
		const bool snapshotDue = m_session.Clock().SnapshotDue();
		bool gathered = false;
		for (const PeerState& peer : m_session.Peers())
		{
			if (!peer.IsJoined()) continue;
			ClientView& view = ViewOf(peer.peer);
			RefreshConfirmations(peer, view);
			if (peer.phase != PeerState::Phase::InGame) continue;
			ProcessAcks(peer, view);
			if (!snapshotDue) continue;
			if (!gathered)
			{
				// Gathered once, shared by every client: every mapped entity, whole records, id order.
				m_worldIds.clear();
				for (const auto& [id, entity] : m_session.Entities()) m_worldIds.push_back(id);
				std::ranges::sort(m_worldIds);
				BuildRecords(world, m_worldIds, m_snapshotRecords, OnWriteFailure::Skip);
				gathered = true;
			}
			SendSnapshotsTo(peer.peer, view, m_session.Clock().Tick());
		}

		DestroyFailed(world);
		m_spawned.clear();
		m_despawned.clear();
	}

	// A slot whose client left (or was replaced by a new one) starts from nothing.
	void NetReplicationSendSystem::SyncViews()
	{
		const std::span<const PeerState> peers = m_session.Peers();
		for (std::size_t i = 0; i < m_views.size() && i < peers.size(); ++i)
		{
			const PeerState& peer = peers[i];
			ClientView& view = m_views[i];
			const NetConnection owner = peer.phase == PeerState::Phase::Free ? kInvalidConnection : peer.connection;
			if (view.connection == owner) continue;
			view.Reset();
			view.connection = owner;
		}
	}

	void NetReplicationSendSystem::BuildRecords(const World& world, std::span<const Net::NetworkId> ids, std::vector<Record>& out,
		OnWriteFailure onFailure)
	{
		out.clear();
		const Net::NetTick tick = m_session.Clock().Tick();
		std::array<std::byte, kMaxRecordBytes> body;
		std::uint32_t skipped = 0;
		for (const Net::NetworkId id : ids)
		{
			const Entity entity = m_session.Entities().Find(id);
			if (!world.IsAlive(entity)) continue;   // spawned and destroyed in the same frame
			const NetworkIdentity* identity = world.Read<NetworkIdentity>(entity);
			if (identity == nullptr) continue;

			const NetPrefabDesc* desc = m_content.Prefabs().Desc(identity->prefab);
			std::string error = desc == nullptr ? "not a registered network prefab" : "";
			const std::uint8_t mask = desc != nullptr ? FullMask(*desc) : 0;
			ByteWriter writer(body);
			if (desc != nullptr && !m_content.Serializers().WriteRecord(world, entity, mask, ReplicationContext{ .desc = desc, .tick = tick }, writer, &error))
				error = "record: " + error;
			if (!error.empty())
			{
				if (onFailure == OnWriteFailure::Destroy) m_failed.push_back({ id, identity->prefab, "spawn " + error });
				else ++skipped;
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
		if (skipped > 0)
		{
			m_session.MutableStats().recordWriteFailed += skipped;
			CRU_ERROR("[Net] {} snapshot record(s) could not be written and were left out", skipped);
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

			// The client holds these once it reports this message applied (RefreshConfirmations).
			ClientView& view = ViewOf(peer);
			for (const SpawnView& spawn : m_spawnBatch.spawns)
				view.states[spawn.id] = ClientEntityState{ .spawnSeq = m_spawnBatch.lifecycleSeq };
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

	// Confirmation is application, not delivery (issues 1, 12): the client says which lifecycle
	// message it has applied (Input.appliedLifecycle, or ClientReady for the whole world), and every
	// spawn carried by that message or an earlier one is now in its World.
	void NetReplicationSendSystem::RefreshConfirmations(const PeerState& peer, ClientView& view)
	{
		if (peer.appliedLifecycle == view.confirmedUpTo) return;
		view.confirmedUpTo = peer.appliedLifecycle;
		for (auto& [id, state] : view.states)
			if (!state.confirmed && state.spawnSeq <= peer.appliedLifecycle) state.confirmed = true;
	}

	// The newest acknowledged sequence and the 32 before it. Each sent snapshot is processed once:
	// what it carried is now held by the client, at that snapshot's tick (not today's state).
	void NetReplicationSendSystem::ProcessAcks(const PeerState& peer, ClientView& view)
	{
		if (!peer.hasAck) return;
		for (std::uint32_t back = 0; back <= 32; ++back)
		{
			if (back > 0 && (peer.ackBits & (1u << (back - 1))) == 0) continue;
			const std::uint16_t sequence = static_cast<std::uint16_t>(peer.newestAckedSnapshot - back);
			SentSnapshot& sent = view.sent[sequence % view.sent.size()];
			if (sequence == 0 || !sent.valid || sent.sequence != sequence) continue;   // never sent, overwritten, or done
			for (const Net::NetworkId id : sent.ids)
			{
				const auto it = view.states.find(id);   // gone if despawned since
				if (it == view.states.end() || !it->second.confirmed) continue;
				ClientEntityState& state = it->second;
				if (state.acked && sent.tick <= state.ackedTick) continue;
				state.acked = true;
				state.ackedTick = sent.tick;
			}
			sent.valid = false;
			sent.ids.clear();
			++m_session.MutableStats().snapshotsAcked;
		}
	}

	// Every confirmed entity, whole records, id order, starting where the last snapshot ran out of
	// room. A message is remembered only if GNS took it, so no ack can ever name one that was not sent.
	void NetReplicationSendSystem::SendSnapshotsTo(Net::PeerId peer, ClientView& view, Net::NetTick tick)
	{
		const std::size_t total = m_snapshotRecords.size();
		std::size_t start = 0;
		if (view.resumeFrom != 0)
		{
			const auto it = std::ranges::lower_bound(m_snapshotRecords, view.resumeFrom, {}, &Record::id);
			start = it == m_snapshotRecords.end() ? 0 : static_cast<std::size_t>(it - m_snapshotRecords.begin());
		}
		m_candidates.clear();
		for (std::size_t k = 0; k < total; ++k)
		{
			const std::size_t index = (start + k) % total;
			const auto it = view.states.find(m_snapshotRecords[index].id);
			if (it != view.states.end() && it->second.confirmed) m_candidates.push_back(index);   // GATING (issue 1)
		}
		view.resumeFrom = 0;

		std::size_t next = 0;
		for (std::size_t message = 0; message < kMaxSnapshotMessagesPerClient && next < m_candidates.size(); ++message)
		{
			const std::uint16_t sequence = view.lastSequence == 0xFFFF ? 1 : static_cast<std::uint16_t>(view.lastSequence + 1);   // never 0
			m_snapshot.sequence = sequence;
			m_snapshot.serverTick = tick;
			m_snapshot.count = 0;
			const std::size_t first = next;
			std::size_t bytes = kSnapshotHeaderBytes;
			while (next < m_candidates.size() && m_snapshot.count < kMaxEntitiesPerMessage)
			{
				const Record& r = m_snapshotRecords[m_candidates[next]];
				const std::size_t recordBytes = VarU32Bytes(r.id) + 1 + 1 + r.size;
				if (bytes + recordBytes > kMaxSnapshotBytes) break;
				RecordView& record = m_snapshot.records[m_snapshot.count++];
				record.id = r.id;
				record.mask = r.mask;
				record.body = std::span<const std::byte>(m_state).subspan(r.offset, r.size);
				bytes += recordBytes;
				++next;
			}
			if (m_snapshot.count == 0) break;   // cannot happen: one record (at most 262 bytes) always fits

			ByteWriter writer(std::span<std::byte>(m_message).first(kMaxSnapshotBytes));
			if (!Encode(writer, m_snapshot))
			{
				CRU_ERROR("[Net] could not encode a snapshot of {} records for peer {}", m_snapshot.count, peer);
				next = first;
				break;
			}
			if (m_session.SendSnapshot(peer, writer.Written(), sequence) != SendResult::Ok)
			{
				next = first;   // not sent: not remembered; these entities go first next time
				break;
			}

			view.lastSequence = sequence;
			SentSnapshot& sent = view.sent[sequence % view.sent.size()];
			sent.sequence = sequence;
			sent.tick = tick;
			sent.valid = true;
			sent.ids.clear();
			for (const RecordView& record : m_snapshot.Records())
			{
				sent.ids.push_back(record.id);
				view.states[record.id].lastSentTick = tick;
			}
			m_session.MutableStats().snapshotRecordsSent += m_snapshot.count;
		}

		if (next < m_candidates.size())
		{
			m_session.MutableStats().recordsDeferred += static_cast<std::uint32_t>(m_candidates.size() - next);
			view.resumeFrom = m_snapshotRecords[m_candidates[next]].id;
		}
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
