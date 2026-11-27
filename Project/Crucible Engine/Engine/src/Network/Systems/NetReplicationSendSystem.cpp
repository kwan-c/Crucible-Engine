/*************************************************************************
* \file:         NetReplicationSendSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.10
* \brief
*                NetReplicationSendSystem: the host's lifecycle stream
*                (world on join, spawns, despawns) and snapshots.
*
* \history
*    -v1.0       lifecycle only
*    -v1.1       step-stamped tick, per-client views, confirmed-spawn
*                gating, full-state snapshots, acks
*    -v1.2       gameplay events, after the spawns and despawns
*    -v1.3       the per-client budget: classes, maximum ages, priority,
*                change detection, adaptive allowance, counters
*    -v1.4       SceneObjectBatches in the world stream; the scene identity
*                in WorldBegin (Networking Phase 14.5)
*    -v1.5       a gameplay event's target goes on the wire (protocol 6)
*    -v1.6       views start over after a scene change; Loading peers get nothing (Networking C# Phase 9)
*    -v1.7       protocol 9: varint record mask and length in the size estimates; WorldBegin carries the
*                scene seed
*    -v1.8       an attached object's distance class uses its world position
*    -v1.9       OwnerChange messages after the frame's spawns and despawns
*    -v1.10      records the lag-compensation history every frame on the host
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
#include <chrono>
#include <format>

namespace Crucible
{
	namespace
	{
		RecordMask FullMask(const NetPrefabDesc& desc) noexcept
		{
			return static_cast<RecordMask>((1u << desc.replicated.size()) - 1u);   // at most 16 entries: no overflow
		}

		std::size_t VarU32Bytes(std::uint32_t v) noexcept
		{
			std::size_t n = 1;
			while (v >= 0x80u) { v >>= 7; ++n; }
			return n;
		}

		// A record's mask and length on the wire (protocol 9: both varints).
		std::size_t RecordHeaderBytes(RecordMask mask, std::size_t size) noexcept
		{
			return VarU32Bytes(mask) + VarU32Bytes(static_cast<std::uint32_t>(size));
		}

		// u8 type, u32 lifecycleSeq, u32 serverTick, u16 count
		constexpr std::size_t kSpawnHeaderBytes = 1 + 4 + 4 + 2;
		// u8 type, u16 sequence, u32 serverTick, u16 count
		constexpr std::size_t kSnapshotHeaderBytes = 1 + 2 + 4 + 2;

		// FNV-1a, 64 bits, over the mask and the state: equal records, equal hash. Two different
		// records sharing a hash (about 1 in 2^64 per comparison) would leave one client with an
		// older copy until that entity's state changes again.
		std::uint64_t RecordHash(RecordMask mask, std::span<const std::byte> state) noexcept
		{
			std::uint64_t h = 0xcbf29ce484222325ull;
			const auto mix = [&h](std::uint8_t b) { h ^= b; h *= 0x100000001b3ull; };
			mix(static_cast<std::uint8_t>(mask));
			mix(static_cast<std::uint8_t>(mask >> 8));
			for (const std::byte b : state) mix(static_cast<std::uint8_t>(b));
			return h;
		}

		// Writes elapsed microseconds into `out` (and the largest into `max`) when it goes.
		struct ScopedMicros
		{
			std::int64_t& out;
			std::int64_t* max = nullptr;
			std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			~ScopedMicros()
			{
				out = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
				if (max != nullptr) *max = std::max(*max, out);
			}
		};
	}

	void NetReplicationSendSystem::ClientView::Reset()
	{
		connection = kInvalidConnection;
		states.clear();
		for (SentSnapshot& s : sent) { s.valid = false; s.entries.clear(); }
		lastSequence = 0;
		confirmedUpTo = 0;
		allowance = kDefaultAllowanceBytes;
		calmSnapshots = 0;
		fixedAllowance = false;
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

	std::size_t NetReplicationSendSystem::Allowance(Net::PeerId peer) const noexcept
	{
		return peer >= 1 && peer <= Net::kMaxClients ? m_views[peer - 1u].allowance : 0;
	}

	void NetReplicationSendSystem::ForceAllowance(Net::PeerId peer, std::size_t bytes) noexcept
	{
		if (peer < 1 || peer > Net::kMaxClients) return;
		ClientView& view = m_views[peer - 1u];
		view.fixedAllowance = bytes != 0;
		if (bytes != 0) view.allowance = std::clamp(bytes, kMinAllowanceBytes, kMaxAllowanceBytes);
	}

	std::size_t NetReplicationSendSystem::TrackedStates() const noexcept
	{
		std::size_t total = 0;
		for (const ClientView& view : m_views) total += view.states.size();
		return total;
	}

	void NetReplicationSendSystem::Update(World& world, SystemContext&)
	{
		NetStats& stats = m_session.MutableStats();
		const ScopedMicros timer{ stats.sendMicros, &stats.maxSendMicros };
		// A different session, or another scene: nothing any view says is true any more.
		if (m_session.Generation() != m_generation || m_session.SceneEpoch() != m_sceneEpoch)
		{
			for (ClientView& view : m_views) view.Reset();
			m_generation = m_session.Generation();
			m_sceneEpoch = m_session.SceneEpoch();
			m_session.LagHistory().Clear();   // shapes of another scene (or session) are no use
		}

		// The tick counts the fixed steps physics really ran (none while paused), so a snapshot's
		// tick names an exact simulation step.
		const bool hosting = m_session.IsServer() && m_session.State() == SessionState::Running;
		if (hosting) m_session.Clock().AdvanceSimulation(m_steps ? m_steps() : 0u);
		if (hosting) m_session.LagHistory().Record(world, m_session, m_session.Clock().Tick());   // lag compensation

		// Ids unmapped since last frame: their per-client state goes. In every role, so the
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
			m_session.TakeOwnerChanges(m_ownerChanges);
			m_ownerChanges.clear();
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
			// Loading another scene (Phase 9): nothing of this one until it asks for the new world.
			if (!peer.IsJoined() || peer.phase == PeerState::Phase::Loading) continue;
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

		// Ownership changes, after this frame's spawns and despawns: to every
		// client in the game. A client receiving its world this frame has the new owners in it.
		m_session.TakeOwnerChanges(m_ownerChanges);
		for (const OwnerChangeRequest& change : m_ownerChanges)
		{
			for (const Net::PeerId peer : m_incremental)
			{
				const std::uint32_t seq = m_session.NextLifecycleSeq(peer);
				if (seq == 0) continue;   // closed by a failed send earlier this frame
				std::array<std::byte, 16> buffer;
				ByteWriter writer(buffer);
				if (Encode(writer, OwnerChange{ .lifecycleSeq = seq, .id = change.id, .owner = change.owner })) Send(peer, writer.Written());
			}
		}
		m_ownerChanges.clear();

		// Gameplay events, after this frame's spawns and despawns on the same lane: in order, each
		// to everyone or to its one client. A client receiving its world this frame gets none of
		// them: its world is already newer than they are (the events may name entities it never had).
		m_session.TakeGameplayEvents(m_events);
		for (const QueuedGameplayEvent& event : m_events)
		{
			for (const PeerState& peer : m_session.Peers())
			{
				if (!peer.IsJoined() || (event.to != NetworkSession::kEveryone && event.to != peer.peer)) continue;
				if (peer.peer == event.except) continue;
				if (std::ranges::find(m_incremental, peer.peer) == m_incremental.end())
				{
					++m_session.MutableStats().gameplayEventsSkipped;
					continue;
				}
				const std::uint32_t seq = m_session.NextLifecycleSeq(peer.peer);
				if (seq == 0) continue;   // closed by a failed send earlier this frame
				m_eventBuffer.resize(kMaxReliableBytes);
				ByteWriter writer(m_eventBuffer);
				const GameplayEventView view{ .lifecycleSeq = seq, .kind = event.kind, .target = event.target, .payload = event.payload };
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
				const ScopedMicros gatherTimer{ stats.gatherMicros };
				m_worldIds.clear();
				for (const auto& [id, entity] : m_session.Entities()) m_worldIds.push_back(id);
				std::ranges::sort(m_worldIds);
				BuildRecords(world, m_worldIds, m_snapshotRecords, OnWriteFailure::Skip);
				gathered = true;
			}
			SendSnapshotsTo(peer, view, m_session.Clock().Tick());
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
			const RecordMask mask = desc != nullptr ? FullMask(*desc) : 0;
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
			record.size = static_cast<std::uint16_t>(writer.Size());   // at most kMaxRecordBytes: the buffer's size
			record.hash = RecordHash(mask, writer.Written());
			record.sendPriority = desc->sendPriority;
			record.critical = desc->critical;
			if (const Transform* t = world.Read<Transform>(entity)) record.position = t->positions;
			// An attached object: its Transform is local to the mount, so its
			// distance to each client is taken from where it is in the world.
			if (Math::mat4 m(1.f); world.GetParent(entity).IsValid() && world.TryGetWorldMatrix(entity, m)) record.position = Math::vec3(m[3]);
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
		// Scene objects first (the client binds its own entities), then the spawned ones.
		m_worldScene.clear();
		m_worldDynamic.clear();
		for (const Record& r : records) (m_session.SceneIdOf(r.id) != 0 ? m_worldScene : m_worldDynamic).push_back(r);

		ByteWriter begin(m_message);
		const WorldBegin message{ .lifecycleSeq = m_session.NextLifecycleSeq(peer), .sceneAsset = m_session.SceneAsset(), .sceneHash = m_session.SceneHash(),
			.sceneSeed = m_session.SceneSeed() };
		if (!Encode(begin, message) || !Send(peer, begin.Written())) return false;
		if (!SendSceneObjectBatches(peer, m_worldScene)) return false;
		if (!SendSpawnBatches(peer, m_worldDynamic)) return false;

		const std::uint32_t endSeq = m_session.NextLifecycleSeq(peer);
		ByteWriter end(m_message);
		if (!Encode(end, WorldEnd{ .lifecycleSeq = endSeq }) || !Send(peer, end.Written())) return false;
		m_session.MarkWorldSent(peer, endSeq);
		CRU_INFO("[Net] sent peer {} the world: {} entities ({} scene objects)", peer, records.size(), m_worldScene.size());
		return true;
	}

	bool NetReplicationSendSystem::SendSpawnBatches(Net::PeerId peer, std::span<const Record> records)
	{
		std::size_t next = 0;
		while (next < records.size())
		{
			// As many records as fit one reliable message (16 KB, 512 entities).
			const std::size_t first = next;
			m_spawnBatch.spawns.clear();
			std::size_t bytes = kSpawnHeaderBytes;
			while (next < records.size() && m_spawnBatch.spawns.size() < kMaxEntitiesPerMessage)
			{
				const Record& r = records[next];
				const std::size_t recordBytes = VarU32Bytes(r.id) + 2 + 1 + RecordHeaderBytes(r.mask, r.size) + r.size;
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

			// The client holds these once it reports this message applied (RefreshConfirmations):
			// then exactly the records this message carried are its baseline.
			ClientView& view = ViewOf(peer);
			for (std::size_t k = first; k < next; ++k)
				view.states[records[k].id] = ClientEntityState{ .spawnSeq = m_spawnBatch.lifecycleSeq, .spawnHash = records[k].hash };
		}
		return true;
	}

	// SendSpawnBatches for adopted scene objects: each record also names its persistent scene id,
	// and the client binds its own entity instead of instantiating one.
	bool NetReplicationSendSystem::SendSceneObjectBatches(Net::PeerId peer, std::span<const Record> records)
	{
		std::size_t next = 0;
		while (next < records.size())
		{
			const std::size_t first = next;
			m_sceneBatch.objects.clear();
			std::size_t bytes = kSpawnHeaderBytes;
			while (next < records.size() && m_sceneBatch.objects.size() < kMaxEntitiesPerMessage)
			{
				const Record& r = records[next];
				const std::size_t recordBytes = VarU32Bytes(r.id) + 8 + 2 + 1 + RecordHeaderBytes(r.mask, r.size) + r.size;
				if (!m_sceneBatch.objects.empty() && bytes + recordBytes > kMaxReliableBytes) break;
				SceneObjectView view;
				view.id = r.id;
				view.sceneId = m_session.SceneIdOf(r.id);
				view.profile = r.prefab;
				view.owner = r.owner;
				view.mask = r.mask;
				view.state = std::span<const std::byte>(m_state).subspan(r.offset, r.size);
				m_sceneBatch.objects.push_back(view);
				bytes += recordBytes;
				++next;
			}

			m_sceneBatch.lifecycleSeq = m_session.NextLifecycleSeq(peer);
			m_sceneBatch.serverTick = m_session.Clock().Tick();
			ByteWriter writer(m_message);
			if (!Encode(writer, m_sceneBatch))
			{
				m_session.DropPeer(peer, CloseReason::Generic, std::format("could not encode a SceneObjectBatch of {} records", m_sceneBatch.objects.size()));
				return false;
			}
			if (!Send(peer, writer.Written())) return false;
			m_session.MutableStats().spawnRecordsSent += static_cast<std::uint32_t>(m_sceneBatch.objects.size());

			ClientView& view = ViewOf(peer);
			for (std::size_t k = first; k < next; ++k)
				view.states[records[k].id] = ClientEntityState{ .spawnSeq = m_sceneBatch.lifecycleSeq, .spawnHash = records[k].hash };
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

	// Confirmation is application, not delivery: the client says which lifecycle
	// message it has applied (Input.appliedLifecycle, or ClientReady for the whole world), and every
	// spawn carried by that message or an earlier one is now in its World.
	void NetReplicationSendSystem::RefreshConfirmations(const PeerState& peer, ClientView& view)
	{
		if (peer.appliedLifecycle == view.confirmedUpTo) return;
		view.confirmedUpTo = peer.appliedLifecycle;
		const Net::NetTick tick = m_session.Clock().Tick();
		for (auto& [id, state] : view.states)
		{
			if (state.confirmed || state.spawnSeq > peer.appliedLifecycle) continue;
			// APPLIED, not merely delivered: the client holds exactly what the spawn carried.
			state.confirmed = true;
			state.acked = true;
			state.ackedTick = 0;
			state.baseline = state.spawnHash;
			state.cleanTick = tick;
		}
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
			for (const SentSnapshot::Entry& entry : sent.entries)
			{
				const auto it = view.states.find(entry.id);   // gone if despawned since
				if (it == view.states.end() || !it->second.confirmed) continue;
				ClientEntityState& state = it->second;
				if (state.acked && sent.tick <= state.ackedTick) continue;
				// The client applied this snapshot: it holds what was SENT then, not today's state.
				state.acked = true;
				state.ackedTick = sent.tick;
				state.baseline = entry.hash;
			}
			sent.valid = false;
			sent.entries.clear();
			++m_session.MutableStats().snapshotsAcked;
		}
	}

	// The allowance follows how long GNS queues this client's snapshots (lane 1 only, so a
	// burst of reliable spawns on lane 0 cannot shrink it).
	void NetReplicationSendSystem::AdaptAllowance(const PeerState& peer, ClientView& view)
	{
		if (view.fixedAllowance) return;
		const GnsTransport* transport = m_session.Transport();
		std::array<LaneMetrics, kLaneCount> lanes{};
		if (transport == nullptr || !transport->LaneStatus(peer.connection, lanes)) return;
		NetStats& stats = m_session.MutableStats();
		const std::int64_t queued = lanes[static_cast<std::size_t>(Lane::Snapshot)].queueTimeUs;
		if (queued > kShrinkQueueMicros)
		{
			view.allowance = std::max(kMinAllowanceBytes, view.allowance * 3 / 4);
			view.calmSnapshots = 0;
			++stats.allowanceShrinks;
		}
		else if (queued < kGrowQueueMicros)
		{
			if (++view.calmSnapshots >= kCalmSnapshotsToGrow && view.allowance < kMaxAllowanceBytes)
			{
				view.allowance = std::min(kMaxAllowanceBytes, view.allowance + std::max<std::size_t>(1, view.allowance / 10));
				view.calmSnapshots = 0;
				++stats.allowanceGrows;
			}
		}
		else
		{
			view.calmSnapshots = 0;
		}
	}

	// One client's snapshot (see the header's notes): candidates, urgent first, then priority,
	// within the allowance and at most two messages.
	void NetReplicationSendSystem::SendSnapshotsTo(const PeerState& peer, ClientView& view, Net::NetTick tick)
	{
		NetStats& stats = m_session.MutableStats();
		AdaptAllowance(peer, view);

		// Where this client is: the first entity it owns (its player, in a typical game).
		bool hasViewer = false;
		Math::vec3 viewer(0.f);
		for (const Record& r : m_snapshotRecords)
			if (r.owner == peer.peer) { viewer = r.position; hasViewer = true; break; }

		m_candidates.clear();
		for (std::uint32_t index = 0; index < m_snapshotRecords.size(); ++index)
		{
			const Record& r = m_snapshotRecords[index];
			const auto it = view.states.find(r.id);
			if (it == view.states.end() || !it->second.confirmed) continue;   // GATING
			ClientEntityState& state = it->second;
			if (state.acked && state.baseline == r.hash)   // the client holds exactly this: 0 bytes
			{
				state.cleanTick = tick;
				state.dueSince = 0;
				state.priority = 0.f;
				++stats.recordsUnchanged;
				continue;
			}

			UpdateClass cls = UpdateClass::Critical;
			float distanceFactor = 1.f;
			if (!r.critical)
			{
				const Math::vec3 d = r.position - viewer;
				const float d2 = hasViewer ? glm::dot(d, d) : 0.f;   // nothing owned: everything counts as near
				if (d2 < kNearMetres * kNearMetres)     { cls = UpdateClass::Near; distanceFactor = 1.f; }
				else if (d2 < kMidMetres * kMidMetres)  { cls = UpdateClass::Mid;  distanceFactor = 0.35f; }
				else                                    { cls = UpdateClass::Far;  distanceFactor = 0.08f; }
			}
			state.priority += r.sendPriority * distanceFactor;

			Candidate c;
			c.record = index;
			c.state = &state;
			c.cls = cls;
			c.age = tick - std::max(state.lastSentTick, state.cleanTick);
			c.lateness = static_cast<std::int64_t>(c.age) - static_cast<std::int64_t>(MaxAgeTicks(cls));
			c.urgent = r.critical || c.lateness >= 0;
			if (c.urgent && state.dueSince == 0) state.dueSince = tick;
			m_candidates.push_back(c);
		}

		// Critical first (oldest first), then overdue (furthest past its limit first), then by
		// priority (then the longest waiting).
		const auto rank = [](const Candidate& c) { return c.cls == UpdateClass::Critical ? 0 : (c.urgent ? 1 : 2); };
		std::ranges::sort(m_candidates, [&](const Candidate& a, const Candidate& b) {
			const int ra = rank(a), rb = rank(b);
			if (ra != rb) return ra < rb;
			if (ra == 0 && a.age != b.age) return a.age > b.age;
			if (ra == 1 && a.lateness != b.lateness) return a.lateness > b.lateness;
			if (ra == 2)
			{
				if (a.state->priority != b.state->priority) return a.state->priority > b.state->priority;
				if (a.age != b.age) return a.age > b.age;
			}
			return m_snapshotRecords[a.record].id < m_snapshotRecords[b.record].id;
		});

		// Critical entities may fill one message's worth at most; the rest of them wait at the end
		// (they lead again next snapshot, being older), so the second message always serves the others.
		std::size_t criticalEnd = 0, criticalBytes = kSnapshotHeaderBytes, keep = 0;
		while (criticalEnd < m_candidates.size() && rank(m_candidates[criticalEnd]) == 0)
		{
			const Record& r = m_snapshotRecords[m_candidates[criticalEnd].record];
			criticalBytes += VarU32Bytes(r.id) + RecordHeaderBytes(r.mask, r.size) + r.size;
			if (criticalBytes <= kMaxSnapshotBytes) keep = criticalEnd + 1;
			++criticalEnd;
		}
		if (keep < criticalEnd)
			std::rotate(m_candidates.begin() + static_cast<std::ptrdiff_t>(keep), m_candidates.begin() + static_cast<std::ptrdiff_t>(criticalEnd), m_candidates.end());

		std::size_t allowance = view.allowance;
		std::size_t next = 0;
		for (std::size_t message = 0; message < kMaxSnapshotMessagesPerClient; ++message)
		{
			if (message > 0 && next >= m_candidates.size()) break;
			std::size_t capacity = std::min(allowance, kMaxSnapshotBytes);
			bool urgentOnly = false;
			if (capacity < kMinMessageBytes)
			{
				// Allowance spent: only urgent entities may have one more message.
				if (next >= m_candidates.size() || !m_candidates[next].urgent) break;
				capacity = kMaxSnapshotBytes;
				urgentOnly = true;
				++stats.budgetOverflow;
			}
			const std::size_t before = next;
			std::size_t bytes = 0;
			if (!WriteAndSend(peer.peer, view, tick, next, capacity, urgentOnly, bytes)) break;   // GNS refused: try next snapshot
			allowance -= std::min(allowance, bytes);
			if (next == before) break;   // an empty message (nothing changed)
		}

		for (std::size_t k = next; k < m_candidates.size(); ++k)
		{
			++stats.recordsDeferred;
			if (m_candidates[k].urgent) ++stats.urgentDeferred;
		}
	}

	bool NetReplicationSendSystem::WriteAndSend(Net::PeerId peer, ClientView& view, Net::NetTick tick, std::size_t& next,
		std::size_t capacity, bool urgentOnly, std::size_t& bytes)
	{
		const std::uint16_t sequence = view.lastSequence == 0xFFFF ? 1 : static_cast<std::uint16_t>(view.lastSequence + 1);   // never 0
		m_snapshot.sequence = sequence;
		m_snapshot.serverTick = tick;
		m_snapshot.count = 0;
		const std::size_t first = next;
		const std::size_t limit = std::min(capacity, kMaxSnapshotBytes);   // never more than the buffer
		std::size_t size = kSnapshotHeaderBytes;
		while (next < m_candidates.size() && m_snapshot.count < kMaxEntitiesPerMessage)
		{
			const Candidate& c = m_candidates[next];
			if (urgentOnly && !c.urgent) break;
			const Record& r = m_snapshotRecords[c.record];
			const std::size_t recordBytes = VarU32Bytes(r.id) + RecordHeaderBytes(r.mask, r.size) + r.size;
			if (size + recordBytes > limit) break;
			RecordView& record = m_snapshot.records[m_snapshot.count++];
			record.id = r.id;
			record.mask = r.mask;
			record.body = std::span<const std::byte>(m_state).subspan(r.offset, r.size);
			size += recordBytes;
			++next;
		}

		ByteWriter writer(std::span<std::byte>(m_message).first(kMaxSnapshotBytes));
		if (!Encode(writer, m_snapshot))
		{
			CRU_ERROR("[Net] could not encode a snapshot of {} records for peer {}", m_snapshot.count, peer);
			next = first;
			return false;
		}
		if (m_session.SendSnapshot(peer, writer.Written(), sequence) != SendResult::Ok)
		{
			next = first;   // not sent: not remembered, priorities and ages untouched
			return false;
		}

		NetStats& stats = m_session.MutableStats();
		view.lastSequence = sequence;
		SentSnapshot& sent = view.sent[sequence % view.sent.size()];
		sent.sequence = sequence;
		sent.tick = tick;
		sent.valid = true;
		sent.entries.clear();
		for (std::size_t k = first; k < next; ++k)
		{
			const Candidate& c = m_candidates[k];
			const Record& r = m_snapshotRecords[c.record];
			sent.entries.push_back({ r.id, r.hash });
			const std::size_t cls = static_cast<std::size_t>(c.cls);
			++stats.updateAges[cls][std::min<std::size_t>(c.age, NetStats::kAgeBuckets - 1)];
			stats.maxUpdateAge[cls] = std::max(stats.maxUpdateAge[cls], static_cast<std::uint32_t>(c.age));
			if (c.state->dueSince != 0) stats.maxUrgentWaitTicks = std::max(stats.maxUrgentWaitTicks, tick - c.state->dueSince);
			c.state->dueSince = 0;
			c.state->priority = 0.f;
			c.state->lastSentTick = tick;
		}
		bytes = writer.Size();
		stats.snapshotRecordsSent += m_snapshot.count;
		stats.lastSnapshotBytes = static_cast<std::uint32_t>(bytes);
		stats.maxSnapshotBytes = std::max(stats.maxSnapshotBytes, stats.lastSnapshotBytes);
		if (m_snapshot.count == 0) ++stats.emptySnapshots;
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
