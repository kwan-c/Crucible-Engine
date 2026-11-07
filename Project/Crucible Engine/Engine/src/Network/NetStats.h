/*************************************************************************
* \file:         NetStats.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.11
* \brief
*                Counters the networking code measures itself, per session
*                (reset by every Start).
*
* \details
*                GNS measures the transport (ping, quality, rates, queues:
*                GnsTransport::Status / LaneStatus). These count what only
*                Crucible knows: what it received, what it rejected and why,
*                and how long its own receive work took. A steady rise in a
*                rejection counter means a bug or a misbehaving peer.
*
* \history
*    -v1.0       connection, receive and rejection counters (Phase 4)
*    -v1.1       carriedOver and oversized (receive budget review fix)
*    -v1.2       identityRejected, despawnsQueued, lastFrameOversized (Phase 6)
*    -v1.3       spawn, despawn, world stream and proxy counters (Phase 8)
*    -v1.4       snapshot, ack and input counters (Phase 9)
*    -v1.5       command, input timeout and player spawn counters (PR 11)
*    -v1.6       gameplay event, interpolation and input latency counters
*                (PR 12)
*    -v1.7       snapshot budget, change detection, update ages, bytes by
*                message type, replication CPU time (PR 13)
*    -v1.8       kMessageTypes 12: SceneObjectBatch (Networking Phase 14.4)
*    -v1.9       scene object counters (Networking Phase 14.5)
*    -v1.10      scenePlayersPlaced (Networking Phase 14.6)
*    -v1.11      sceneProfiles (Networking Phase 14.7)
*
**************************************************************************/
#pragma once

#include <array>
#include <cstdint>

namespace Crucible
{
	struct NetStats
	{
		// Receiving
		std::uint64_t messagesReceived = 0;
		std::uint64_t bytesReceived = 0;
		std::uint32_t lastFrameMessages = 0;   // processed in the latest frame (budget: 256)
		std::uint32_t lastFrameBytes = 0;      // processed in the latest frame (budget: 256 KB)
		std::uint32_t carriedOver = 0;         // received but over the budget: first in the next frame
		std::uint64_t oversized = 0;           // bigger than any protocol message: rejected unprocessed
		std::uint32_t lastFrameOversized = 0;  // of those, taken in the latest frame (with lastFrameMessages: at most 256)
		std::int64_t  receiveMicros = 0;       // time spent in NetworkSession::Update, latest frame

		// Rejected input
		std::uint64_t malformed = 0;           // undecodable, wrong size, wrong lane, wrong direction
		std::uint64_t unexpected = 0;          // valid, but not allowed in the sender's current phase
		std::uint64_t unhandled = 0;           // valid, handled by a later phase (spawns, snapshots, input)
		std::uint64_t quotaDropped = 0;        // over a peer's per-frame message quota
		std::uint64_t unknownSender = 0;       // from a connection that is no longer a peer

		// Connections (server)
		std::uint32_t accepted = 0;            // connections accepted at the transport level
		std::uint32_t joined = 0;              // JoinRequests accepted
		std::uint32_t refusedFull = 0;
		std::uint32_t refusedProtocol = 0;     // protocol version or content hash differs
		std::uint32_t joinTimeouts = 0;
		std::uint32_t closedMalformed = 0;     // peers closed for malformed traffic or flooding
		std::uint32_t peersLeft = 0;

		// Sending
		std::uint32_t sendFailed = 0;

		// Networked entities
		std::uint32_t identityRejected = 0;    // NetworkIdentity with no id, or an id already in use
		std::uint32_t despawnsQueued = 0;      // server: mapped entities destroyed while running

		// Spawning and the lifecycle stream
		std::uint32_t spawnsPerformed = 0;     // server: requested spawns that now exist
		std::uint32_t spawnsFailed = 0;        // server: spawns not performed or not writable (SpawnFailed event)
		std::uint32_t spawnRecordsSent = 0;    // server: spawn records sent, all clients, world batches included
		std::uint32_t despawnIdsSent = 0;      // server: despawn ids sent, all clients
		std::uint32_t lifecycleMessagesSent = 0;   // server: reliable lane-0 messages (JoinAccepted included)
		std::uint32_t worldsSent = 0;          // server: WorldBegin..WorldEnd streams
		std::uint32_t peersReady = 0;          // server: ClientReady accepted (peer InGame)
		std::uint32_t syncTimeouts = 0;        // server: Synchronizing for too long
		std::uint32_t lifecycleApplied = 0;    // client: lifecycle messages applied
		std::uint32_t proxiesCreated = 0;      // client
		std::uint32_t proxiesDestroyed = 0;    // client: by a DespawnBatch
		std::uint32_t duplicateSpawns = 0;     // client: spawn for an id it already has (ignored)
		std::uint32_t unknownDespawns = 0;     // client: despawn for an id it does not have (ignored)

		// Snapshots and acknowledgements
		std::uint64_t snapshotsSent = 0;       // server: snapshot messages GNS accepted, all clients
		std::uint64_t snapshotBytesSent = 0;
		std::uint64_t snapshotRecordsSent = 0;
		std::uint32_t snapshotSendFailed = 0;  // server: not accepted by GNS (never remembered, never acked)
		std::uint64_t recordsDeferred = 0;     // server: changed, but left for a later snapshot by the budget
		std::uint32_t recordWriteFailed = 0;   // server: an entity's record could not be written (left out)
		std::uint64_t snapshotsAcked = 0;      // server: sent snapshots a client acknowledged
		std::uint64_t inputsReceived = 0;      // server: Input messages from joined peers
		std::uint32_t acksDropped = 0;         // server: an ack for a snapshot never sent
		std::uint64_t inputsSent = 0;          // client
		std::uint32_t inputSendFailed = 0;     // client
		std::uint64_t snapshotsApplied = 0;    // client: validated, applied and acknowledged
		std::uint32_t snapshotsRejected = 0;   // client: a record failed validation: nothing applied, not acked
		std::uint64_t staleRecords = 0;        // client: a record no newer than what the entity has
		std::uint64_t unknownEntity = 0;       // client: a record for an id it does not have (despawned since)
		std::uint32_t lastSnapshotTick = 0;    // client: server tick of the newest snapshot applied

