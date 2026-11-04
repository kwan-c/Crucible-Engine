/*************************************************************************
* \file:         NetworkSession.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.6
* \brief
*                One multiplayer session: hosting or joining, who is
*                connected, the join handshake, refusals, receive limits
*                and quotas. Everything with session lifetime.
*
* \details
*                Core owns exactly one NetworkSession for the whole run. It
*                is Offline (no sockets, GNS not even started) until Start,
*                which Core calls when Play begins and this instance's
*                NetworkSettings say Host or Client.
*
*                Roles and states
*                  Role():  Offline | Server (the host) | Client
*                  State(): Offline -> Running -> Stopping -> Offline
*                  Host, per client slot (PeerState::Phase):
*                    Free -> Connecting (accepted, handshake running)
*                         -> AwaitingJoin (connected; JoinRequest due within
*                            joinTimeoutMs)
*                         -> Synchronizing (JoinAccepted sent; the send
*                            system streams the world the same frame;
*                            ClientReady due within kSynchronizeTimeout)
*                         -> InGame (ClientReady echoed the WorldEnd's
*                            sequence)                 -> Free when it leaves
*                  Client (ClientPhase):
*                    Connecting -> Joining (JoinRequest sent; JoinAccepted due
*                    within joinTimeoutMs, else Close(Timeout)) -> Joined
*                  Client world (ClientWorld, after joining):
*                    AwaitingWorld -> Receiving (WorldBegin) -> Ready
*                    (WorldEnd applied, ClientReady sent)
*
*                The lifecycle stream (Networking Guide 8.4, 9.3, 9.4)
*                  Every reliable lane-0 message to a client carries the
*                  next number of one per-client counter (JoinAccepted is 1).
*                  Server: SendLifecycle stamps and sends; a failed send
*                  closes the peer (SendFailed). Client: the session hands
*                  lifecycle messages to NetReceiveSystem (they need the
*                  World), which applies each whole message or leaves.
*
*                Spawning (server): RequestSpawn allocates the id and queues
*                the spawn; NetReceiveSystem performs it next frame and
*                reports it (NotifySpawned / NotifySpawnFailed); the send
*                system announces it. Destroying a mapped entity queues its
*                despawn (identity hooks).
*
*                Settings: Settings() is what the NEXT Start uses (SetSettings
*                may change it at any time). A running session works only from
*                ActiveSettings(), the copy taken when it started, so nothing
*                can change a session under its feet, and a refused Start
*                changes nothing at all.
*
*                The join handshake
*                  client                         host
*                  Connect ----- GNS handshake --> Incoming: slot free? Accept
*                                                  : else Close(ServerFull)
*                  Connected: JoinRequest ------>  version and content hash
*                                                  equal? JoinAccepted (peer id,
*                                                  tick, tick rate)
*                                                  : else Close(ProtocolMismatch)
*                  Joined <------- JoinAccepted
*                Refusals are GNS close reasons, so the client learns why
*                even though no message of ours reaches it.
*
*                Defensive limits (Networking Guide 8.7, issue 10)
*                  - At most 256 messages and 256 KB processed per Update,
*                    counted message by message. A message that would cross
*                    the byte budget waits, in order, for the next frame
*                    (always first in that frame, so one large message can
*                    never stall the queue). Messages already taken from GNS
*                    wait in a carry-over queue of at most one receive batch
*                    (64 messages of at most 16 KB each, so at most 1 MB);
*                    while it is not empty nothing new is taken from GNS, which
*                    keeps the rest queued.
*                  - The 256-message limit counts every message taken from
*                    GNS, valid or not, so no kind of traffic can keep one
*                    frame receiving.
*                  - A message bigger than the largest valid protocol message
*                    (16 KB) is malformed: never read, released at once, never
*                    carried. It uses one of the frame's 256 messages and one
*                    of its sender's 64, but not the 256 KB decode budget (its
*                    bytes are never decoded; billing them would let one huge
*                    message a frame starve everyone else's valid traffic).
*                  - At most 64 messages per client per frame, oversized ones
*                    included; a client over it for 30 frames in a row is
*                    closed (Malformed).
*                  - Every message: size within MaxMessageBytes, lane 0, a
*                    type the client may send. More than 50 bad messages from
*                    one client closes it.
*
*                Networked entities (Networking Guide 9.2, issues 8, 9)
*                  Entities() maps each NetworkId to its local entity. Only
*                  the NetworkIdentity hooks write it (NetIdentityHooks.h),
*                  so every add, remove and destroy path keeps it right:
*                  - an identity with no id, or with an id that already
*                    names another live entity, is rejected: logged,
*                    counted, and destroyed by NetReceiveSystem next frame;
*                  - only removing a mapping the map held counts. Its id
*                    goes to DestroyedThisFrame() (per-client cleanup) and,
*                    on a running server, to PendingDespawns(). A rejected
*                    duplicate dying never despawns the original, and
*                    nothing is announced while Stopping or Offline.
*                  Start and Stop clear all of it. Core stops the session
*                  before Stop restores the world (PlayMode::OnBeforeStop),
*                  so tearing down the play world announces nothing.
*
*                Threading and call rules: main thread only. Start and Stop
*                at a safe point (Core calls them from the PlayStateChanged
*                listener), never from inside Update; code running inside
*                Update calls RequestStop, which the next Update performs.
*
* \history
*    -v1.6       snapshots: SendSnapshot, SendInput, the Input handler
*                (appliedLifecycle and snapshot acks, validated) (PR 9)
*    -v1.5       spawn requests, the lifecycle stream (send, apply, world
*                state), ClientReady and the Synchronizing timeout
*                (Networking Guide 9.3, 18.3 issues 3, 4, 13, PR 8)
*    -v1.4       SetContentError: invalid network content refuses Start
*                (Networking Guide PR 7)
*    -v1.3       identity map and hooks: rejection, despawn ownership,
*                per-client cleanup ids (Networking Guide PR 6); oversized
*                messages count towards the frame limit and peer quota
*    -v1.2       LastStartError and LastEndReason for the editor's panel
*    -v1.1       review fixes: a refused Start changes nothing (validated
*                first; the running session uses ActiveSettings()); FreeSlot
*                guarded; per-message receive budget with a bounded carry-
*                over queue; client JoinAccepted deadline
*    -v1.0       Networking Guide 5.4, 8.7, 18.3 issues 10 and 13, PR 4:
*                settings-driven Start, connection handling, join handshake
*                with version/hash/full refusals, quotas, timeouts, events
*
**************************************************************************/
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "EngineExport.h"
#include "ECS/Core/ECSCore.h"
#include "Network/GnsTransport.h"
#include "Network/NetClock.h"
#include "Network/NetComponents.h"
#include "Network/NetEntityMap.h"
#include "Network/NetStats.h"
#include "Network/NetTypes.h"
#include "Network/NetworkSettings.h"
#include "Network/Protocol.h"

