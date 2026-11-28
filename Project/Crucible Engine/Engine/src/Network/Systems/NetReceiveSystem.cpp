/*************************************************************************
* \file:         NetReceiveSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.26
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
*    -v1.10      PeerRoster applied through the session (Networking C# Phase 1)
*    -v1.11      PerformSpawn (shared with the script bindings); a
*                GameplayEvent's target is passed on
*    -v1.12      NetVariables ensured on spawned, adopted and bound objects (Networking C#
*                Phase 4); UnreliableEvent delivered (Phase 5B)
*    -v1.13      SceneChange: forget, load, rescan, SceneLoaded; stale identities of
*                DontDestroyOnLoad entities removed (Networking C# Phase 9)
*    -v1.14      EndSessionObjects: spawned objects removed and scene objects made local again
*                when a session ends, so the next one in the same Play starts clean
*    -v1.15      NetAnimator removed from scene objects too (Networking C# Phase 7)
*    -v1.16      client query bodies (Network/NetQueryBodies.h) on proxies, except this
*                client's own player, and on scene objects (Networking C# Phase 8)
*    -v1.17      protocol 9: RecordMask; WorldBegin's scene seed is kept (SetSceneSeedFromHost)
*    -v1.18      query bodies follow a replicated scale and CharacterController size
*                (RefreshClientQueryBodies after every record that can change them)
*    -v1.19      synced children: bound after every record of an object that
*                replicates them; the runtime sync components leave scene objects when the session ends
*    -v1.20      attachments: applied after every record (AttachOrWait), retried every
*                frame while the parent or mount is missing; network objects below a despawned proxy are
*                detached first (DetachNetworkDescendants)
*    -v1.21      renderers: ApplyRenderers after every record of an object that
*                replicates them
*    -v1.22      synced children's on/off: ApplyChildOnOff after every record of an
*                object that replicates Children/OnOff
*    -v1.23      ownership transfer: the host applies requests (players refused) and
*                gives a departed client's objects back; a client applies OwnerChange; owner-only
*                components follow the owner on every machine
*    -v1.24      NetData removed from scene objects when the session ends
*    -v1.25      objects kept across scene changes (protocol 12): the host's CollectKept, the client
*                keeps its copies (KeepForSceneChange), rebinds them from the new world, drops the rest
*    -v1.26      players of any player prefab (a scene's player is the prefab it places)
*
**************************************************************************/
#include "Network/Systems/NetReceiveSystem.h"

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Network/NetAttachments.h"
#include "Network/NetComponents.h"
#include "Network/NetQueryBodies.h"
#include "Network/NetRendererSync.h"
#include "Network/NetContent.h"
#include "Network/NetIdentityHooks.h"
#include "Network/NetSceneObjects.h"
#include "Network/NetSyncedChildren.h"
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
		// Whether a record of this profile can change what a query body copies: a replicated scale
		// (a character's body undoes it) or a character's size.
		bool ResizesQueryBodies(const NetPrefabDesc& desc) noexcept
		{
			return std::ranges::any_of(desc.replicated, [](NetComponentId id) {
				return id == NetComponentId::TransformFullScale || id == NetComponentId::TransformYawScale
					|| id == NetComponentId::CharacterControllerSize;
			});
		}

		bool SyncsRenderers(const NetPrefabDesc& desc) noexcept
		{
			return std::ranges::find(desc.replicated, NetComponentId::Renderers) != desc.replicated.end();
		}

		bool SyncsAttachment(const NetPrefabDesc& desc) noexcept
		{
			return std::ranges::find(desc.replicated, NetComponentId::Attachment) != desc.replicated.end();
		}

		bool SyncsChildOnOff(const NetPrefabDesc& desc) noexcept
		{
			return std::ranges::find(desc.replicated, NetComponentId::ChildOnOff) != desc.replicated.end();
		}

		bool SyncsChildren(const NetPrefabDesc& desc) noexcept
		{
			return std::ranges::any_of(desc.replicated, [](NetComponentId id) {
				return id == NetComponentId::ChildRotations || id == NetComponentId::ChildTransforms;
			});
		}

		RecordMask FullMask(const NetPrefabDesc& desc) noexcept
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
		m_lastWorld = &world;
		if (!m_keptQuerySet)
		{
			m_keptQuerySet = true;
			m_session.SetKeptObjectsQuery([this](std::vector<std::pair<Net::NetworkId, Entity>>& out) { CollectKept(out); });
		}
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

		// Scene objects: once per session and scene - in the session's first frame, and in the
		// frame after each scene change it made (Phase 9) - before anything has run on them.
		if (m_session.Generation() != m_sceneGeneration || m_session.SceneEpoch() != m_sceneEpoch)
		{
			// The session ended or a new one began: what the old one made networked ends with it.
			if (m_session.Generation() != m_sceneGeneration) EndSessionObjects(world);
			m_sceneGeneration = m_session.Generation();
			m_sceneEpoch = m_session.SceneEpoch();
			m_sceneStarted = false;
			m_localSceneObjects.clear();
		}
		if (!m_sceneStarted && m_session.State() == SessionState::Running && m_session.IsActive())
		{
			m_sceneStarted = true;
			if (m_session.SceneEpoch() > 0) ForgetPersistentIdentities(world);
			StartSceneObjects(world);
			if (m_session.IsClient()) m_session.ConfirmSceneLoaded();   // after a scene change: SceneLoaded
		}

		if (m_session.IsServer()) PerformPendingSpawns(world);
		if (m_session.IsServer()) ApplyOwnerRequests(world);   // after the spawns: a request may name one
		else if (m_session.IsClient()) ApplyWorldMessages(world);   // NetInputSystem sends the Input later this stage
		if (m_session.IsClient()) RetryAttachments(world);          // parents that arrived since
		else m_waitingAttachments.clear();

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
		for (const PendingSpawn& spawn : m_spawns) PerformSpawn(world, m_session, m_content, m_scenes, spawn, m_scratch);
		m_spawns.clear();
	}

	Entity NetReceiveSystem::PerformSpawn(World& world, NetworkSession& session, const NetContent& content, SceneManager& scenes,
		const PendingSpawn& spawn, std::vector<Entity>& scratch)
	{
		Entity root = kNullEntity;
		try
		{
			const NetPrefabDesc* desc = content.Prefabs().Desc(spawn.prefab);
			const Prefab* prefab = content.Prefabs().PrefabOf(spawn.prefab);
			if (desc == nullptr || prefab == nullptr) throw std::runtime_error("the prefab is not registered or not loaded");

			root = prefab->Instantiate(world, kNullEntity,
				[&scenes](Entity entity) { return scenes.EnsureId(entity).m_Value; });   // all or nothing (Prefab.cpp)
			if (!world.IsAlive(root)) throw std::runtime_error("the prefab did not instantiate");
			if (Transform* t = world.Write<Transform>(root))
			{
				t->positions = spawn.position;
				t->rotation = spawn.rotation;
			}
			// The host simulates everything, but a player's camera rig belongs to whoever plays it.
			if (spawn.owner != session.LocalPeer()) SetComponentsEnabled(world, root, desc->ownerLocalOnly, false, scratch);
			content.Serializers().EnsureRuntimeComponents(world, root, ReplicationContext{ .desc = desc });   // NetVariables

			world.AddComponent(root, NetworkIdentity{ .id = spawn.id, .prefab = spawn.prefab, .owner = spawn.owner });
			if (!(session.Entities().Find(spawn.id) == root)) throw std::runtime_error("its NetworkIdentity was rejected");
			session.NotifySpawned(spawn.id);
			return root;
		}
		catch (const std::exception& e)
		{
			if (world.IsAlive(root)) world.DestroyEntity(root);   // removes the whole subtree
			session.NotifySpawnFailed(spawn.id, spawn.prefab, e.what());
			return kNullEntity;
		}
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
			m_session.SetSceneSeedFromHost(begin.sceneSeed);   // a client that joined into a running scene learns it here
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
			DropUnboundKept(world);             // nor these (destroyed during the change)
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
		case MessageType::OwnerChange:
			if (state != ClientWorld::Ready) { Leave(CloseReason::Malformed, "OwnerChange inside the world stream"); return false; }
			return ApplyOwnerChange(world, reader);
		case MessageType::SceneObjectBatch:
			if (state != ClientWorld::Receiving) { Leave(CloseReason::Malformed, "a SceneObjectBatch outside the world stream"); return false; }
			return ApplySceneObjectBatch(world, reader);
		case MessageType::GameplayEvent:
		{
			GameplayEventView event;
			if (!Decode(reader, event)) { Leave(CloseReason::Malformed, "malformed GameplayEvent"); return false; }
			if (!CheckSequence(event.lifecycleSeq, "GameplayEvent")) return false;
			if (state != ClientWorld::Ready) { Leave(CloseReason::Malformed, "GameplayEvent inside the world stream"); return false; }
			m_session.DeliverGameplayEvent(event.kind, event.payload, event.target);   // the payload is the game's to check
			m_session.MarkLifecycleApplied(event.lifecycleSeq);
			return true;
		}
		case MessageType::PeerRoster:
		{
			// Who is in the session. It needs no World and may come at any point of the stream
			// (the first one right after JoinAccepted, before the world).
			PeerRoster roster;
			if (!Decode(reader, roster)) { Leave(CloseReason::Malformed, "malformed PeerRoster"); return false; }
			if (!CheckSequence(roster.lifecycleSeq, "PeerRoster")) return false;
			if (!m_session.ApplyPeerRoster(roster)) { Leave(CloseReason::Malformed, "a PeerRoster without this client in it"); return false; }
			m_session.MarkLifecycleApplied(roster.lifecycleSeq);
			return true;
		}
		case MessageType::SceneChange:
		{
			// The host moved to another scene (Phase 9). Allowed in any world state: it may even
			// overtake a world this client is still receiving. This scene's network objects are
			// forgotten now (they go with the scene), the load happens at the next frame's safe
			// point, and the new world follows SceneLoaded.
			SceneChange change;
			if (!Decode(reader, change)) { Leave(CloseReason::Malformed, "malformed SceneChange"); return false; }
			if (!CheckSequence(change.lifecycleSeq, "SceneChange")) return false;
			m_session.MarkLifecycleApplied(change.lifecycleSeq);
			KeepForSceneChange(world, change, m_kept);   // before the load: they must survive it
			m_session.BeginClientSceneLoad(change, m_kept);
			m_localSceneObjects.clear();
			const bool requested = m_sceneLoader ? m_sceneLoader(change.sceneAsset) : m_scenes.RequestLoadAsset(change.sceneAsset, true);   // past any loading screen
			if (!requested)
			{
				++m_session.MutableStats().sceneMismatches;
				Leave(CloseReason::SceneMismatch, std::format("the host moved to scene {:016X}, which this project does not have", change.sceneAsset));
				return false;
			}
			m_session.NoteSceneLoadIssued();
			return true;
		}
		case MessageType::UnreliableEvent:
		{
			// Unreliable (lane 1), like a snapshot: a bad or early one is dropped, never fatal.
			UnreliableEventView event;
			if (state != ClientWorld::Ready) { ++m_session.MutableStats().unexpected; return true; }
			if (!Decode(reader, event)) { ++m_session.MutableStats().malformed; return true; }
			const std::uint64_t before = m_session.Stats().gameplayEventsReceived;
			m_session.DeliverGameplayEvent(event.kind, event.payload, event.target);   // drops one about an unknown object
			if (m_session.Stats().gameplayEventsReceived != before) ++m_session.MutableStats().unreliableEventsReceived;
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
		if (const Entity existing = m_session.Entities().Find(spawn.id); world.IsAlive(existing))
		{
			if (m_session.TakeKept(spawn.id)) return Rebind(world, existing, spawn, tick, why);   // kept through the scene change
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
			// What this client's raycasts hit instead of the Host-only physics; they go with the
			// proxy. Not on its own player: its camera's rays would hit its own body.
			if (!(spawn.owner == m_session.LocalPeer() && m_content.IsPlayerPrefab(spawn.prefab)))
			{
				std::vector<Entity> bodies;
				AddClientQueryBodies(world, root, bodies);
			}

			std::string error;
			const ReplicationContext context{ .desc = desc, .tick = tick };
			m_content.Serializers().EnsureRuntimeComponents(world, root, context);   // NetVariables: the record fills it
			if (!m_content.Serializers().ApplyRecord(world, root, spawn.mask, spawn.state, context, &error))
				throw std::runtime_error("initial state: " + error);
			if (ResizesQueryBodies(*desc)) RefreshClientQueryBodies(world, root);   // the bodies were made from the prefab's values
			if (SyncsChildren(*desc)) BindSyncedChildren(world, root, *desc);
			if (SyncsRenderers(*desc)) ApplyRenderers(world, root, *desc);
			if (SyncsChildOnOff(*desc)) ApplyChildOnOff(world, root, *desc);
			if (SyncsAttachment(*desc)) AttachOrWait(world, root);

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

	// ---- kept across scene changes (protocol 12) ---------------------------------------------

	// Host, from the session at a scene change (the safe point, before the load): the spawned network
	// objects that survive it - marked DontDestroyOnLoad, themselves or through an ancestor. With
	// KeepPlayers, every player character is marked first. Scene objects belong to their scene and
	// are never kept (ForgetPersistentIdentities says so if one was marked).
	void NetReceiveSystem::CollectKept(std::vector<std::pair<Net::NetworkId, Entity>>& out)
	{
		out.clear();
		if (m_lastWorld == nullptr) return;
		World& world = *m_lastWorld;
		if (m_session.KeepPlayers())
		{
			for (const auto& [id, entity] : m_session.Entities())
			{
				const NetworkIdentity* identity = world.IsAlive(entity) ? world.Read<NetworkIdentity>(entity) : nullptr;
				if (identity != nullptr && m_content.IsPlayerPrefab(identity->prefab) && !world.GetParent(entity).IsValid())
					m_scenes.DontDestroyOnLoad(entity);
			}
		}
		for (const auto& [id, entity] : m_session.Entities())
			if (world.IsAlive(entity) && m_session.SceneIdOf(id) == 0 && m_scenes.IsPersistent(entity)) out.emplace_back(id, entity);
	}

	// Client: the host keeps these objects through its scene change, so this client keeps its copies:
	// the roots are marked DontDestroyOnLoad (an attached one goes with the root it hangs under).
	// Ids this client does not have are skipped; the new world spawns them.
	void NetReceiveSystem::KeepForSceneChange(World& world, const SceneChange& change, std::vector<std::pair<Net::NetworkId, Entity>>& out)
	{
		out.clear();
		for (const Net::NetworkId id : change.Kept())
		{
			const Entity e = m_session.Entities().Find(id);
			if (world.IsAlive(e) && !world.GetParent(e).IsValid()) m_scenes.DontDestroyOnLoad(e);
		}
		for (const Net::NetworkId id : change.Kept())
		{
			const Entity e = m_session.Entities().Find(id);
			if (world.IsAlive(e) && m_scenes.IsPersistent(e)) out.emplace_back(id, e);
		}
	}

	// Client: the new world names a kept object again: its record is applied to the copy kept here (as
	// a snapshot would), and its interpolation starts over, so a player the host moved to the new
	// scene's spawn point is drawn there at once instead of sliding across.
	bool NetReceiveSystem::Rebind(World& world, Entity root, const SpawnView& spawn, Net::NetTick tick, std::string& why)
	{
		const NetworkIdentity* identity = world.Read<NetworkIdentity>(root);
		const NetPrefabDesc* desc = m_content.Prefabs().Desc(spawn.prefab);
		if (identity == nullptr || desc == nullptr || identity->prefab != spawn.prefab)
		{
			why = "a kept object came back as another prefab";
			return false;
		}
		const ReplicationContext context{ .desc = desc, .tick = tick };
		m_content.Serializers().EnsureRuntimeComponents(world, root, context);
		std::string error;
		if (!m_content.Serializers().ApplyRecord(world, root, spawn.mask, spawn.state, context, &error))
		{
			why = "kept object's state: " + error;
			return false;
		}
		if (ResizesQueryBodies(*desc)) RefreshClientQueryBodies(world, root);
		if (SyncsChildren(*desc)) BindSyncedChildren(world, root, *desc);
		if (SyncsRenderers(*desc)) ApplyRenderers(world, root, *desc);
		if (SyncsChildOnOff(*desc)) ApplyChildOnOff(world, root, *desc);
		if (SyncsAttachment(*desc)) AttachOrWait(world, root);
		if (NetInterpolation* interpolation = world.Write<NetInterpolation>(root)) *interpolation = SeededInterpolation(world, root, tick);
		else world.AddComponent(root, SeededInterpolation(world, root, tick));
		if (identity->owner != spawn.owner) SetOwner(world, root, spawn.owner);
		++m_session.MutableStats().keptRebound;
		return true;
	}

	void NetReceiveSystem::DropUnboundKept(World& world)
	{
		m_session.TakeUnboundKept(m_unbound);
		for (const Net::NetworkId id : m_unbound)
		{
			const Entity e = m_session.Entities().Find(id);
			if (!world.IsAlive(e)) continue;
			DetachNetworkDescendants(world, e);
			world.DestroyEntity(e);
			++m_session.MutableStats().keptDropped;
		}
		m_unbound.clear();
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
				// Network objects attached below it are their own objects: the host announces their
				// despawn too if it destroyed them, but one it detached first must survive here.
				DetachNetworkDescendants(world, e);
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

	// ---- ownership --------------------------------------------------------

	// The new owner in the identity, and the owner-only components switched for it: on where this
	// machine is the owner, off elsewhere. Then the event (and, on the host, the message).
	bool NetReceiveSystem::SetOwner(World& world, Entity entity, Net::PeerId owner)
	{
		const NetworkIdentity* identity = world.IsAlive(entity) ? world.Read<NetworkIdentity>(entity) : nullptr;
		if (identity == nullptr) return false;
		const Net::PeerId previous = identity->owner;
		if (previous == owner) return true;
		const Net::NetworkId id = identity->id;
		const NetPrefabDesc* desc = m_content.Prefabs().Desc(identity->prefab);
		world.Write<NetworkIdentity>(entity)->owner = owner;   // the id and prefab never change: the map stays valid
		if (desc != nullptr && !desc->ownerLocalOnly.empty())
			SetComponentsEnabled(world, entity, desc->ownerLocalOnly, owner == m_session.LocalPeer(), m_scratch);
		m_session.NoteOwnerChanged(id, previous, owner);
		return true;
	}

	void NetReceiveSystem::ApplyOwnerRequests(World& world)
	{
		m_session.TakeOwnerRequests(m_ownerRequests);
		// A client that left: its objects (not its player, which NetInputSystem removes) go back to
		// the host, so a later player who gets the same PeerId does not inherit them.
		for (const auto& [id, entity] : m_session.Entities())
		{
			const NetworkIdentity* identity = world.IsAlive(entity) ? world.Read<NetworkIdentity>(entity) : nullptr;
			if (identity == nullptr || identity->owner == Net::kServerPeer || identity->owner > Net::kMaxClients) continue;
			if (identity->prefab == m_content.PlayerPrefab()) continue;
			const PeerState& peer = m_session.Peers()[identity->owner - 1];
			if (peer.IsJoined()) continue;
			if (std::ranges::none_of(m_ownerRequests, [&](const OwnerChangeRequest& r) { return r.id == id; }))
				m_ownerRequests.push_back(OwnerChangeRequest{ .id = id, .owner = Net::kServerPeer });
		}
		for (const OwnerChangeRequest& request : m_ownerRequests)
		{
			const Entity entity = m_session.Entities().Find(request.id);
			const NetworkIdentity* identity = world.IsAlive(entity) ? world.Read<NetworkIdentity>(entity) : nullptr;
			if (identity == nullptr) { ++m_session.MutableStats().ownerChangesRefused; continue; }   // despawned since
			if (identity->prefab == m_content.PlayerPrefab())
			{
				++m_session.MutableStats().ownerChangesRefused;
				CRU_ERROR("[Net] ownership of network object {} not changed: a player always belongs to its own peer", request.id);
				continue;
			}
			if (request.owner != Net::kServerPeer && !m_session.Peers()[request.owner - 1].IsJoined())
			{
				++m_session.MutableStats().ownerChangesRefused;   // left between the request and now
				continue;
			}
			SetOwner(world, entity, request.owner);
		}
		m_ownerRequests.clear();
	}

	bool NetReceiveSystem::ApplyOwnerChange(World& world, ByteReader& reader)
	{
		OwnerChange change;
		if (!Decode(reader, change)) { Leave(CloseReason::Malformed, "malformed OwnerChange"); return false; }
		if (!CheckSequence(change.lifecycleSeq, "OwnerChange")) return false;
		const Entity entity = m_session.Entities().Find(change.id);
		if (!SetOwner(world, entity, change.owner)) ++m_session.MutableStats().unknownOwnerChanges;   // despawned since: fine
		m_session.MarkLifecycleApplied(change.lifecycleSeq);
		return true;
	}

	// ---- attachments ------------------------------------------------------

	void NetReceiveSystem::AttachOrWait(World& world, Entity root)
	{
		const auto find = [this](Net::NetworkId id) { return m_session.Entities().Find(id); };
		if (ApplyAttachment(world, root, find)) return;
		if (std::ranges::find(m_waitingAttachments, root) == m_waitingAttachments.end()) m_waitingAttachments.push_back(root);
	}

	void NetReceiveSystem::RetryAttachments(World& world)
	{
		if (m_waitingAttachments.empty()) return;
		const auto find = [this](Net::NetworkId id) { return m_session.Entities().Find(id); };
		std::erase_if(m_waitingAttachments, [&](Entity root) { return !world.IsAlive(root) || ApplyAttachment(world, root, find); });
	}

	// ---- scene objects (Networking Phase 14.5) ------------------------------------------------

	// The first frame of a session: the scene as it was loaded. Both sides record its identity; the
	// host adopts its scene objects; a client makes its own inert until they are bound - server-only
	// and owner-local components off (the host owns scene objects), before scripts, AI or physics
	// ever see them (they run later this frame, in Update and PostUpdate).
	// A session that ended (or the one before a new one) leaves its marks in the World: entities it
	// spawned - players, projectiles, a client's copies of the host's objects - and the scene's own
	// entities it made network objects. Without this, the next session in the same Play would find
	// those still carrying a NetworkIdentity: a scene object would not be adopted again (so host and
	// client disagree about the scene), and a client that left would keep seeing old players.
	// Spawned ones are destroyed; the scene's own become plain scene entities again, as loaded.
	void NetReceiveSystem::EndSessionObjects(World& world)
	{
		std::vector<Entity> spawned;
		world.CreateQuery<const NetworkIdentity>().ForEach([&](Entity e, const NetworkIdentity&) {
			const bool sceneObject = std::ranges::any_of(m_sessionSceneObjects, [&](const SessionSceneObject& o) { return o.entity == e; });
			if (!sceneObject) spawned.push_back(e);
		});
		for (const SessionSceneObject& object : m_sessionSceneObjects)
		{
			RemoveClientQueryBodies(world, object.queryBodies);   // before the scene object is checked: they are its children
			if (!world.IsAlive(object.entity)) continue;
			if (world.HasComponent<NetworkIdentity>(object.entity)) world.RemoveComponent<NetworkIdentity>(object.entity);
			if (world.HasComponent<NetInterpolation>(object.entity)) world.RemoveComponent<NetInterpolation>(object.entity);
			if (world.HasComponent<NetVariables>(object.entity)) world.RemoveComponent<NetVariables>(object.entity);   // the next session starts at 0
			if (world.HasComponent<NetAnimator>(object.entity)) world.RemoveComponent<NetAnimator>(object.entity);     // the same
			if (world.HasComponent<NetChildTransforms>(object.entity)) world.RemoveComponent<NetChildTransforms>(object.entity);
			if (world.HasComponent<NetAttachment>(object.entity)) world.RemoveComponent<NetAttachment>(object.entity);
			if (world.HasComponent<NetRenderers>(object.entity)) world.RemoveComponent<NetRenderers>(object.entity);
			if (world.HasComponent<NetChildEnabled>(object.entity)) world.RemoveComponent<NetChildEnabled>(object.entity);
			if (world.HasComponent<NetData>(object.entity)) world.RemoveComponent<NetData>(object.entity);   // the next session starts empty
			SetComponentsEnabled(world, object.entity, object.switchedOff, true, m_scratch);   // what the client switched off
		}
		for (const Entity e : spawned)
			if (world.IsAlive(e)) world.DestroyEntity(e);   // with its children; unmapped already, so nothing is announced
		if (!spawned.empty() || !m_sessionSceneObjects.empty())
			CRU_INFO("[Net] the session's objects were cleared: {} spawned removed, {} scene objects made local again",
				spawned.size(), m_sessionSceneObjects.size());
		m_sessionSceneObjects.clear();
	}

	// A DontDestroyOnLoad entity that was a network object kept its NetworkIdentity through the
	// load, but the session forgot every id of the old scene: it is a local entity now. Its stale
	// identity goes, so it can never be taken for a network object again.
	void NetReceiveSystem::ForgetPersistentIdentities(World& world)
	{
		m_scratch.clear();
		world.CreateQuery<const NetworkIdentity>().ForEach([&](Entity e, const NetworkIdentity& identity) {
			if (!(m_session.Entities().Find(identity.id) == e)) m_scratch.push_back(e);
		});
		for (const Entity e : m_scratch)
		{
			CRU_WARN("[Net] entity {}:{} kept through a scene change is no longer a network object (DontDestroyOnLoad does not carry network objects across scenes)",
				e.index, e.generation);
			world.RemoveComponent<NetworkIdentity>(e);
			if (world.HasComponent<NetInterpolation>(e)) world.RemoveComponent<NetInterpolation>(e);
		}
		m_scratch.clear();
	}

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
				m_content.Serializers().EnsureRuntimeComponents(world, object.entity, ReplicationContext{ .desc = desc });   // NetVariables
				world.AddComponent(object.entity, NetworkIdentity{ .id = id, .prefab = object.profile, .owner = Net::kServerPeer });
				if (!(m_session.Entities().Find(id) == object.entity)) CRU_ERROR("[Net] scene object {:016X} could not be adopted", object.sceneId);
				m_sessionSceneObjects.push_back(SessionSceneObject{ .entity = object.entity });
			}
			else
			{
				SetComponentsEnabled(world, object.entity, desc->serverOnly, false, m_scratch);
				SetComponentsEnabled(world, object.entity, desc->ownerLocalOnly, false, m_scratch);
				m_localSceneObjects[object.sceneId] = LocalSceneObject{ .entity = object.entity, .profile = object.profile };
				SessionSceneObject kept{ .entity = object.entity, .switchedOff = desc->serverOnly };
				kept.switchedOff.insert(kept.switchedOff.end(), desc->ownerLocalOnly.begin(), desc->ownerLocalOnly.end());
				AddClientQueryBodies(world, object.entity, kept.queryBodies);   // removed when the session ends
				m_sessionSceneObjects.push_back(std::move(kept));
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
		m_session.SetPlayerSpawnPoints(std::move(points), scan.playerPrefab);

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
			m_content.Serializers().EnsureRuntimeComponents(world, local.entity, ReplicationContext{ .desc = desc });   // NetVariables
			if (!m_content.Serializers().ApplyRecord(world, local.entity, o.mask, o.state, ReplicationContext{ .desc = desc, .tick = m_sceneBatch.serverTick }, &error))
			{
				Leave(CloseReason::SpawnFailed, std::format("scene object {:016X} (id {}): initial state: {}", o.sceneId, o.id, error));
				return false;
			}
			if (ResizesQueryBodies(*desc)) RefreshClientQueryBodies(world, local.entity);
			if (SyncsChildren(*desc)) BindSyncedChildren(world, local.entity, *desc);
			if (SyncsRenderers(*desc)) ApplyRenderers(world, local.entity, *desc);
			if (SyncsChildOnOff(*desc)) ApplyChildOnOff(world, local.entity, *desc);
			if (SyncsAttachment(*desc)) AttachOrWait(world, local.entity);
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
			if (ResizesQueryBodies(*context.desc)) RefreshClientQueryBodies(world, e);   // scale and size apply as they come
			if (SyncsChildren(*context.desc)) BindSyncedChildren(world, e, *context.desc);
			if (SyncsRenderers(*context.desc)) ApplyRenderers(world, e, *context.desc);   // visuals apply as they come
			if (SyncsChildOnOff(*context.desc)) ApplyChildOnOff(world, e, *context.desc);
			const bool attaches = SyncsAttachment(*context.desc);
			interpolation = world.Write<NetInterpolation>(e);
			if (interpolation == nullptr) { if (attaches) AttachOrWait(world, e); continue; }
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
			// After the new sample is in the buffer: a new parent restarts it from that sample.
			if (attaches) AttachOrWait(world, e);
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