		// Player input (Networking Guide 6.2)
		std::uint64_t commandsAccepted = 0;    // server: new commands taken (newest wins)
		std::uint64_t commandsDuplicate = 0;   // server: copies (redundancy) or older ones, skipped
		std::uint32_t inputTimeouts = 0;       // server: a peer's commands stopped for kInputTimeoutTicks
		std::uint64_t commandsSent = 0;        // client: commands built (each goes out in up to 3 packets)
		std::uint32_t playersSpawned = 0;      // server: player prefabs NetInputSystem requested
		std::uint32_t playersRemoved = 0;      // server: players of clients that left, destroyed

		// Gameplay events (Networking Guide 9.5)
		std::uint64_t gameplayEventsQueued = 0;     // server: events for clients (one per SendGameplayEvent)
		std::uint64_t gameplayEventsSent = 0;       // server: event messages sent, all clients
		std::uint32_t gameplayEventsRefused = 0;    // SendGameplayEvent said no (see its rules)
		std::uint32_t gameplayEventsSkipped = 0;    // server: not sent to a client receiving the world that frame
		std::uint64_t gameplayEventsReceived = 0;   // client

		// Interpolation and responsiveness (client, Networking Guide 11.4, 11.7)
		std::uint64_t interpolationSamples = 0;     // snapshot states buffered instead of written
		std::uint32_t interpolationTeleports = 0;   // a jump of more than 5 m: snapped, not slid
		std::uint32_t inputLatencySamples = 0;      // key-to-motion measurements
		std::uint32_t lastInputLatencyMs = 0;
		std::uint32_t minInputLatencyMs = 0;
		std::uint32_t maxInputLatencyMs = 0;
		std::uint64_t totalInputLatencyMs = 0;      // / inputLatencySamples = the average
		std::uint32_t inputLatencyAbandoned = 0;    // no visible motion within 2 s (blocked, or no player)

		// The snapshot budget (server, Networking Guide 10.4 to 10.8, issues 11 and 14)
		std::uint64_t recordsUnchanged = 0;         // the client already holds this state: not sent (0 bytes)
		std::uint32_t budgetOverflow = 0;           // a message beyond the allowance, for urgent entities only
		std::uint32_t urgentDeferred = 0;           // critical or overdue, but even the hard cap was full
		std::uint32_t maxUrgentWaitTicks = 0;       // the longest an entity waited after becoming due (0: sent at once)
		std::uint64_t emptySnapshots = 0;           // nothing changed: a header-only message (time and acks keep flowing)
		std::uint32_t lastSnapshotBytes = 0;        // the latest snapshot message, bytes
		std::uint32_t maxSnapshotBytes = 0;         // the largest so far (never above kMaxSnapshotBytes)
		std::uint32_t allowanceShrinks = 0;         // lane 1 queued over 50 ms: a client's allowance cut by 25%
		std::uint32_t allowanceGrows = 0;           // lane 1 under 10 ms for 2 s: raised by 10%

		/// Update age per class (index = NetReplicationSendSystem::UpdateClass: critical, near,
		/// mid, far): simulation steps between a client's copy going out of date (or its previous
		/// update) and the snapshot that updated it. Bucket i counts ages of i steps; the last
		/// bucket counts every age of kAgeBuckets - 1 or more.
		static constexpr std::size_t kUpdateClasses = 4;
		static constexpr std::size_t kAgeBuckets = 256;
		std::array<std::array<std::uint32_t, kAgeBuckets>, kUpdateClasses> updateAges{};
		std::array<std::uint32_t, kUpdateClasses> maxUpdateAge{};

		// Bytes of our messages by MessageType (index = the type's value), both directions; GNS's
		// own headers, encryption and acks are not included (GnsTransport::Status has those).
		static constexpr std::size_t kMessageTypes = 12;   // MessageType::Count (checked in NetworkSession.cpp)
		std::array<std::uint64_t, kMessageTypes> bytesSentByType{};
		std::array<std::uint64_t, kMessageTypes> bytesReceivedByType{};

		// Replication CPU time, microseconds, the latest frame (and the largest)
		std::int64_t gatherMicros = 0;              // server: gathering and hashing records for a snapshot
		std::int64_t sendMicros = 0;                // server: the whole send system, lifecycle and snapshots
		std::int64_t maxSendMicros = 0;
		std::int64_t interpolateMicros = 0;         // client: NetInterpolationSystem

		// Scene objects (Networking Phase 14.5)
		std::uint32_t sceneObjectsAdopted = 0;      // host: scene entities that became network objects
		std::uint32_t sceneObjectsBound = 0;        // client: own scene entities bound to the host's
		std::uint32_t sceneObjectsRemoved = 0;      // client: own scene objects the host had destroyed before the join
		std::uint32_t sceneObjectsSkipped = 0;      // NetworkObjectAuthoring left as a local entity (see the log)
		std::uint32_t sceneMismatches = 0;          // client: refused, its scene is not the host's
		std::uint32_t scenePlayersPlaced = 0;       // placed players removed as spawn points (Phase 14.6)
		std::uint32_t sceneProfiles = 0;            // scene profiles registered (Phase 14.7)
	};
}