namespace Crucible
{
	struct EventBuses;

	enum class SessionState : std::uint8_t { Offline, Running, Stopping };

	/// A client's progress through the join handshake.
	enum class ClientPhase : std::uint8_t { None, Connecting, Joining, Joined };

	/// One client slot on the host. Slot i holds PeerId i + 1 (0 is the host itself).
	struct PeerState
	{
		enum class Phase : std::uint8_t { Free, Connecting, AwaitingJoin, Synchronizing, InGame };

		Phase         phase = Phase::Free;
		NetConnection connection = kInvalidConnection;
		Net::PeerId   peer = Net::kInvalidPeer;
		std::string   name;                    // from JoinRequest
		std::string   remoteAddress;           // "ip:port", for logs
		std::chrono::steady_clock::time_point joinDeadline{};   // AwaitingJoin: JoinRequest due by then
		std::uint32_t messagesThisFrame = 0;   // quota
		std::uint32_t quotaStrikes = 0;        // consecutive frames over the quota
		std::uint32_t malformed = 0;
		std::uint32_t nextLifecycleSeq = kFirstLifecycleSeq;   // stamped on the next lane-0 message
		std::uint32_t appliedLifecycle = 0;                    // reported by the client (snapshots, Phase 9)
		bool          needsWorld = false;   // Synchronizing: the world stream has not been sent yet
		std::uint32_t worldEndSeq = 0;      // the WorldEnd's lifecycleSeq; ClientReady must echo it
		std::chrono::steady_clock::time_point syncDeadline{};   // Synchronizing: ClientReady due by then

