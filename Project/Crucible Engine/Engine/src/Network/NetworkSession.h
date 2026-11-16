/*************************************************************************
* \file:         NetworkSession.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.13
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
*                The lifecycle stream
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
*                Defensive limits
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
*                Networked entities
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
*                Player input (server): a
*                client's Input carries up to 3 commands (each one is sent
*                in 3 packets in a row). Only an InGame peer's commands
*                count; each is taken once, by sequence (copies and old ones
*                are skipped), the newest wins, and a jump in any of them is
*                latched until TakePeerIntent hands it out - once. The
*                commands reach only entities that peer owns: the peer comes
*                from the connection, never from the packet. If no new
*                command arrives for kInputTimeoutTicks simulation steps the
*                intent goes neutral, so a frozen or cut-off client's mech
*                stops instead of walking until GNS times it out.
*
*                Gameplay events: the host
*                sends a generic event - a game-defined kind and payload
*                bytes - with SendGameplayEvent. The send system delivers it
*                reliably on lane 0 after that frame's spawns and despawns,
*                to every client in the game or synchronizing (not one that
*                receives the world this frame: its world is already newer),
*                or to one client. The host posts it to itself as well, and
*                offline it is only posted locally, so gameplay listening on
*                NetworkEvents::GameplayEventReceived works in every role.
*                Clients cannot send events.
*
*                Scene objects (Networking Phase 14.5): network objects the
*                loaded scene already holds on every machine. The host
*                adopts them when its session starts (AdoptSceneObject: an
*                id, remembered with the entity's persistent scene id); the
*                world stream binds them on each client instead of spawning
*                them. SceneAsset()/SceneHash() name the loaded scene.
*
*                Players (Networking C# Phase 1): Roster() is everyone in
*                the session as this machine knows it, by PeerId - the host
*                builds it from its slots; a client gets it from the host
*                (PeerRoster, sent to every joined client whenever it
*                changes, in the lifecycle stream) and posts PeerJoined,
*                PeerReady and PeerLeft from the differences, so gameplay
*                hears the same events in every role.
*
*                Commands (Networking C# Phase 2): the one way gameplay on
*                a client asks the host for something. SendCommand sends a
*                game-defined kind and payload, optionally aimed at one
*                network object, reliably and in order on lane 0. The host
*                accepts it only from a peer in the game, in sequence, and
*                posts NetworkEvents::CommandReceived with the sender taken
*                from the connection. On the host itself and offline the
*                same call posts the same event locally, so one script
*                serves every role. A command counts against its sender's
*                64-messages-a-frame quota like everything else.
*
*                Client time: ServerTime() is
*                the estimate interpolation renders behind; the latency
*                probe measures key-down to the first rendered frame in
*                which the own player moved.
*
* \history
*    -v1.8       gameplay events (SendGameplayEvent, TakeGameplayEvents,
*                DeliverGameplayEvent), ServerTime(), the input latency
*                probe; review fix: the probe is cancelled with its
*                entity and expires on its own
*    -v1.7       client commands: per-peer command state, TakePeerIntent and
*                the input timeout; the client's SnapshotAcks()
*    -v1.6       snapshots: SendSnapshot, SendInput, the Input handler
*                (appliedLifecycle and snapshot acks, validated)
*    -v1.5       spawn requests, the lifecycle stream (send, apply, world
*                state), ClientReady and the Synchronizing timeout
*    -v1.4       SetContentError: invalid network content refuses Start
*    -v1.3       identity map and hooks: rejection, despawn ownership,
*                per-client cleanup ids; oversized
*                messages count towards the frame limit and peer quota
*    -v1.2       LastStartError and LastEndReason for the editor's panel
*    -v1.1       review fixes: a refused Start changes nothing (validated
*                first; the running session uses ActiveSettings()); FreeSlot
*                guarded; per-message receive budget with a bounded carry-
*                over queue; client JoinAccepted deadline
*    -v1.0       settings-driven Start, connection handling, join handshake
*                with version/hash/full refusals, quotas, timeouts, events
*    -v1.9       scene objects: AdoptSceneObject, SceneIdOf, the scene
*                identity (Networking Phase 14.5)
*    -v1.10      PlayerSpawnPoints (Networking Phase 14.6)
*    -v1.11      network simulation applied while running (IsSimulating);
*                PeerLinkStatus and HostLinkStatus for the editor's
*                statistics (Networking Phase 0)
*    -v1.12      the roster: Roster, FindPlayer, PlayerCount, PeerRoster
*                sent by the host and applied by clients (Networking C#
*                Phase 1); commands: SendCommand and the host's handler
*                (Networking C# Phase 2)
*    -v1.13      gameplay events may name a target object (Networking C#
*                Phase 5A); BeginSpawn: a spawn performed at once by the
*                caller (Networking C# Phase 3)
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
#include <unordered_map>
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
#include "Network/ServerTimeEstimate.h"
#include "Network/SnapshotAckWindow.h"

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

		// Input commands (InGame only)
		bool          hasCommand = false;          // latest is meaningful (a command was taken)
		std::uint32_t lastInputSequence = 0;       // newest command taken; copies and older ones are skipped
		Net::NetTick  lastInputTick = 0;           // Clock().Tick() when it was taken: the input timeout
		InputCommand  latest{};                    // no prediction: the newest command wins
		bool          jumpLatched = false;         // a jump arrived that TakePeerIntent has not handed out
		bool          inputTimedOut = false;       // counted once per silence (NetStats::inputTimeouts)

		// Commands (InGame only)
		std::uint32_t lastCommandSequence = 0;     // newest Command accepted; one that is not newer is malformed

		bool IsJoined() const noexcept { return phase == Phase::Synchronizing || phase == Phase::InGame; }
	};

	/// One player of the session as this machine knows it (NetworkSession::Roster).
	struct NetPeerInfo
	{
		Net::PeerId peer = Net::kInvalidPeer;   // kInvalidPeer: nobody has this PeerId now
		std::string name;
		bool        inGame = false;             // its world is complete (the host always is)

		bool Present() const noexcept { return peer != Net::kInvalidPeer; }
	};

	/// Server: a spawn requested this frame, performed by NetReceiveSystem at the start of the
	/// next frame (structural changes are not allowed inside declared gameplay systems).
	/// Where a player is spawned: a placed instance of the player prefab (Networking Phase 14.6).
	struct PlayerSpawnPoint
	{
		Math::vec3 position{ 0.f };
		Math::quat rotation{ 1.f, 0.f, 0.f, 0.f };
	};

	struct PendingSpawn
	{
		Net::NetworkId   id = Net::kInvalidNetworkId;
		Net::NetPrefabId prefab = Net::kInvalidNetPrefab;
		Net::PeerId      owner = Net::kServerPeer;
		Math::vec3       position{ 0.f };
		Math::quat       rotation{ 1.f, 0.f, 0.f, 0.f };
	};

	/// Server: what one client's players should do now (NetworkSession::TakePeerIntent).
	struct PeerIntent
	{
		Math::vec3 moveDirection{ 0.f };   // world XZ, length <= 1 (the command's, clamped)
		float      yawDegrees = 0.f;
		bool       hasYaw = false;         // a command arrived, so yawDegrees is the client's
		bool       jump = false;           // once per jump the client pressed
		bool       timedOut = false;       // no command for kInputTimeoutTicks: neutral
	};

	/// Host: a gameplay event waiting for the send system (NetworkSession::SendGameplayEvent).
	struct QueuedGameplayEvent
	{
		Net::PeerId            to = Net::kInvalidPeer;   // a client, or NetworkSession::kEveryone
		std::uint16_t          kind = 0;
		std::vector<std::byte> payload;
		Net::NetworkId         target = Net::kInvalidNetworkId;   // the object it is about, or none
	};

	/// Client: where it is in the host's lifecycle stream.
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

		/// Host: GNS's live numbers (ping, quality, rates, queues) for client `peer`'s
		/// connection. False if that slot is free or no session runs. Qualities read -1 for
		/// about the first second of a connection (not measured yet).
		bool PeerLinkStatus(Net::PeerId peer, ConnectionStatus& out) const;

		/// Client: the same for the connection to the host. False before connecting or after leaving.
		bool HostLinkStatus(ConnectionStatus& out) const;

		/// True while this session runs with ActiveSettings().simulation applied (fake latency or
		/// packet loss for the whole process). Start applies it, Stop clears it.
		bool IsSimulating() const noexcept { return m_simulationApplied; }

		// ---- players ------------------------------------------------------------------------

		/// Everyone in the running session, as this machine knows it; the index is the PeerId
		/// (0 = the host) and a slot nobody holds is not Present(). The host: itself and every
		/// joined client. A client: what the host's latest PeerRoster said (nobody before the
		/// first one, which follows JoinAccepted). Offline: nobody.
		std::span<const NetPeerInfo> Roster() const noexcept { return m_roster; }

		/// The roster's entry for `peer`, or null if nobody holds that PeerId.
		const NetPeerInfo* FindPlayer(Net::PeerId peer) const noexcept;

		/// How many players the roster lists, this machine included.
		std::size_t PlayerCount() const noexcept;

		/// Client, NetReceiveSystem: a PeerRoster arrived and was decoded. Replaces Roster() and
		/// posts PeerJoined / PeerReady / PeerLeft for what changed (never for this machine).
		/// False, changing nothing, if this is not a joined client or the list leaves this
		/// machine out (the host always lists the receiver).
		bool ApplyPeerRoster(const PeerRoster& roster);

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
		/// for them can be dropped. The send system (Phase 8) takes them each frame.
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

		/// Server, while Running: allocates an id for a spawn the CALLER performs right now (code
		/// that may change the World's structure: NetReceiveSystem::PerformSpawn does all of it).
		/// Same checks and the same never-reused ids as RequestSpawn, but nothing is queued: the
		/// caller instantiates, adds the NetworkIdentity and then calls NotifySpawned (or
		/// NotifySpawnFailed), so the send system announces it this same frame.
		Net::NetworkId BeginSpawn(Net::NetPrefabId prefab, Net::PeerId owner);

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

		/// Client: sends an Input message (commands, heartbeat, snapshot acks, appliedLifecycle)
		/// unreliably on lane 0, now or never. False if not sent.
		bool SendInput(const InputMessage& message);

		/// Client: the snapshots NetReceiveSystem applied, for NetInputSystem to acknowledge.
		/// Reset by every Start and Stop.
		SnapshotAckWindow&       SnapshotAcks() noexcept       { return m_snapshotAcks; }
		const SnapshotAckWindow& SnapshotAcks() const noexcept { return m_snapshotAcks; }

		// ---- player input (server) --------------------------------------------------------------

		/// Simulation steps without a new command after which a client's intent goes neutral:
		/// 16 steps at 60 Hz is about 270 ms, 8 input packets.
		static constexpr Net::NetTick kInputTimeoutTicks = 16;

		// ---- scene objects (Networking Phase 14.5) -----------------------------------------------

		/// Host: a scene object of the loaded scene (NetSceneObjects.h) becomes a network object: a
		/// new NetworkId (never reused), remembered with its persistent scene id so the world stream
		/// binds it on clients instead of spawning it. NetReceiveSystem then gives the entity its
		/// NetworkIdentity. kInvalidNetworkId unless a running server.
		Net::NetworkId AdoptSceneObject(std::uint64_t sceneId);

		/// The scene id a network object was adopted from, or 0 (a spawned one, or none).
		std::uint64_t SceneIdOf(Net::NetworkId id) const noexcept;

		/// The loaded scene this session plays: its asset GUID and its scene-object hash (0, 0: no
		/// saved scene). The host sends its own in every WorldBegin; a client compares its own.
		void          SetSceneIdentity(std::uint64_t asset, std::uint64_t hash) noexcept { m_sceneAsset = asset; m_sceneHash = hash; }
		std::uint64_t SceneAsset() const noexcept { return m_sceneAsset; }
		std::uint64_t SceneHash() const noexcept  { return m_sceneHash; }

		/// The placed players of the loaded scene, in scene-id order (removed when the session
		/// started; NetInputSystem spawns players there). Empty: none placed.
		void SetPlayerSpawnPoints(std::vector<PlayerSpawnPoint> points) { m_playerSpawnPoints = std::move(points); }
		std::span<const PlayerSpawnPoint> PlayerSpawnPoints() const noexcept { return m_playerSpawnPoints; }

		/// Server: the intent of an InGame peer's players from its newest command, consuming its
		/// latched jump (so call it once per frame per peer). Neutral (no walk, no jump, yaw kept)
		/// before its first command and once kInputTimeoutTicks steps passed without one. False
		/// (and `out` neutral) if `peer` is not InGame.
		bool TakePeerIntent(Net::PeerId peer, PeerIntent& out);

		// ---- gameplay events ----------------------------------------------------------------

		static constexpr Net::PeerId kEveryone = Net::kInvalidPeer;          // SendGameplayEvent: all clients and the host
		static constexpr std::size_t kMaxQueuedGameplayEvents = 1024;       // per frame; more are refused

		/// Host or offline: sends a game-defined event. kind: non-zero, chosen by the game (the
		/// engine never interprets it). payload: at most kMaxGameplayEventPayload bytes, in the
		/// game's own format (its handler validates it). to: kEveryone, kServerPeer (the host
		/// only), or one client's PeerId. The host (and offline, the only receiver) gets it as a
		/// local NetworkEvents::GameplayEventReceived; clients get it reliably, in order, after the
		/// spawns and despawns of this frame - so it may name entities that already exist, but
		/// not one RequestSpawn'ed this frame (that spawn is performed and announced next frame:
		/// send such an event a frame later). False (logged, counted) on a client, while
		/// stopping, for kind 0, an oversized payload, an unknown target, or a full queue.
		/// about: the network object the event concerns, or kInvalidNetworkId. On a host it must
		/// be one the session has now (else refused); a client that no longer has it when the
		/// event arrives drops the event (counted).
		bool SendGameplayEvent(std::uint16_t kind, std::span<const std::byte> payload, Net::PeerId to = kEveryone,
			Net::NetworkId about = Net::kInvalidNetworkId);

		/// The send system: this frame's events for clients (moved into `out`, in send order).
		void TakeGameplayEvents(std::vector<QueuedGameplayEvent>& out);

		/// Client, NetReceiveSystem: a GameplayEvent arrived and was validated; posts it.
		void DeliverGameplayEvent(std::uint16_t kind, std::span<const std::byte> payload,
			Net::NetworkId about = Net::kInvalidNetworkId);

		// ---- commands (client to host) -------------------------------------------------------

		/// Any role: asks the authority to do something. kind: non-zero, chosen by the game (the
		/// engine never interprets it). payload: at most kMaxCommandPayload bytes, in the game's
		/// own format. target: the network object it is about, or kInvalidNetworkId.
		/// A client in the game (its world is Ready) sends it reliably and in order; the host
		/// posts NetworkEvents::CommandReceived for it in the frame it arrives. The host itself,
		/// and an offline game, post that event at once with sender 0 and local set - so gameplay
		/// written as "ask, then act on CommandReceived" runs unchanged in every role.
		/// False (logged, counted) for kind 0, an oversized payload, while stopping, on a client
		/// that is not in the game yet, or if GNS did not take the message (its send buffer is
		/// full: nothing was sent, try again later).
		bool SendCommand(std::uint16_t kind, std::span<const std::byte> payload, Net::NetworkId target = Net::kInvalidNetworkId);

		// ---- client time and responsiveness ------------------------------------------------

		/// Client: the host's clock as this client estimates it (reset by every Start and Stop).
		ServerTimeEstimate&       ServerTime() noexcept       { return m_serverTime; }
		const ServerTimeEstimate& ServerTime() const noexcept { return m_serverTime; }

		/// Client, NetInputSystem: the own player `player`, at `position`, just started being
		/// asked to move (a key went down). Ignored while a measurement is pending.
		void NoteMoveStarted(Net::NetworkId player, const Math::vec3& position);

		/// Client, NetInterpolationSystem: where `player` is drawn this frame. The first frame it
		/// is more than 1 cm from where the move started completes the measurement: the latency
		/// goes to NetStats (input latency fields) and the log. Abandoned (inputLatencyAbandoned)
		/// when that entity is removed, after kLatencyProbeTimeout (checked every Update, so
		/// with no position for it at all), and by Start and Stop. The next key-down then
		/// starts a new one.
		void NoteRenderedPosition(Net::NetworkId player, const Math::vec3& position);

		static constexpr std::chrono::milliseconds kLatencyProbeTimeout{ 2000 };

		/// Client: a key-to-motion measurement is waiting for its motion (diagnostics, tests).
		bool LatencyMeasurementPending() const noexcept { return m_latency.pending; }

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
		void HandleCommand(PeerState& peer, ByteReader& reader);
		void RefreshRoster();     // host: Roster() from the slots; marks it for BroadcastRoster
		void BroadcastRoster();   // host: the whole roster to every joined client
		void CollectFrameMessages();
		void RejectOversized(const ReceivedMessage& message);
		void EnforceQuotas();
		void EnforceJoinTimeouts(std::chrono::steady_clock::time_point now);

		PeerState* FreeSlot() noexcept;
		PeerState* PeerByConnection(NetConnection connection) noexcept;
		PeerState* PeerById(Net::PeerId peer) noexcept;   // a slot that is not Free, or null
		void       Malformed(PeerState& peer, const char* what);
		void       ClosePeer(PeerState& peer, CloseReason reason, const std::string& debug);
		void       AbandonLatencyProbe() noexcept;
		void       ClearSimulation() noexcept;   // back to a normal network, if Start applied one
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
		bool                           m_simulationApplied = false;   // GNS's global fake lag/loss set by Start

		// Client
		ClientPhase                    m_clientPhase = ClientPhase::None;
		NetConnection                  m_serverConnection = kInvalidConnection;
		Net::PeerId                    m_localPeer = Net::kInvalidPeer;
		std::uint32_t                  m_appliedLifecycle = 0;
		ClientWorld                    m_clientWorld = ClientWorld::AwaitingWorld;
		std::vector<ReceivedMessage>   m_worldMessages;   // for NetReceiveSystem, this frame only
		SnapshotAckWindow              m_snapshotAcks;    // applied snapshots, for the next Input
		ServerTimeEstimate             m_serverTime;      // the host's clock, for interpolation
		struct LatencyProbe
		{
			bool                                  pending = false;
			std::chrono::steady_clock::time_point started{};
			Net::NetworkId                        player = Net::kInvalidNetworkId;
			Math::vec3                            from{ 0.f };
		}                              m_latency;
		Net::NetTick                   m_serverTickAtJoin = 0;
		std::chrono::steady_clock::time_point m_joinDeadline{};   // Joining: JoinAccepted due by then
		std::uint32_t                  m_nextCommandSequence = 1;   // stamped on the next Command

		// Players (every role)
		std::array<NetPeerInfo, Net::kMaxPlayers> m_roster{};   // index = PeerId
		bool                           m_rosterChanged = false;   // host: send it at the end of this Update

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

		// Gameplay events (host): this frame's, for the send system
		std::vector<QueuedGameplayEvent> m_gameplayEvents;

		// Scene objects (Phase 14.5)
		std::unordered_map<Net::NetworkId, std::uint64_t> m_sceneObjects;   // host: NetworkId -> scene id, while mapped
		std::uint64_t                  m_sceneAsset = 0;
		std::uint64_t                  m_sceneHash = 0;
		std::vector<PlayerSpawnPoint>  m_playerSpawnPoints;   // Phase 14.6
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
