/*************************************************************************
* \file:         NetReplicationSendSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.3
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
*                Snapshots: a budget per client (Networking Guide 10.4 to
*                10.8, issues 1, 9, 11, 12, 14). Every 2 steps (30 Hz), for
*                every InGame client:
*                  - Gating: only entities whose spawn that client APPLIED
*                    (Input.appliedLifecycle, or ClientReady for the world)
*                    are candidates, so a snapshot never races its spawn.
*                  - Change detection: each record is hashed (64-bit) when it
*                    is gathered. Per client and entity the host keeps the
*                    hash of what that client holds: the spawn's record once
*                    the spawn is confirmed, then the record of the newest
*                    snapshot it ACKNOWLEDGED carrying it (the bytes sent
*                    then, not today's). An entity whose record equals that
*                    baseline costs nothing; one that changed is a candidate
*                    until a snapshot carrying the new state is acked.
*                  - Class and age: a prefab marked critical (NetPrefabDesc::
*                    critical) is class Critical; any other by its distance
*                    to the entity this client owns (its player, if any):
*                    Near (< 20 m), Mid (< 50 m), Far. Each class has a
*                    maximum update age (2, 6, 30, 120 steps). An entity's
*                    age is the steps since its previous update or since the
*                    client's copy went out of date, whichever is later.
*                  - Order: critical entities first (oldest first), but at most
*                    one message's worth (kMaxSnapshotBytes) of them per
*                    client per snapshot; then every other entity at or past
*                    its maximum age, furthest past it first; then the rest
*                    by accumulated priority: each snapshot an entity waits
*                    it gains sendPriority x a distance factor (1, 0.35,
*                    0.08); being sent resets it. Critical entities beyond
*                    that cap wait (oldest first next time), so the second
*                    message is always left to the others: under any load
*                    neither group starves the other. NetStats::
*                    maxUrgentWaitTicks measures it: steps an entity waited
*                    after it became due.
*                  - Budget: the client's allowance (bytes per snapshot,
*                    adaptive, below) spent in at most 2 messages of at most
*                    kMaxSnapshotBytes. When it is spent, urgent entities may
*                    still use one more message (budgetOverflow); the hard
*                    cap of 2 messages per client per snapshot, and the
*                    message size, hold for critical entities too. What does
*                    not fit waits for the next snapshot (recordsDeferred).
*                  - Nothing changed: one header-only message still goes out,
*                    so the client's clock estimate and its acks keep going.
*                  - Adaptive allowance (issue 14): from 1,100 bytes; if the
*                    client's lane 1 (snapshots) queues more than 50 ms in
*                    GNS it is cut by 25% (at least 300); after 2 s under
*                    10 ms it grows by 10% (at most 2,200).
*                  - A message is remembered (64-entry ring) only if GNS took
*                    it, so no ack can name a message that was not sent.
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
*    -v1.3       per-client budget: priority and maximum age by class,
*                change detection against confirmed and acknowledged
*                baselines, adaptive allowance from lane 1, instrumentation
*                (PR 13)
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
#include "Math/Math.h"
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

		// ---- the budget (see the file notes) -------------------------------------------------
		static constexpr std::size_t kMaxSnapshotMessagesPerClient = 2;          // per snapshot, a hard cap
		static constexpr std::size_t kDefaultAllowanceBytes = kMaxSnapshotBytes;  // per client per snapshot
		static constexpr std::size_t kMinAllowanceBytes = 300;
		static constexpr std::size_t kMaxAllowanceBytes = 2 * kMaxSnapshotBytes;
		static constexpr std::size_t kMinMessageBytes = 64;                      // less is not worth a message
		static constexpr std::int64_t  kShrinkQueueMicros = 50000;               // lane 1 over this: cut by 25%
		static constexpr std::int64_t  kGrowQueueMicros = 10000;                 // under this ...
		static constexpr std::uint32_t kCalmSnapshotsToGrow = 60;                // ... for 2 s: raise by 10%
		static constexpr float kNearMetres = 20.f;
		static constexpr float kMidMetres = 50.f;

		/// Update classes, each with a maximum age (simulation steps). NetStats indexes by them.
		enum class UpdateClass : std::uint8_t { Critical = 0, Near = 1, Mid = 2, Far = 3 };
		static constexpr Net::NetTick MaxAgeTicks(UpdateClass c) noexcept
		{
			switch (c)
			{
			case UpdateClass::Critical: return 2;     // every snapshot
			case UpdateClass::Near:     return 6;     // 100 ms
			case UpdateClass::Mid:      return 30;    // 0.5 s
			default:                    return 120;   // 2 s
			}
		}

		/// What the host knows about one client's copy of one entity.
		struct ClientEntityState
		{
			std::uint32_t spawnSeq = 0;       // lifecycleSeq of the SpawnBatch that carried it to this client
			bool          confirmed = false;  // the client applied that SpawnBatch: snapshots may carry it
			bool          acked = false;      // baseline is what the client holds (confirmed spawn, or an acked snapshot)
			Net::NetTick  ackedTick = 0;      // tick of the newest acknowledged snapshot carrying it
			Net::NetTick  lastSentTick = 0;   // tick of the newest snapshot sent with it
			std::uint64_t spawnHash = 0;      // hash of the record its SpawnBatch carried
			std::uint64_t baseline = 0;       // hash of the record the client holds (when acked)
			Net::NetTick  cleanTick = 0;      // newest snapshot tick at which it was up to date (or confirmed)
			Net::NetTick  dueSince = 0;       // first snapshot tick it was urgent and not yet sent (0 = not due)
			float         priority = 0.f;     // grows while it waits; reset when sent
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

		/// Diagnostics and tests: `peer`'s current allowance (bytes per snapshot), 0 if no such slot.
		std::size_t Allowance(Net::PeerId peer) const noexcept;

		/// Tests and measurements: fixes `peer`'s allowance at `bytes` (clamped to [kMinAllowanceBytes,
		/// kMaxAllowanceBytes]) until its slot is reset; 0 returns it to the adaptive rule.
		void ForceAllowance(Net::PeerId peer, std::size_t bytes) noexcept;

		/// Diagnostics (leak checks): per-client entity states held for all clients.
		std::size_t TrackedStates() const noexcept;

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
			std::uint64_t    hash = 0;              // of mask and state: change detection
			Math::vec3       position{ 0.f };       // the root's, for distance
			float            sendPriority = 1.f;    // from the prefab
			bool             critical = false;
		};

		/// One snapshot message GNS accepted, until it is acknowledged or overwritten.
		struct SentSnapshot
		{
			std::uint16_t               sequence = 0;
			Net::NetTick                tick = 0;
			bool                        valid = false;
			struct Entry { Net::NetworkId id; std::uint64_t hash; };
			std::vector<Entry>          entries;   // which entities it carried, and the state it carried
		};

		/// Everything the host keeps about one client slot.
		struct ClientView
		{
			NetConnection connection = kInvalidConnection;   // the client it belongs to
			std::unordered_map<Net::NetworkId, ClientEntityState> states;
			std::array<SentSnapshot, 64> sent{};
			std::uint16_t  lastSequence = 0;        // newest sequence sent (0 = none; 0 is never used)
			std::uint32_t  confirmedUpTo = 0;       // the appliedLifecycle last turned into confirmations
			std::size_t    allowance = kDefaultAllowanceBytes;   // bytes per snapshot (adaptive)
			std::uint32_t  calmSnapshots = 0;       // snapshots in a row with lane 1 under kGrowQueueMicros
			bool           fixedAllowance = false;  // ForceAllowance
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
		void AdaptAllowance(const PeerState& peer, ClientView& view);
		void SendSnapshotsTo(const PeerState& peer, ClientView& view, Net::NetTick tick);

		/// One candidate for one client's snapshot.
		struct Candidate
		{
			std::uint32_t      record = 0;       // index into m_snapshotRecords
			ClientEntityState* state = nullptr;
			UpdateClass        cls = UpdateClass::Near;
			Net::NetTick       age = 0;          // steps since its previous update or since it went out of date
			std::int64_t       lateness = 0;     // age - MaxAgeTicks(cls)
			bool               urgent = false;   // critical, or at or past its maximum age
		};

		/// Writes candidates from `next` into one message of at most `capacity` bytes and sends it
		/// (an empty one if nothing is left). False if GNS refused it: nothing remembered, `next`
		/// unchanged. `bytes` gets the message's size.
		bool WriteAndSend(Net::PeerId peer, ClientView& view, Net::NetTick tick, std::size_t& next, std::size_t capacity,
			bool urgentOnly, std::size_t& bytes);

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
		std::vector<Candidate>      m_candidates;   // one client's snapshot candidates, in send order
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