		// Snapshots (sequence 0 is never used: an ack of 0 means "none received yet")
		bool          snapshotSent = false;        // lastSentSnapshot is meaningful
		std::uint16_t lastSentSnapshot = 0;        // newest sequence GNS accepted: acks beyond it are dropped
		bool          hasAck = false;              // newestAckedSnapshot is meaningful
		std::uint16_t newestAckedSnapshot = 0;     // from Input: newest snapshot the client applied
		std::uint32_t ackBits = 0;                 // bit i: (newestAckedSnapshot - 1 - i) applied too

		bool IsJoined() const noexcept { return phase == Phase::Synchronizing || phase == Phase::InGame; }
	};

	/// Server: a spawn requested this frame, performed by NetReceiveSystem at the start of the
	/// next frame (structural changes are not allowed inside declared gameplay systems).
	struct PendingSpawn
	{
		Net::NetworkId   id = Net::kInvalidNetworkId;
		Net::NetPrefabId prefab = Net::kInvalidNetPrefab;
		Net::PeerId      owner = Net::kServerPeer;
		Math::vec3       position{ 0.f };
		Math::quat       rotation{ 1.f, 0.f, 0.f, 0.f };
	};

	/// Client: where it is in the host's lifecycle stream (Networking Guide 9.3).
	enum class ClientWorld : std::uint8_t
	{
		AwaitingWorld,   // joined; only WorldBegin may come next
		Receiving,       // after WorldBegin: SpawnBatch messages, then WorldEnd
		Ready,           // after WorldEnd (ClientReady sent): spawns, despawns, events
	};

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members of an exported class: used only inside Engine.dll
#endif

	class ENGINE_API NetworkSession
	{
	public:
		static constexpr std::size_t   kMaxMessagesPerFrame        = 256;
		static constexpr std::size_t   kMaxBytesPerFrame           = 256 * 1024;
		static constexpr std::uint32_t kMaxMessagesPerPeerPerFrame = 64;
		static constexpr std::uint32_t kMaxQuotaStrikes            = 30;
		static constexpr std::uint32_t kMaxMalformedPerPeer        = 50;

		/// `events` must outlive the session; connection events are posted to events.network.
		explicit NetworkSession(EventBuses& events);
		~NetworkSession();   // Stop("session destroyed")
		NetworkSession(const NetworkSession&) = delete;
		NetworkSession& operator=(const NetworkSession&) = delete;

		// ---- configuration ------------------------------------------------------------------

		/// What the next Start uses. Safe at any time: a running session never reads these, it
		/// works from ActiveSettings() (the copy taken when it started).
		void SetSettings(const NetworkSettings& settings) { m_settings = settings; }
		const NetworkSettings& Settings() const noexcept { return m_settings; }

		/// The settings the running session started with (meaningless while Offline).
		const NetworkSettings& ActiveSettings() const noexcept { return m_active; }

		/// The hash both sides must agree on (JoinRequest). Default: DefaultContentHash(); Core
		/// sets NetContent's full hash (serializers and network prefabs) before every Start.
		void          SetContentHash(std::uint64_t hash) noexcept { m_contentHash = hash; }
		std::uint64_t ContentHash() const noexcept { return m_contentHash; }

		/// Why the network content (prefabs, serializers) is unusable, or empty when it is fine.
		/// While set, Start refuses Host and Client with this reason (Offline still starts).
		void               SetContentError(std::string error) { m_contentError = std::move(error); }
		const std::string& ContentError() const noexcept { return m_contentError; }

		/// Protocol version, tick rate and snapshot interval, canonically hashed.
		static std::uint64_t DefaultContentHash() noexcept;

		// ---- lifecycle ----------------------------------------------------------------------

		/// Starts what `settings` ask for and, on success, makes them Settings(). Offline: returns
		/// true and stays Offline. Fails with a reason on a running session, invalid settings or
		/// a busy port; a failed Start changes NOTHING (session, settings, connections).
		bool Start(const NetworkSettings& settings, std::string& error);
		bool Start(std::string& error) { return Start(m_settings, error); }

		/// Ends the session now: every connection closed without linger (clients receive the
		/// reason in the close; see Stop in the .cpp for why not linger), every slot freed, role
		/// Offline, Generation() bumped, SessionEnded posted.
		/// Safe to call when already Offline. Never call from inside Update().
		void Stop(std::string_view reason) noexcept;

		/// Stop at the start of the next Update() (for code running inside Update).
		void RequestStop(std::string_view reason);

