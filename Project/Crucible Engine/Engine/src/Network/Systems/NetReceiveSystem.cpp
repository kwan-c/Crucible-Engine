/*************************************************************************
* \file:         NetReceiveSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.9
* \brief
*                NetReceiveSystem: drives NetworkSession::Update each frame,
*                performs host spawns and applies the host's lifecycle stream
*                and snapshots on clients.
*
* \history
*    -v1.0       connection handling
*    -v1.1       destroys rejected identities; Debug id-map check
*    -v1.2       host spawns; client WorldBegin/SpawnBatch/WorldEnd/
*                DespawnBatch, validated whole before applying; proxies
*                made in a fixed order and destroyed on failure
*    -v1.3       snapshots validated whole, applied with a per-entity stale
*                check, acknowledged only once applied; 30 Hz heartbeat
*                Input with acks and appliedLifecycle
*    -v1.4       the heartbeat moved to NetInputSystem (it sends commands
*                too); applied snapshots go to the session's SnapshotAcks()
*    -v1.5       a snapshot's root pose goes into the proxy's interpolation
*                buffer instead of its Transform, and feeds the server time
*                estimate; GameplayEvents are delivered
*    -v1.6       SceneObjectBatch decoded and checked; binding comes in
*                Networking Phase 14.5
*    -v1.7       scene objects: adoption, lockdown, scene identity check,
*                binding, removal of the host's destroyed ones (Phase 14.5)
*    -v1.8       placed players removed as spawn points (Networking Phase 14.6)
*    -v1.9       registers scene profiles (Networking Phase 14.7)
*
**************************************************************************/
#include "Network/Systems/NetReceiveSystem.h"

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Network/NetComponents.h"
#include "Network/NetContent.h"
#include "Network/NetIdentityHooks.h"
#include "Network/NetSceneObjects.h"
#include "Network/Systems/NetInterpolationSystem.h"
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

		// Scene objects: once per session, in its first frame, before anything has run on them.
		if (m_session.Generation() != m_sceneGeneration)
		{
			m_sceneGeneration = m_session.Generation();
			m_sceneStarted = false;
			m_localSceneObjects.clear();
		}
		if (!m_sceneStarted && m_session.State() == SessionState::Running && m_session.IsActive())
		{
			m_sceneStarted = true;
			StartSceneObjects(world);
		}

		if (m_session.IsServer()) PerformPendingSpawns(world);
		else if (m_session.IsClient()) ApplyWorldMessages(world);   // NetInputSystem sends the Input later this stage

#ifndef NDEBUG
		if (m_session.IsActive()) CheckIdentityMap(world, m_session);   // reports identities overwritten after spawn
