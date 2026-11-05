/*************************************************************************
* \file:         NetReplicationSendSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.2
* \brief
*                The host's sending system: the lifecycle stream (the world
*                for a joining client, then every spawn and despawn) and
*                state snapshots.
*
* \details
*                PostUpdate, after physics (priority 1), so records carry
*                this frame's state. Exclusive: serializers may read any
*                component, and an entity whose spawn record cannot be
*                written is destroyed here. Not SimulationOnly: joins,
*                despawns and acknowledgements keep flowing while Paused.
*
*                The tick (Networking Guide 18.4, 6.2 "steps 6-7"): every
*                frame the clock advances by the fixed steps physics really
*                ran (the StepSource: Core reads ECSPhysicsSystem's
*                GetStepsLastFrame() while Playing, 0 otherwise), so a
*                snapshot's tick names an exact simulation step and a paused
*                host neither advances the tick nor sends snapshots.
*
*                Each frame, on the host (Networking Guide 9.3, 9.4):
*                  1. Joining clients (JoinAccepted sent this frame): their
*                     whole world, from the World as it is now - WorldBegin,
*                     SpawnBatches for every mapped networked entity in id
*                     order, WorldEnd. The peer then waits for ClientReady.
*                  2. Everyone else already Synchronizing or InGame: the
*                     spawns performed this frame, then the despawns queued
*                     since last frame, then this frame's gameplay events
*                     (NetworkSession::SendGameplayEvent), each to everyone
*                     or to its one client. So an event that names an entity
*                     spawned this frame always arrives after that spawn.
*                A joining client gets no incremental messages in its join
*                frame, because its world already holds every change made up
*                to now; from the next frame on it gets all of them. Every
*                change is therefore in exactly one of "the world" or "a
*                later incremental message", which lane 0's ordering then
*                delivers in order.
*
*                Snapshots (this version: the correct baseline, Networking
*                Guide PR 9, issues 1 and 12; the bandwidth budget comes
*                later):
*                  - Per client and entity the host remembers which lifecycle
*                    message carried the spawn. The entity is CONFIRMED once
*                    the client reports (Input.appliedLifecycle, or the
*                    ClientReady for the world) that it applied that message.
*                    Only confirmed entities go into that client's snapshots,
*                    so a snapshot can never race ahead of the spawn it
*                    depends on, and an ack can never name an unapplied record.
*                  - Every 2 steps (30 Hz), every InGame client gets every
*                    confirmed entity, whole-entity records (the full mask),
*                    in id order, in messages of at most kMaxSnapshotBytes,
*                    at most kMaxSnapshotMessagesPerClient of them. Entities
*                    that do not fit go first in the next snapshot.
*                  - A message is remembered (64-entry ring) only if GNS took
*                    it. An acknowledged snapshot marks what it carried as
*                    held by the client, at that snapshot's tick.
*                  - Despawned ids, closed peers and ended sessions leave no
*                    per-client state behind.
*
*                A spawn record that cannot be written (a serializer fails)
*                is a server bug: the entity is left out, destroyed, and
*                reported (NetworkEvents::SpawnFailed). A snapshot record
*                that cannot be written is left out of that snapshot and
*                counted.
*
* \history
*    -v1.0       lifecycle only: world stream, spawns, despawns (Networking
*                Guide 9.3, 9.4, PR 8)
*    -v1.1       snapshots: step-stamped tick, confirmed-spawn gating, full
*                state every snapshot, acks (PR 9)
*    -v1.2       gameplay events after the frame's spawns and despawns
*                (PR 12)
*
**************************************************************************/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"
#include "Network/GnsTransport.h"
#include "Network/NetworkSession.h"   // QueuedGameplayEvent
#include "Network/NetTypes.h"
#include "Network/Protocol.h"

namespace Crucible
{
	class NetworkSession;
	class NetContent;
	struct PeerState;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members of an exported class: used only inside Engine.dll
#endif
	class ENGINE_API NetReplicationSendSystem final : public IECSSystem
	{
	public:
		/// The fixed simulation steps that really ran this frame (0 while paused or in Edit).
		using StepSource = std::function<std::uint32_t()>;

		/// Snapshot messages per client per snapshot, at most (about 8.8 KB, 264 KB/s at 30 Hz).
		/// A safety limit for this baseline; the per-client budget replaces it later.
		static constexpr std::size_t kMaxSnapshotMessagesPerClient = 8;

		/// What the host knows about one client's copy of one entity.
		struct ClientEntityState
		{
			std::uint32_t spawnSeq = 0;       // lifecycleSeq of the SpawnBatch that carried it to this client
			bool          confirmed = false;  // the client applied that SpawnBatch: snapshots may carry it
			bool          acked = false;      // some snapshot carrying it was acknowledged
			Net::NetTick  ackedTick = 0;      // tick of the newest acknowledged snapshot carrying it
			Net::NetTick  lastSentTick = 0;   // tick of the newest snapshot sent with it
		};