		/// One frame of networking (NetReceiveSystem calls it in PreUpdate, every frame, also
		/// while paused): connection events, bounded receive, handshake, quotas, timeouts.
		void Update();

		// ---- queries ------------------------------------------------------------------------

		Net::NetRole  Role() const noexcept         { return m_role; }
		SessionState  State() const noexcept        { return m_state; }
		bool          IsActive() const noexcept     { return m_role != Net::NetRole::Offline; }
		bool          IsServer() const noexcept     { return m_role == Net::NetRole::Server; }
		bool          IsClient() const noexcept     { return m_role == Net::NetRole::Client; }
		bool          IsAuthoritative() const noexcept { return m_role != Net::NetRole::Client; }   // offline counts
		bool          IsStopRequested() const noexcept { return m_stopRequested; }

		/// Bumped by every Start and every Stop: lets later code notice "a different session".
		std::uint32_t Generation() const noexcept   { return m_generation; }

		Net::PeerId   LocalPeer() const noexcept    { return m_localPeer; }   // host 0; client after joining
		ClientPhase   ClientState() const noexcept  { return m_clientPhase; }
		bool          IsJoined() const noexcept     { return m_clientPhase == ClientPhase::Joined; }
		NetConnection ServerConnection() const noexcept { return m_serverConnection; }   // client
		Net::NetTick  ServerTickAtJoin() const noexcept { return m_serverTickAtJoin; }   // client

		std::span<const PeerState> Peers() const noexcept { return m_peers; }   // host: kMaxClients slots
		std::size_t   OccupiedSlots() const noexcept;   // host: slots not Free
		std::size_t   JoinedPeers() const noexcept;     // host: slots past the handshake

		const NetStats& Stats() const noexcept { return m_stats; }
		NetClock&       Clock() noexcept       { return m_clock; }

		/// The transport while a session runs, otherwise null (diagnostics).
		const GnsTransport* Transport() const noexcept { return m_transport.get(); }

		/// Why the latest Start failed (empty after a successful one): for the editor's panel.
		const std::string& LastStartError() const noexcept { return m_lastStartError; }

		/// Why the latest session ended (kept until the next one ends): for the editor's panel.
		const std::string& LastEndReason() const noexcept { return m_lastEndReason; }

		// ---- networked entities -------------------------------------------------------------

		/// NetworkId -> local entity for the running session (empty while Offline).
		const NetEntityMap& Entities() const noexcept { return m_entities; }

		/// The NetworkIdentity add hook (registered by RegisterIdentityHooks). While Running: maps
		/// identity.id -> entity, or rejects the entity when the id is invalid or already names
		/// another entity that isAlive says is alive. A rejected entity is logged, counted and
		/// queued in Rejected(). Offline or Stopping: ignored.
		void OnIdentityAdded(Entity entity, const NetworkIdentity& identity, const std::function<bool(Entity)>& isAlive);

		/// The NetworkIdentity remove hook (fires on every remove and destroy path). Acts only if
		/// the map held exactly (identity.id -> entity): unmaps it, adds the id to
		/// DestroyedThisFrame() and, on a running server, to PendingDespawns().
		void OnIdentityRemoved(Entity entity, const NetworkIdentity& identity);

		/// Entities whose identity was rejected, waiting for NetReceiveSystem to destroy them (a
		/// hook may not change the World's structure). TakeRejected moves them into `out`.
		std::span<const Entity> Rejected() const noexcept { return m_rejected; }
		void TakeRejected(std::vector<Entity>& out);

		/// Server: ids whose entity was destroyed while the session ran. The send system (Phase 8)
		/// takes them each frame and broadcasts them as despawns.
		std::span<const Net::NetworkId> PendingDespawns() const noexcept { return m_pendingDespawns; }
		void TakePendingDespawns(std::vector<Net::NetworkId>& out);

		/// Every role: ids unmapped since they were last taken, so per-client replication state
		/// for them can be dropped (issue 9). The send system (Phase 8) takes them each frame.
		std::span<const Net::NetworkId> DestroyedThisFrame() const noexcept { return m_destroyedThisFrame; }
		void TakeDestroyedThisFrame(std::vector<Net::NetworkId>& out);

		// ---- spawning (server) --------------------------------------------------------------

		/// Which ids are network prefabs (Core asks its NetContent). A function, so the session
		/// itself stays free of the ECS and prefab code (the standalone session tests link it alone).
		/// Unset: nothing can be spawned.
		void SetPrefabCheck(std::function<bool(Net::NetPrefabId)> isNetworkPrefab) { m_isNetworkPrefab = std::move(isNetworkPrefab); }