#endif
	}

	// ---- host --------------------------------------------------------------------------------

	// The spawns gameplay requested last frame. NetworkIdentity goes on
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
			if (begin.sceneAsset != m_session.SceneAsset() || begin.sceneHash != m_session.SceneHash())
			{
				// Different scene objects would be bound to different entities: refuse before anything is.
				++m_session.MutableStats().sceneMismatches;
				Leave(CloseReason::SceneMismatch, std::format("the host plays scene {:016X} (scene objects {:016X}); this machine loaded {:016X} ({:016X})",
					begin.sceneAsset, begin.sceneHash, m_session.SceneAsset(), m_session.SceneHash()));
				return false;
			}
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
			RemoveUnboundSceneObjects(world);   // the host's world no longer has them
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
		case MessageType::SceneObjectBatch:
			if (state != ClientWorld::Receiving) { Leave(CloseReason::Malformed, "a SceneObjectBatch outside the world stream"); return false; }
			return ApplySceneObjectBatch(world, reader);
		case MessageType::GameplayEvent:
		{
			GameplayEventView event;
			if (!Decode(reader, event)) { Leave(CloseReason::Malformed, "malformed GameplayEvent"); return false; }
			if (!CheckSequence(event.lifecycleSeq, "GameplayEvent")) return false;
			if (state != ClientWorld::Ready) { Leave(CloseReason::Malformed, "GameplayEvent inside the world stream"); return false; }
			m_session.DeliverGameplayEvent(event.kind, event.payload);   // the payload is the game's to check
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
		// Phase 1: the whole message, without touching the World.
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
		// message's proxies and leave.
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

	// ---- scene objects (Networking Phase 14.5) ------------------------------------------------

	// The first frame of a session: the scene as it was loaded. Both sides record its identity; the
	// host adopts its scene objects; a client makes its own inert until they are bound - server-only
	// and owner-local components off (the host owns scene objects), before scripts, AI or physics
	// ever see them (they run later this frame, in Update and PostUpdate).
	void NetReceiveSystem::StartSceneObjects(World& world)
	{
		NetStats& stats = m_session.MutableStats();
		m_content.ForgetSceneProfiles();   // the last session's: ids start above the prefabs again
		SceneObjectScan scan = ScanSceneObjects(world, m_scenes.Current(), m_content);

		// Scene profiles (Phase 14.7). Both machines run the same scan, so a profile one refuses the
		// other refuses too: its objects stay local on both, and the hash (taken over the scan) agrees.
		std::vector<Net::NetPrefabId> refused;
		for (SceneProfile& profile : scan.profiles)
		{
			std::string error;
			if (m_content.AddSceneProfile(profile.id, std::move(profile.desc), error)) { ++stats.sceneProfiles; continue; }
			CRU_ERROR("[Net] scene profile {}: {}", profile.id, error);
			refused.push_back(profile.id);
		}
		if (!refused.empty())
			std::erase_if(scan.objects, [&](const SceneObject& o) { return std::ranges::find(refused, o.profile) != refused.end(); });
		for (const std::string& line : scan.skipped)
		{
			++stats.sceneObjectsSkipped;
			CRU_WARN("[Net] NetworkObjectAuthoring kept local: {}", line);
		}
		const std::uint64_t asset = m_scenes.HasPath() && m_sceneAssetOf ? m_sceneAssetOf(m_scenes.CurrentPath()) : 0;
		m_session.SetSceneIdentity(asset, scan.hash);

		for (const SceneObject& object : scan.objects)
		{
			const NetPrefabDesc* desc = m_content.Prefabs().Desc(object.profile);
			if (m_session.IsServer())
			{
				const Net::NetworkId id = m_session.AdoptSceneObject(object.sceneId);
				world.AddComponent(object.entity, NetworkIdentity{ .id = id, .prefab = object.profile, .owner = Net::kServerPeer });
				if (!(m_session.Entities().Find(id) == object.entity)) CRU_ERROR("[Net] scene object {:016X} could not be adopted", object.sceneId);
			}
			else
			{
				SetComponentsEnabled(world, object.entity, desc->serverOnly, false, m_scratch);
				SetComponentsEnabled(world, object.entity, desc->ownerLocalOnly, false, m_scratch);
				m_localSceneObjects[object.sceneId] = LocalSceneObject{ .entity = object.entity, .profile = object.profile };
			}
		}
		// Placed players: each peer gets a spawned one, so these only say where (world space).
		std::vector<PlayerSpawnPoint> points;
		for (const SceneObject& player : scan.players)
		{
			Math::mat4 m(1.f);
			if (!world.TryGetWorldMatrix(player.entity, m)) continue;
			const Math::mat3 basis(glm::normalize(Math::vec3(m[0])), glm::normalize(Math::vec3(m[1])), glm::normalize(Math::vec3(m[2])));
			points.push_back(PlayerSpawnPoint{ .position = Math::vec3(m[3]), .rotation = glm::normalize(glm::quat_cast(basis)) });
		}
		for (const SceneObject& player : scan.players)
		{
			if (!world.IsAlive(player.entity)) continue;
			world.DestroyEntity(player.entity);   // with its children
			++stats.scenePlayersPlaced;
		}
		m_session.SetPlayerSpawnPoints(std::move(points));

		if (!scan.objects.empty() || !scan.skipped.empty() || !scan.players.empty())
			CRU_INFO("[Net] scene objects: {} {} ({} scene profiles, {} kept local, {} placed players as spawn points), scene {:016X}, hash {:016X}",
				scan.objects.size(), m_session.IsServer() ? "adopted" : "waiting for the host", scan.profiles.size(), scan.skipped.size(),
				scan.players.size(), asset, scan.hash);
	}

	// Binds the client's own scene entities to the host's network objects. All or nothing: every
	// record is checked against this machine's scene first (the entity exists, is not bound yet, uses
	// the same profile; its state is valid), then each is bound: state applied, interpolation seeded,
	// NetworkIdentity added LAST (as for a proxy). Nothing is created: the scene's own entity stays,
	// with every authored override, material and child it was loaded with.
	bool NetReceiveSystem::ApplySceneObjectBatch(World& world, ByteReader& reader)
	{
		if (!Decode(reader, m_sceneBatch)) { Leave(CloseReason::Malformed, "malformed SceneObjectBatch"); return false; }
		if (!CheckSequence(m_sceneBatch.lifecycleSeq, "SceneObjectBatch")) return false;
		for (std::size_t i = 0; i < m_sceneBatch.objects.size(); ++i)
		{
			const SceneObjectView& o = m_sceneBatch.objects[i];
			const auto local = m_localSceneObjects.find(o.sceneId);
			const NetPrefabDesc* desc = m_content.Prefabs().Desc(o.profile);
			std::string fault;
			CloseReason reason = CloseReason::SceneMismatch;
			if (local == m_localSceneObjects.end() || !world.IsAlive(local->second.entity)) fault = "this scene has no such network object";
			else if (local->second.bound) fault = "bound twice";
			else if (local->second.profile != o.profile) fault = std::format("it uses profile {} here", local->second.profile);
			else if (world.IsAlive(m_session.Entities().Find(o.id))) { fault = "its NetworkId is already in use"; reason = CloseReason::Malformed; }
			else if (desc == nullptr || o.owner > Net::kMaxClients || (o.mask & ~FullMask(*desc)) != 0
				|| !m_content.Serializers().ValidateRecord(o.mask, o.state, ReplicationContext{ .desc = desc, .tick = m_sceneBatch.serverTick }))
			{
				fault = "invalid state";
				reason = CloseReason::Malformed;
			}
			if (!fault.empty())
			{
				if (reason == CloseReason::SceneMismatch) ++m_session.MutableStats().sceneMismatches;
				Leave(reason, std::format("SceneObjectBatch {} record {} of {} (scene object {:016X}, id {}, profile {}): {}",
					m_sceneBatch.lifecycleSeq, i + 1, m_sceneBatch.objects.size(), o.sceneId, o.id, o.profile, fault));
				return false;
			}
		}

		for (const SceneObjectView& o : m_sceneBatch.objects)
		{
			LocalSceneObject& local = m_localSceneObjects[o.sceneId];
			const NetPrefabDesc* desc = m_content.Prefabs().Desc(o.profile);
			std::string error;
			if (!m_content.Serializers().ApplyRecord(world, local.entity, o.mask, o.state, ReplicationContext{ .desc = desc, .tick = m_sceneBatch.serverTick }, &error))
			{
				Leave(CloseReason::SpawnFailed, std::format("scene object {:016X} (id {}): initial state: {}", o.sceneId, o.id, error));
				return false;
			}
			if (o.owner == m_session.LocalPeer()) SetComponentsEnabled(world, local.entity, desc->ownerLocalOnly, true, m_scratch);
			world.AddComponent(local.entity, SeededInterpolation(world, local.entity, m_sceneBatch.serverTick));
			world.AddComponent(local.entity, NetworkIdentity{ .id = o.id, .prefab = o.profile, .owner = o.owner });
			if (!(m_session.Entities().Find(o.id) == local.entity))
			{
				Leave(CloseReason::SpawnFailed, std::format("scene object {:016X}: its NetworkIdentity {} was rejected", o.sceneId, o.id));
				return false;
			}
			local.bound = true;
			++m_session.MutableStats().sceneObjectsBound;
		}
		m_session.MarkLifecycleApplied(m_sceneBatch.lifecycleSeq);
		return true;
	}

	// WorldEnd: the host's world is complete, so a scene object of ours it did not name is one it
	// destroyed before we joined. Destroyed here too, so both worlds hold the same objects.
	void NetReceiveSystem::RemoveUnboundSceneObjects(World& world)
	{
		for (auto& [sceneId, local] : m_localSceneObjects)
		{
			if (local.bound || !world.IsAlive(local.entity)) continue;
			world.DestroyEntity(local.entity);   // with its children
			local.entity = kNullEntity;
			++m_session.MutableStats().sceneObjectsRemoved;
		}
	}

	// ---- snapshots ---------------------------------------------------------------------------

	// One snapshot. All or nothing: the whole message
	// is decoded and every record for a live entity validated before anything is written. Then each
	// record is applied unless the entity already holds something as new (the per-entity tick,
	// NetInterpolation::lastApplied, so a late or reordered snapshot never moves anything back).
	// The snapshot is acknowledged only if every record was applied or deliberately skipped (stale,
	// or an entity despawned since): delivery alone acknowledges nothing.
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

			// The record is applied whole (every replicated component), but a proxy's root pose goes
			// into its interpolation buffer: NetInterpolationSystem draws it 100 ms behind, so the
			// Transform is put back as it was right after the record wrote it (11.4).
			const Transform* before = interpolation != nullptr ? world.Read<Transform>(e) : nullptr;
			const bool buffered = before != nullptr;
			const Math::vec3 drawnPosition = buffered ? before->positions : Math::vec3(0.f);
			const Math::quat drawnRotation = buffered ? before->rotation : Math::quat(1.f, 0.f, 0.f, 0.f);

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
			interpolation = world.Write<NetInterpolation>(e);
			if (interpolation == nullptr) continue;
			interpolation->lastApplied = m_snapshot.serverTick;
			if (buffered)
			{
				const Transform* applied = world.Read<Transform>(e);
				NetInterpolation::Sample sample;
				sample.tick = m_snapshot.serverTick;
				sample.position = applied->positions;
				sample.rotation = applied->rotation;
				if (NetInterpolationSystem::PushSample(*interpolation, sample)) ++stats.interpolationTeleports;
				++stats.interpolationSamples;
				if (applied->positions != drawnPosition || applied->rotation != drawnRotation)
				{
					Transform* t = world.GetComponent<Transform>(e);
					t->positions = drawnPosition;
					t->rotation = drawnRotation;
				}
			}
		}
		if (!complete) { ++stats.snapshotsRejected; return; }

		// The host's clock as this client sees it: the snapshot's tick plus half the round trip.
		ConnectionStatus link;
		const GnsTransport* transport = m_session.Transport();
		const int rttMs = transport != nullptr && transport->Status(m_session.ServerConnection(), link) ? link.pingMs : 0;
		m_session.ServerTime().OnSnapshot(m_snapshot.serverTick, rttMs);

		m_session.SnapshotAcks().Received(m_snapshot.sequence);   // NetInputSystem reports it in the next Input
		++stats.snapshotsApplied;
		stats.lastSnapshotTick = std::max(stats.lastSnapshotTick, m_snapshot.serverTick);
	}
}