		/// `session` and `content` are owned by Core and outlive every system.
		NetReplicationSendSystem(NetworkSession& session, const NetContent& content, StepSource steps);
		NetReplicationSendSystem(const NetReplicationSendSystem&) = delete;
		NetReplicationSendSystem& operator=(const NetReplicationSendSystem&) = delete;

		SystemStage Stage() const override { return SystemStage::PostUpdate; }
		int         Priority() const override { return 100; }   // after ECSPhysicsSystem (1)
		void        Describe(SystemDesc& desc) const override;
		void        Update(World& world, SystemContext& ctx) override;

		/// Diagnostics and tests: this host's record of `peer`'s copy of `id`, or null.
		const ClientEntityState* ClientState(Net::PeerId peer, Net::NetworkId id) const;

	private:
		/// One record, its state bytes at [offset, offset + size) of m_state.
		struct Record
		{
			Net::NetworkId   id = Net::kInvalidNetworkId;
			Net::NetPrefabId prefab = Net::kInvalidNetPrefab;
			Net::PeerId      owner = Net::kServerPeer;
			std::uint8_t     mask = 0;
			std::uint32_t    offset = 0;
			std::uint8_t     size = 0;
		};

		/// One snapshot message GNS accepted, until it is acknowledged or overwritten.
		struct SentSnapshot
		{
			std::uint16_t               sequence = 0;
			Net::NetTick                tick = 0;
			bool                        valid = false;
			std::vector<Net::NetworkId> ids;   // which entities it carried
		};

		/// Everything the host keeps about one client slot.
		struct ClientView
		{
			NetConnection connection = kInvalidConnection;   // the client it belongs to
			std::unordered_map<Net::NetworkId, ClientEntityState> states;
			std::array<SentSnapshot, 64> sent{};
			std::uint16_t  lastSequence = 0;        // newest sequence sent (0 = none; 0 is never used)
			std::uint32_t  confirmedUpTo = 0;       // the appliedLifecycle last turned into confirmations
			Net::NetworkId resumeFrom = 0;          // the first id left over by the last snapshot (0 = start)
			void Reset();
		};

		enum class OnWriteFailure : std::uint8_t { Destroy, Skip };

		/// Appends the records of `ids` (mapped, alive) to `out`. An entity whose record cannot be
		/// written is destroyed later (spawns) or only left out (snapshots).
		void BuildRecords(const World& world, std::span<const Net::NetworkId> ids, std::vector<Record>& out, OnWriteFailure onFailure);
		bool SendWorld(Net::PeerId peer, std::span<const Record> records);
		bool SendSpawnBatches(Net::PeerId peer, std::span<const Record> records);
		bool SendDespawnBatches(Net::PeerId peer, std::span<const Net::NetworkId> ids);
		bool Send(Net::PeerId peer, std::span<const std::byte> message);
		void DestroyFailed(World& world);

		ClientView& ViewOf(Net::PeerId peer) { return m_views[peer - 1u]; }
		void SyncViews();
		void RefreshConfirmations(const PeerState& peer, ClientView& view);
		void ProcessAcks(const PeerState& peer, ClientView& view);
		void SendSnapshotsTo(Net::PeerId peer, ClientView& view, Net::NetTick tick);

		NetworkSession&             m_session;
		const NetContent&           m_content;
		StepSource                  m_steps;
		std::uint32_t               m_generation = 0;   // the session's, when the views were last valid

		std::vector<Net::NetworkId> m_spawned;      // this frame's performed spawns
		std::vector<Net::NetworkId> m_despawned;    // queued by the identity hooks
		std::vector<Net::NetworkId> m_destroyed;    // unmapped since last frame: per-client cleanup
		std::vector<Net::NetworkId> m_worldIds;     // every mapped entity, in id order
		std::vector<Record>         m_worldRecords;
		std::vector<Record>         m_spawnRecords;
		std::vector<Record>         m_snapshotRecords;   // this snapshot's state, shared by every client
		std::vector<std::byte>      m_state;        // record bodies, back to back
		std::vector<Net::PeerId>    m_joining;      // peers that get the world this frame
		std::vector<Net::PeerId>    m_incremental;  // peers that get this frame's changes
		std::vector<QueuedGameplayEvent> m_events;  // this frame's gameplay events
		std::vector<std::byte>      m_eventBuffer;  // one encoded GameplayEvent
		std::vector<std::size_t>    m_candidates;   // one client's snapshot entries (indices into m_snapshotRecords)
		SpawnBatchView              m_spawnBatch;   // one message being encoded (reused)
		DespawnBatchView            m_despawnBatch;
		SnapshotView                m_snapshot;
		std::vector<std::byte>      m_message;      // one encoded message (kMaxReliableBytes)
		std::array<ClientView, Net::kMaxClients> m_views;
		struct Failed { Net::NetworkId id; Net::NetPrefabId prefab; std::string reason; };
		std::vector<Failed>         m_failed;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