		/// Server, while Running: allocates an id now (never reused this session) and queues the
		/// spawn; NetReceiveSystem performs it at the start of the next frame and the send system
		/// then announces it. kInvalidNetworkId if this is not a running server, the prefab is not
		/// a network prefab, or owner is not 0..kMaxClients. Spawning never needs anything else:
		/// destroy the entity normally to despawn it.
		Net::NetworkId RequestSpawn(Net::NetPrefabId prefab, Net::PeerId owner, const Math::vec3& position,
			const Math::quat& rotation = Math::quat(1.f, 0.f, 0.f, 0.f));

		std::span<const PendingSpawn> PendingSpawns() const noexcept { return m_pendingSpawns; }
		void TakePendingSpawns(std::vector<PendingSpawn>& out);

		/// NetReceiveSystem: the spawn of `id` was performed (its entity is mapped). The send system
		/// takes these with TakeSpawned and announces them.
		void NotifySpawned(Net::NetworkId id);
		void TakeSpawned(std::vector<Net::NetworkId>& out);

		/// A spawn was not performed, or its record could not be written: logged, counted, and
		/// posted as NetworkEvents::SpawnFailed.
		void NotifySpawnFailed(Net::NetworkId id, Net::NetPrefabId prefab, std::string reason);

		// ---- the lifecycle stream (server) ----------------------------------------------------

		/// The lifecycleSeq the next lane-0 message to `peer` must carry; 0 if it is not joined.
		std::uint32_t NextLifecycleSeq(Net::PeerId peer) const noexcept;

		/// Sends one encoded lifecycle message (WorldBegin, SpawnBatch, DespawnBatch, WorldEnd,
		/// GameplayEvent) reliably on lane 0. Its lifecycleSeq must be NextLifecycleSeq(peer),
		/// which then moves on. If GNS does not take it the peer is closed with SendFailed, so a
		/// client can never miss a message and carry on. False if not sent (peer gone or closed,
		/// or a message that is not the next one).
		bool SendLifecycle(Net::PeerId peer, std::span<const std::byte> message);

		/// The send system streamed the world to `peer`, ending with WorldEnd `worldEndSeq`.
		void MarkWorldSent(Net::PeerId peer, std::uint32_t worldEndSeq);

		/// Closes a joined or joining peer (for example after an encoding bug).
		void DropPeer(Net::PeerId peer, CloseReason reason, const std::string& debug);

		/// How long a peer may stay Synchronizing before it is closed with Timeout.
		static constexpr std::chrono::milliseconds kSynchronizeTimeout{ 10000 };

		// ---- snapshots and acknowledgements -----------------------------------------------------

		/// Server: sends one encoded Snapshot (its own `sequence`, never 0) to an InGame peer,
		/// unreliably on lane 1. On Ok the sequence becomes the newest one an ack may name. A
		/// failure is only counted: the next snapshot supersedes it, and a snapshot that was not
		/// sent must never be remembered as sent.
		SendResult SendSnapshot(Net::PeerId peer, std::span<const std::byte> message, std::uint16_t sequence);

		/// Client: sends an Input message (heartbeat, snapshot acks, appliedLifecycle; commands
		/// arrive in a later phase) unreliably on lane 0, now or never. False if not sent.
		bool SendInput(const InputMessage& message);

		// ---- the lifecycle stream (client) ----------------------------------------------------

		/// Newest lifecycle message fully applied (1 after JoinAccepted).
		std::uint32_t AppliedLifecycle() const noexcept { return m_appliedLifecycle; }
		ClientWorld   WorldState() const noexcept { return m_clientWorld; }
		void          SetWorldState(ClientWorld state) noexcept { m_clientWorld = state; }

		/// After a lifecycle message was applied completely. seq must be AppliedLifecycle() + 1.
		void MarkLifecycleApplied(std::uint32_t seq) noexcept;

		/// Lifecycle (and snapshot) messages from the host, in arrival order, for NetReceiveSystem
		/// to apply right after Update() in the same frame (they need the World). Their buffers
		/// go back to GNS when `out` is cleared, so clear it before the frame ends.
		void TakeWorldMessages(std::vector<ReceivedMessage>& out);

