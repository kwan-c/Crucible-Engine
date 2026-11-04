/*************************************************************************
* \file:         NetReceiveSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.3
* \brief
*                NetReceiveSystem: drives NetworkSession::Update each frame,
*                performs host spawns and applies the host's lifecycle stream
*                and snapshots on clients.
*
* \history
*    -v1.0       connection handling (Networking Guide PR 4)
*    -v1.1       destroys rejected identities; Debug id-map check (PR 6)
*    -v1.2       host spawns; client WorldBegin/SpawnBatch/WorldEnd/
*                DespawnBatch, validated whole before applying; proxies
*                made in a fixed order and destroyed on failure (PR 8)
*    -v1.3       snapshots validated whole, applied with a per-entity stale
*                check, acknowledged only once applied; 30 Hz heartbeat
*                Input with acks and appliedLifecycle (PR 9)
*
**************************************************************************/
#include "Network/Systems/NetReceiveSystem.h"

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Network/NetComponents.h"
#include "Network/NetContent.h"
#include "Network/NetIdentityHooks.h"
#include "Scene/Prefab.h"
#include "Scene/SceneManager.h"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace Crucible
{
	namespace
	{
		/// Every bit a record of this prefab may carry: bit i = replicated entry i.
		std::uint8_t FullMask(const NetPrefabDesc& desc) noexcept
		{
			return static_cast<std::uint8_t>((1u << desc.replicated.size()) - 1u);
		}

		/// The proxy's first interpolation sample: where the spawn put it, at the spawn's tick.
		NetInterpolation SeededInterpolation(const World& world, Entity root, Net::NetTick tick)
		{
			NetInterpolation interpolation;
			if (const Transform* t = world.Read<Transform>(root))
			{
				interpolation.samples[0].tick = tick;
				interpolation.samples[0].position = t->positions;
				interpolation.samples[0].rotation = t->rotation;
				interpolation.count = 1;
			}
			interpolation.lastApplied = tick;
			return interpolation;
		}
	}

	void NetReceiveSystem::Describe(SystemDesc& desc) const
	{
		desc.Exclusive();   // creates and destroys entities directly (spawns, proxies, despawns)
	}

	void NetReceiveSystem::SetComponentsEnabled(World& world, Entity root, std::span<const ComponentId> ids, bool on,
		std::vector<Entity>& scratch)
	{
		if (ids.empty()) return;
		scratch.clear();
		world.CollectSubtree(root, scratch);   // parents before children
		for (const Entity e : scratch)
			for (const ComponentId id : ids)
				if (world.IsComponentEnabled(e, id) != on) world.TrySetComponentEnabled(e, id, on);   // false if e has none
	}

	void NetReceiveSystem::Update(World& world, SystemContext& ctx)
	{
		m_session.Update();   // Offline: returns at once

		// Entities whose NetworkIdentity the session rejected (no id, or an id already in use).
		// Skipped if the entity is gone, or is now mapped after all (its identity was replaced).
		m_session.TakeRejected(m_rejected);
		for (const Entity e : m_rejected)
		{
			if (!world.IsAlive(e)) continue;
			const NetworkIdentity* identity = world.Read<NetworkIdentity>(e);
			if (identity != nullptr && m_session.Entities().Find(identity->id) == e) continue;
			ctx.commands.DestroyEntity(e);
		}

		if (m_session.Generation() != m_generation)
		{
			m_acks.Reset();   // acks name another session's snapshots
			m_generation = m_session.Generation();
		}

		if (m_session.IsServer()) PerformPendingSpawns(world);
		else if (m_session.IsClient())
		{
			ApplyWorldMessages(world);
			SendHeartbeat(world.DeltaTime());
		}

#ifndef NDEBUG
		if (m_session.IsActive()) CheckIdentityMap(world, m_session);   // reports identities overwritten after spawn
#endif
	}

	// ---- host --------------------------------------------------------------------------------

	// The spawns gameplay requested last frame (Networking Guide 9.3). NetworkIdentity goes on
	// LAST: its hook maps the id, so the map, and the send system that announces the spawn, only
	// ever see a finished entity. On any failure the root - and with it the whole subtree - is
	// destroyed, and the spawn is reported instead of broadcast.
	void NetReceiveSystem::PerformPendingSpawns(World& world)
	{
		m_session.TakePendingSpawns(m_spawns);
		for (const PendingSpawn& spawn : m_spawns)
		{
			Entity root = kNullEntity;
			try
			{
				const NetPrefabDesc* desc = m_content.Prefabs().Desc(spawn.prefab);
				const Prefab* prefab = m_content.Prefabs().PrefabOf(spawn.prefab);
				if (desc == nullptr || prefab == nullptr) throw std::runtime_error("the prefab is not registered or not loaded");

				root = prefab->Instantiate(world, kNullEntity,
					[this](Entity entity) { return m_scenes.EnsureId(entity).m_Value; });   // all or nothing (Prefab.cpp)
				if (!world.IsAlive(root)) throw std::runtime_error("the prefab did not instantiate");
				if (Transform* t = world.Write<Transform>(root))
				{
					t->positions = spawn.position;
					t->rotation = spawn.rotation;
				}
				// The host simulates everything, but a player's camera rig belongs to whoever plays it.
				if (spawn.owner != m_session.LocalPeer()) SetComponentsEnabled(world, root, desc->ownerLocalOnly, false, m_scratch);

				world.AddComponent(root, NetworkIdentity{ .id = spawn.id, .prefab = spawn.prefab, .owner = spawn.owner });
				if (!(m_session.Entities().Find(spawn.id) == root)) throw std::runtime_error("its NetworkIdentity was rejected");
				m_session.NotifySpawned(spawn.id);
			}
			catch (const std::exception& e)
			{
				if (world.IsAlive(root)) world.DestroyEntity(root);   // removes the whole subtree
				m_session.NotifySpawnFailed(spawn.id, spawn.prefab, e.what());
			}
		}
		m_spawns.clear();
	}

	// ---- client ------------------------------------------------------------------------------

	void NetReceiveSystem::ApplyWorldMessages(World& world)
	{
		m_session.TakeWorldMessages(m_world);
		// Every buffer goes back to GNS before this frame ends, whatever happens below.
		struct ReleaseAll { std::vector<ReceivedMessage>& messages; ~ReleaseAll() { messages.clear(); } } release{ m_world };

		for (const ReceivedMessage& message : m_world)
		{
			if (m_session.IsStopRequested()) break;   // leaving: apply nothing more
			if (!ApplyWorldMessage(world, message.Data())) break;
		}
	}

	void NetReceiveSystem::Leave(CloseReason reason, const std::string& why)
	{
		if (reason == CloseReason::Malformed) ++m_session.MutableStats().malformed;
		CRU_ERROR("[Net] {}", why);
		m_session.CloseAndStop(reason, why);
	}

	bool NetReceiveSystem::CheckSequence(std::uint32_t seq, const char* what)
	{
		const std::uint32_t expected = m_session.AppliedLifecycle() + 1;
		if (seq == expected) return true;
		Leave(CloseReason::Malformed, std::format("{} has lifecycle sequence {}, expected {}", what, seq, expected));   // lane 0 is ordered: a gap is a bug
		return false;
	}

	// One lifecycle message, applied whole or not at all. Every check comes before the World is
	// touched; a message that fails one ends the session (there is no way to skip it safely).
	bool NetReceiveSystem::ApplyWorldMessage(World& world, std::span<const std::byte> data)
	{
		ByteReader reader(data);
		MessageType type{};
		if (!ReadMessageType(reader, type)) { Leave(CloseReason::Malformed, "an unreadable message from the host"); return false; }
		const ClientWorld state = m_session.WorldState();

		switch (type)
		{
		case MessageType::WorldBegin:
		{
			WorldBegin begin;
			if (!Decode(reader, begin)) { Leave(CloseReason::Malformed, "malformed WorldBegin"); return false; }
			if (!CheckSequence(begin.lifecycleSeq, "WorldBegin")) return false;
			if (state != ClientWorld::AwaitingWorld) { Leave(CloseReason::Malformed, "a second WorldBegin"); return false; }
			m_session.SetWorldState(ClientWorld::Receiving);
			m_session.MarkLifecycleApplied(begin.lifecycleSeq);
			return true;
		}
		case MessageType::WorldEnd:
		{
			WorldEnd end;
			if (!Decode(reader, end)) { Leave(CloseReason::Malformed, "malformed WorldEnd"); return false; }
			if (!CheckSequence(end.lifecycleSeq, "WorldEnd")) return false;
			if (state != ClientWorld::Receiving) { Leave(CloseReason::Malformed, "WorldEnd without WorldBegin"); return false; }
			m_session.SetWorldState(ClientWorld::Ready);
			m_session.MarkLifecycleApplied(end.lifecycleSeq);
			return m_session.CompleteWorld(end.lifecycleSeq, m_session.Entities().Count());   // ClientReady
		}
		case MessageType::SpawnBatch:
			if (state == ClientWorld::AwaitingWorld) { Leave(CloseReason::Malformed, "SpawnBatch before WorldBegin"); return false; }
			return ApplySpawnBatch(world, reader);
		case MessageType::DespawnBatch:
			if (state != ClientWorld::Ready) { Leave(CloseReason::Malformed, "DespawnBatch inside the world stream"); return false; }
			return ApplyDespawnBatch(world, reader);
		case MessageType::GameplayEvent:
		{
			GameplayEventView event;
			if (!Decode(reader, event)) { Leave(CloseReason::Malformed, "malformed GameplayEvent"); return false; }
			if (!CheckSequence(event.lifecycleSeq, "GameplayEvent")) return false;
			if (state != ClientWorld::Ready) { Leave(CloseReason::Malformed, "GameplayEvent inside the world stream"); return false; }
			++m_session.MutableStats().unhandled;   // gameplay events: a later phase
			m_session.MarkLifecycleApplied(event.lifecycleSeq);
			return true;
		}
		case MessageType::Snapshot:
			// Unreliable (lane 1): a bad one is dropped, never fatal. The host sends them only once
			// ClientReady was accepted, so one before the world is complete is merely early.
			if (state != ClientWorld::Ready) { ++m_session.MutableStats().unexpected; return true; }
			ApplySnapshot(world, reader);
			return true;
		default:
			Leave(CloseReason::Malformed, "a client-to-host message type from the host");
			return false;
		}
	}

	bool NetReceiveSystem::ApplySpawnBatch(World& world, ByteReader& reader)
	{
		// Phase 1: the whole message, without touching the World (issue 3).
		if (!Decode(reader, m_spawnBatch)) { Leave(CloseReason::Malformed, "malformed SpawnBatch"); return false; }
		if (!CheckSequence(m_spawnBatch.lifecycleSeq, "SpawnBatch")) return false;
		for (std::size_t i = 0; i < m_spawnBatch.spawns.size(); ++i)
		{
			const SpawnView& spawn = m_spawnBatch.spawns[i];
			const NetPrefabDesc* desc = m_content.Prefabs().Desc(spawn.prefab);
			const char* fault = nullptr;
			if (desc == nullptr) fault = "unknown prefab";
			else if (spawn.owner > Net::kMaxClients) fault = "invalid owner";
			else if ((spawn.mask & ~FullMask(*desc)) != 0) fault = "mask bits the prefab does not replicate";
			else if (!m_content.Serializers().ValidateRecord(spawn.mask, spawn.state, ReplicationContext{ .desc = desc, .tick = m_spawnBatch.serverTick }))
				fault = "invalid state";
			if (fault != nullptr)
			{
				Leave(CloseReason::Malformed, std::format("SpawnBatch {} record {} of {} (id {}, prefab {}): {}",
					m_spawnBatch.lifecycleSeq, i + 1, m_spawnBatch.spawns.size(), spawn.id, spawn.prefab, fault));
				return false;
			}
		}

		// Phase 2: one proxy per record. If one fails the worlds have diverged: undo this
		// message's proxies and leave (issue 4).
		m_created.clear();
		for (const SpawnView& spawn : m_spawnBatch.spawns)
		{
			std::string why;
			if (CreateProxy(world, spawn, m_spawnBatch.serverTick, why)) continue;
			for (auto it = m_created.rbegin(); it != m_created.rend(); ++it)
				if (world.IsAlive(*it)) world.DestroyEntity(*it);
			m_created.clear();
			Leave(CloseReason::SpawnFailed, std::format("proxy {} (prefab {}) failed: {}", spawn.id, spawn.prefab, why));
			return false;
		}
		m_created.clear();
		m_session.MarkLifecycleApplied(m_spawnBatch.lifecycleSeq);
		return true;
	}

	// A proxy is made in a fixed order, and NetworkIdentity goes on LAST, so "in the id map" and
	// "complete" are the same thing. Any failure destroys the root, and with it the subtree.
	bool NetReceiveSystem::CreateProxy(World& world, const SpawnView& spawn, Net::NetTick tick, std::string& why)
	{
		if (world.IsAlive(m_session.Entities().Find(spawn.id)))
		{
			++m_session.MutableStats().duplicateSpawns;   // reliable and ordered: should not happen
			return true;
		}

		Entity root = kNullEntity;
		try
		{
			const NetPrefabDesc* desc = m_content.Prefabs().Desc(spawn.prefab);   // validated
			const Prefab* prefab = m_content.Prefabs().PrefabOf(spawn.prefab);
			if (desc == nullptr || prefab == nullptr) throw std::runtime_error("the prefab is not loaded on this client");

			root = prefab->Instantiate(world, kNullEntity,
					[this](Entity entity) { return m_scenes.EnsureId(entity).m_Value; });   // all or nothing (Prefab.cpp)
			if (!world.IsAlive(root)) throw std::runtime_error("the prefab did not instantiate");

			// Before physics or scripts ever see it (they run later this frame): no Jolt body, no AI.
			SetComponentsEnabled(world, root, desc->serverOnly, false, m_scratch);
			if (spawn.owner != m_session.LocalPeer()) SetComponentsEnabled(world, root, desc->ownerLocalOnly, false, m_scratch);

			std::string error;
			const ReplicationContext context{ .desc = desc, .tick = tick };
			if (!m_content.Serializers().ApplyRecord(world, root, spawn.mask, spawn.state, context, &error))
				throw std::runtime_error("initial state: " + error);

			world.AddComponent(root, SeededInterpolation(world, root, tick));
			world.AddComponent(root, NetworkIdentity{ .id = spawn.id, .prefab = spawn.prefab, .owner = spawn.owner });
			if (!(m_session.Entities().Find(spawn.id) == root)) throw std::runtime_error("its NetworkIdentity was rejected");

			m_created.push_back(root);
			++m_session.MutableStats().proxiesCreated;
			return true;
		}
		catch (const std::exception& e)
		{
			if (world.IsAlive(root)) world.DestroyEntity(root);   // removes the whole subtree
			why = e.what();
			return false;
		}
	}

	bool NetReceiveSystem::ApplyDespawnBatch(World& world, ByteReader& reader)
	{
		// The whole batch first: count, non-zero unique ids, no trailing bytes. Nothing destroyed yet.
		if (!Decode(reader, m_despawnBatch)) { Leave(CloseReason::Malformed, "malformed DespawnBatch"); return false; }
		if (!CheckSequence(m_despawnBatch.lifecycleSeq, "DespawnBatch")) return false;

		// Unknown ids are fine: never spawned here, or already gone.
		for (const Net::NetworkId id : m_despawnBatch.ids)
		{
			const Entity e = m_session.Entities().Find(id);
			if (world.IsAlive(e))
			{
				world.DestroyEntity(e);   // the hook removes the mapping; descendants go too
				++m_session.MutableStats().proxiesDestroyed;
			}
			else
			{
				++m_session.MutableStats().unknownDespawns;
			}
		}
		m_session.MarkLifecycleApplied(m_despawnBatch.lifecycleSeq);
		return true;
	}

	// ---- snapshots ---------------------------------------------------------------------------

	// One snapshot (Networking Guide 6.2, client steps 1-4; 9.5). All or nothing: the whole message
	// is decoded and every record for a live entity validated before anything is written. Then each
	// record is applied unless the entity already holds something as new (the per-entity tick,
	// NetInterpolation::lastApplied, so a late or reordered snapshot never moves anything back).
	// The snapshot is acknowledged only if every record was applied or deliberately skipped (stale,
	// or an entity despawned since): delivery alone acknowledges nothing (issues 1, 12).
	void NetReceiveSystem::ApplySnapshot(World& world, ByteReader& reader)
	{
		NetStats& stats = m_session.MutableStats();
		if (!Decode(reader, m_snapshot)) { ++stats.malformed; ++stats.snapshotsRejected; return; }

		m_resolved.clear();
		for (const RecordView& record : m_snapshot.Records())
		{
			const Entity e = m_session.Entities().Find(record.id);
			const NetworkIdentity* identity = world.IsAlive(e) ? world.Read<NetworkIdentity>(e) : nullptr;
			if (identity == nullptr) { m_resolved.push_back(kNullEntity); continue; }   // despawned since: skipped
			const NetPrefabDesc* desc = m_content.Prefabs().Desc(identity->prefab);
			// Whole-entity records only: a newer record must never carry less than an older one, or
			// the per-entity tick would block state it did not carry (9.5).
			const bool valid = desc != nullptr && record.mask == FullMask(*desc)
				&& m_content.Serializers().ValidateRecord(record.mask, record.body, ReplicationContext{ .desc = desc, .tick = m_snapshot.serverTick });
			if (!valid)
			{
				++stats.malformed;
				++stats.snapshotsRejected;   // nothing applied, not acknowledged
				return;
			}
			m_resolved.push_back(e);
		}

		bool complete = true;
		for (std::size_t i = 0; i < m_resolved.size(); ++i)
		{
			const Entity e = m_resolved[i];
			if (e == kNullEntity) { ++stats.unknownEntity; continue; }
			NetInterpolation* interpolation = world.Write<NetInterpolation>(e);
			if (interpolation != nullptr && m_snapshot.serverTick <= interpolation->lastApplied) { ++stats.staleRecords; continue; }

			const RecordView& record = m_snapshot.Records()[i];
			const NetworkIdentity* identity = world.Read<NetworkIdentity>(e);
			const ReplicationContext context{ .desc = m_content.Prefabs().Desc(identity->prefab), .tick = m_snapshot.serverTick };
			std::string error;
			if (!m_content.Serializers().ApplyRecord(world, e, record.mask, record.body, context, &error))
			{
				// Validated, so only a commit can refuse; the entity is unchanged (a transaction).
				CRU_WARN("[Net] snapshot {} record for id {} not applied: {}", m_snapshot.sequence, record.id, error);
				complete = false;
				continue;
			}
			if (interpolation != nullptr) interpolation->lastApplied = m_snapshot.serverTick;
		}
		if (!complete) { ++stats.snapshotsRejected; return; }

		m_acks.Received(m_snapshot.sequence);   // reported in the next Input
		++stats.snapshotsApplied;
		stats.lastSnapshotTick = std::max(stats.lastSnapshotTick, m_snapshot.serverTick);
	}

	// The client's Input message at 30 Hz from frame time, in every play state: the heartbeat, the
	// snapshot acks and the lifecycle confirmation (commands join it in a later phase). It runs
	// after this frame's messages were applied, so appliedLifecycle describes the World as it is.
	void NetReceiveSystem::SendHeartbeat(float dt)
	{
		if (!m_session.IsJoined() || m_session.IsStopRequested()) return;
		if (!m_session.Clock().InputDue(dt)) return;
		InputMessage input;   // fields by name (guide issue 2); count = 0: no commands yet
		input.ackedSnapshot = m_acks.Latest();
		input.ackBits = m_acks.Bits();
		input.appliedLifecycle = m_session.AppliedLifecycle();
		m_session.SendInput(input);   // a lost one is replaced by the next
	}
}
