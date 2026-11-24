/*************************************************************************
* \file:         NetReceiveSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.12
* \brief
*                The ECS system that runs the network session once per
*                frame, first in PreUpdate, and makes the World follow it:
*                spawns on the host, proxies and their state on clients.
*
* \details
*                PreUpdate is the stage IECSSystems.h reserves for "input,
*                network receive"; priority -100 puts it before UI and input
*                systems, so everything later in the frame sees this frame's
*                network state.
*
*                Not SimulationOnly: it must keep running while Paused, or
*                messages pile up in GNS and a client leaving would go
*                unnoticed. In Edit the session is Offline and Update returns
*                at once.
*
*                Exclusive: creating and destroying entities (spawns,
*                proxies, despawns) are structural changes, which a declared
*                system may only make directly when it conflicts with every
*                other system (World::RequireStructuralChange). No Query is
*                iterating while it runs.
*
*                Host, each frame:
*                  the session's Update, then the spawns gameplay requested
*                  last frame (RequestSpawn): instantiate, place, switch off
*                  owner-local components of a player the host does not own,
*                  add NetworkIdentity LAST. A failure destroys the whole
*                  subtree and is reported (NetworkEvents::SpawnFailed); the
*                  spawn is never broadcast.
*
*                Client, each frame: every lifecycle message, in arrival
*                order, is first decoded and validated WHOLE (structure, lifecycle sequence,
*                prefab, owner, mask, every record), and only then applied.
*                A message that fails validation closes the connection with
*                Malformed: the reliable stream is corrupt, and skipping a
*                lifecycle message would leave this World permanently
*                different from the host's.
*                  WorldBegin    only first after joining
*                  SpawnBatch    one proxy per record (CreateProxy)
*                  WorldEnd      the world is complete: ClientReady
*                  DespawnBatch  destroys mapped proxies; unknown ids are fine
*                  GameplayEvent posted (NetworkEvents::GameplayEventReceived)
*                Each proxy is made in a fixed order - instantiate, switch off
*                server-only and (unless owned here) owner-local components,
*                apply the initial state as one transaction, add
*                NetInterpolation, add NetworkIdentity LAST - so the id map
*                only ever holds complete proxies. If a proxy fails, every
*                proxy of that message is destroyed again and the client
*                leaves with SpawnFailed.
*
*                Why switching components off in the same frame is enough:
*                physics builds Jolt bodies lazily in its own update (from
*                queries that skip disabled components), scripts start in
*                theirs, and both run after PreUpdate.
*
*                Snapshots (client):
*                unreliable, so a bad one is dropped, never fatal. Each is
*                decoded and every record for a live entity validated (whole-
*                entity records only) before anything is written; then each
*                record is applied unless the entity already holds a tick at
*                least as new (NetInterpolation::lastApplied: no backwards
*                motion from late or reordered snapshots). It is acknowledged
*                only if every record was applied or deliberately skipped.
*                A proxy's root position and rotation go into its
*                NetInterpolation buffer (NetInterpolationSystem draws it
*                100 ms behind) and the snapshot's tick into the session's
*                ServerTime(); the rest of the record is applied as it is.
*
*                Scene objects (Networking Phase 14.5): network objects the
*                loaded scene holds on every machine (NetSceneObjects.h).
*                In the first frame of a session - before any script, AI or
*                physics has run on them - both sides scan the scene and
*                record its identity (asset GUID, scene-object hash). The
*                host adopts each one (a NetworkId, its NetworkIdentity).
*                A client switches off their server-only and owner-local
*                components and keeps them unbound; WorldBegin must carry
*                its own scene identity (else it leaves: SceneMismatch);
*                each SceneObjectBatch record binds its own entity - the
*                one with that scene id - instead of creating a proxy, so
*                its scene-authored data (overrides, materials, children)
*                stays; at WorldEnd its scene objects the host no longer
*                has (destroyed before the join) are destroyed.
*                Scene objects with their own settings (Phase 14.7: scene-
*                only objects, overridden instances) get scene profiles,
*                registered in NetContent in that first frame on both sides.
*                Placed players (Phase 14.6) are removed in that same first
*                frame on every machine; their world positions become the
*                session's PlayerSpawnPoints.
*
*                Each applied snapshot is marked in the session's
*                SnapshotAcks(); NetInputSystem reports them in the client's
*                Input message (it took over the Phase 9 heartbeat).
*
* \history
*    -v1.0       connection handling
*    -v1.1       destroys rejected identities; Debug id-map check
*    -v1.2       Exclusive; host spawns; client lifecycle stream and safe
*                proxy creation
*    -v1.3       snapshots, SnapshotAckWindow, heartbeat Input
*    -v1.4       the heartbeat moved to NetInputSystem and the ack window
*                into the session (SnapshotAckWindow.h)
*    -v1.5       snapshots buffered for interpolation, server time fed;
*                GameplayEvents delivered
*    -v1.6       scene objects: adopted on the host, locked down and bound
*                on clients, scene identity checked (Networking Phase 14.5)
*    -v1.7       placed players become spawn points (Networking Phase 14.6)
*    -v1.8       registers scene profiles (Networking Phase 14.7)
*    -v1.9       PerformSpawn: one spawn, shared with the script bindings
*                (Networking C# Phase 3)
*    -v1.10      scene objects are scanned again after every scene change of the session;
*                SceneChange applied on clients (Networking C# Phase 9)
*    -v1.11      EndSessionObjects: a session's objects end with it (a new session in the same
*                Play found its scene objects still marked as networked)
*    -v1.12      client query bodies on proxies and scene objects, removed with the session
*                (Networking C# Phase 8)
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"
#include "Network/NetworkSession.h"
#include "Network/SnapshotAckWindow.h"   // moved out of this file in v1.4; still reachable from here

namespace Crucible
{
	class NetContent;
	class SceneManager;
	struct NetPrefabDesc;
	class Prefab;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL member of an exported class: used only inside Engine.dll
#endif
	class ENGINE_API NetReceiveSystem final : public IECSSystem
	{
	public:
		/// `session`, `content` and `scenes` are owned by Core and outlive every system.
		/// `content` is written only to register the scene profiles when a session starts.
		NetReceiveSystem(NetworkSession& session, NetContent& content, SceneManager& scenes) noexcept
			: m_session(session), m_content(content), m_scenes(scenes) {}
		NetReceiveSystem(const NetReceiveSystem&) = delete;   // holds received messages (move-only)

		/// The asset GUID of a scene file (Core: the AssetManager's). Unset: 0, which both machines
		/// then agree on, so only the scene-object hash is compared.
		using SceneAssetResolver = std::function<std::uint64_t(const std::filesystem::path&)>;
		void SetSceneAssetResolver(SceneAssetResolver resolver) { m_sceneAssetOf = std::move(resolver); }
		NetReceiveSystem& operator=(const NetReceiveSystem&) = delete;

		SystemStage Stage() const override { return SystemStage::PreUpdate; }
		int         Priority() const override { return -100; }
		void        Describe(SystemDesc& desc) const override;
		void        Update(World& world, SystemContext& ctx) override;

		/// Switches the listed components off (or on) on `root` and every descendant that has
		/// them. Components an entity does not have are skipped.
		static void SetComponentsEnabled(World& world, Entity root, std::span<const ComponentId> ids, bool on,
			std::vector<Entity>& scratch);

		/// Host: performs one spawn now - instantiates the network prefab at the spawn's position
		/// and rotation, switches off what only its owner runs, and adds the NetworkIdentity LAST
		/// (its hook maps the id), then tells the session (NotifySpawned). Returns the root. On
		/// any failure the whole subtree is destroyed, the session is told (NotifySpawnFailed)
		/// and kNullEntity is returned. `spawn.id` comes from RequestSpawn or BeginSpawn. Only
		/// from code that may change the World's structure (an Exclusive system).
		static Entity PerformSpawn(World& world, NetworkSession& session, const NetContent& content, SceneManager& scenes,
			const PendingSpawn& spawn, std::vector<Entity>& scratch);

	private:
		// Host
		void PerformPendingSpawns(World& world);

		// Client
		void ApplyWorldMessages(World& world);
		bool ApplyWorldMessage(World& world, std::span<const std::byte> data);   // false: the session is leaving
		bool CheckSequence(std::uint32_t seq, const char* what);
		bool ApplySpawnBatch(World& world, ByteReader& reader);
		bool ApplyDespawnBatch(World& world, ByteReader& reader);
		bool CreateProxy(World& world, const SpawnView& spawn, Net::NetTick tick, std::string& why);
		void Leave(CloseReason reason, const std::string& why);
		void ApplySnapshot(World& world, ByteReader& reader);

		// Scene objects (both roles)
		void StartSceneObjects(World& world);
		void ForgetPersistentIdentities(World& world);
		void EndSessionObjects(World& world);
		bool ApplySceneObjectBatch(World& world, ByteReader& reader);
		void RemoveUnboundSceneObjects(World& world);

		NetworkSession&           m_session;
		NetContent&               m_content;
		SceneManager&             m_scenes;
		std::vector<Entity>       m_rejected;        // reused every frame
		std::vector<PendingSpawn> m_spawns;          // host: this frame's requests
		std::vector<ReceivedMessage> m_world;        // client: this frame's lifecycle messages
		SpawnBatchView            m_spawnBatch;      // client: reused decode targets
		DespawnBatchView          m_despawnBatch;
		std::vector<Entity>       m_created;         // client: proxies made by the current message
		std::vector<Entity>       m_scratch;         // subtree walks
		SnapshotView              m_snapshot;        // client: one snapshot, decoded (fixed size, no allocation)
		SceneObjectBatchView      m_sceneBatch;      // client: reused decode target

		struct LocalSceneObject { Entity entity = kNullEntity; Net::NetPrefabId profile = Net::kInvalidNetPrefab; bool bound = false; };
		SceneAssetResolver        m_sceneAssetOf;
		std::uint32_t             m_sceneGeneration = 0;   // the session's, when the scene was last scanned
		std::uint32_t             m_sceneEpoch = 0;        // the session's SceneEpoch(), when it was last scanned
		bool                      m_sceneStarted = false;
		std::unordered_map<std::uint64_t, LocalSceneObject> m_localSceneObjects;   // client: scene id -> own entity
		std::vector<Entity>       m_resolved;        // client: its records' entities (null: despawned since)

		// The scene's own entities this session made network objects (both roles), with what the
		// client switched off on them: given back as plain scene entities when the session ends.
		struct SessionSceneObject
		{
			Entity entity = kNullEntity;
			std::vector<ComponentId> switchedOff;
			std::vector<Entity> queryBodies;   // client: removed when the session ends (Phase 8)
		};
		std::vector<SessionSceneObject> m_sessionSceneObjects;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