		/// The world arrived (WorldEnd `worldEndSeq` applied): sends ClientReady and posts
		/// WorldReady. False (and leaving) if ClientReady could not be sent.
		bool CompleteWorld(std::uint32_t worldEndSeq, std::size_t entities);

		/// Client: closes the connection with `reason` (the host sees it) and stops next Update.
		void CloseAndStop(CloseReason reason, const std::string& debug);

		/// Counters, for the network systems to add to.
		NetStats& MutableStats() noexcept { return m_stats; }

	private:
		bool TryStart(const NetworkSettings& settings, std::string& error);
		void HandleServerEvent(const ConnectionEvent& event);
		void HandleClientEvent(const ConnectionEvent& event);
		void HandleServerMessage(const ReceivedMessage& message);
		void HandleClientMessage(ReceivedMessage& message);
		void HandleJoinRequest(PeerState& peer, ByteReader& reader);
		void HandleClientReady(PeerState& peer, ByteReader& reader);
		void HandleInput(PeerState& peer, ByteReader& reader);
		void CollectFrameMessages();
		void RejectOversized(const ReceivedMessage& message);
		void EnforceQuotas();
		void EnforceJoinTimeouts(std::chrono::steady_clock::time_point now);

		PeerState* FreeSlot() noexcept;
		PeerState* PeerByConnection(NetConnection connection) noexcept;
		PeerState* PeerById(Net::PeerId peer) noexcept;   // a slot that is not Free, or null
		void       Malformed(PeerState& peer, const char* what);
		void       ClosePeer(PeerState& peer, CloseReason reason, const std::string& debug);
		void       ClearEntityState() noexcept;   // map, rejected, spawns, despawns, ids, world messages
		void       Post(auto event);

		template <typename Msg>
		SendResult SendReliable(NetConnection connection, const Msg& message);

		EventBuses&                    m_events;
		NetworkSettings                m_settings;    // for the next Start
		NetworkSettings                m_active;      // the running session's own copy
		std::uint64_t                  m_contentHash;
		std::string                    m_contentError;   // non-empty: Host/Client may not start
		std::unique_ptr<GnsTransport>  m_transport;   // null while Offline

		Net::NetRole                   m_role = Net::NetRole::Offline;
		SessionState                   m_state = SessionState::Offline;
		std::uint32_t                  m_generation = 0;
		bool                           m_stopRequested = false;
		std::string                    m_stopReason;
		std::string                    m_lastStartError;
		std::string                    m_lastEndReason;

		// Client
		ClientPhase                    m_clientPhase = ClientPhase::None;
		NetConnection                  m_serverConnection = kInvalidConnection;
		Net::PeerId                    m_localPeer = Net::kInvalidPeer;
		std::uint32_t                  m_appliedLifecycle = 0;
		ClientWorld                    m_clientWorld = ClientWorld::AwaitingWorld;
		std::vector<ReceivedMessage>   m_worldMessages;   // for NetReceiveSystem, this frame only
		Net::NetTick                   m_serverTickAtJoin = 0;
		std::chrono::steady_clock::time_point m_joinDeadline{};   // Joining: JoinAccepted due by then

		// Host
		std::array<PeerState, Net::kMaxClients> m_peers{};

		NetClock                       m_clock;
		NetStats                       m_stats;
		std::vector<ConnectionEvent>   m_frameEvents;   // reused every frame
		std::vector<ReceivedMessage>   m_messages;      // this frame's messages; released at the end of it
		std::vector<ReceivedMessage>   m_batch;         // one receive from GNS, before it is sorted
		std::deque<ReceivedMessage>    m_carry;         // taken from GNS but over this frame's budget: first next frame

		// Networked entities (written only by the identity hooks)
		NetEntityMap                   m_entities;
		std::vector<Entity>            m_rejected;            // destroyed by NetReceiveSystem next frame
		std::vector<Net::NetworkId>    m_pendingDespawns;     // server, while Running
		std::vector<Net::NetworkId>    m_destroyedThisFrame;  // every role: per-client cleanup

		// Spawning (server)
		std::function<bool(Net::NetPrefabId)> m_isNetworkPrefab;
		Net::NetworkId                 m_nextId = 1;          // never reused within a session
		std::vector<PendingSpawn>      m_pendingSpawns;       // performed by NetReceiveSystem next frame
		std::vector<Net::NetworkId>    m_spawned;             // performed, for the send system to announce
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
