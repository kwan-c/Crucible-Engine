/*************************************************************************
* \file:         NetworkSession.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.20
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
*    -v1.14      gameplay events: everyone except one peer; SendUnreliableEvent
*                (Networking C# Phase 5B)
*    -v1.15      scene changes that keep the session: Loading phase, SceneEpoch,
*                RequestSceneChange and the transition steps (Networking C# Phase 9);
*                RequestStart, ApplyStartRequest, SetContentPreparer (Phase 10)
*    -v1.16      LastEndCode; the LAN lobby list: SetLobby, StartBrowsing, Lobbies; closed
*                lobbies refuse joins (Networking C# lobby browser)
*    -v1.17      ApplyStartRequest leaves Settings() unchanged
*    -v1.18      the LAN connection server: hosts register (lobbyDiscovery auto/server), browsers list from it
*                with LAN fallback; LobbyListFromServer/Total/Truncated, LobbyServerState, LobbyRegistered;
*                LobbyListingProblem (why a hosted lobby is not listed); Auto browses the server
*                only until UseLanDiscovery (no automatic LAN fallback), LobbyServerFailedAttempts;
*                only an open lobby is registered with the server (SetLobby false unregisters it)
*    -v1.19      the scene seed (protocol 9): SceneSeed, HasSceneSeed, SetSceneSeedFromHost and
*                RequestSceneChange(asset, seed)
*    -v1.20      ownership transfer: RequestOwnerChange, TakeOwnerRequests,
*                NoteOwnerChanged, TakeOwnerChanges
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
#include "Network/LanDiscovery.h"
#include "Network/LobbyServerClient.h"
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
		/// Loading (Networking C# Phase 9): the host changed scenes; this client was told and is
		/// loading it. SceneLoaded moves it back to Synchronizing (the new scene's world follows).
		enum class Phase : std::uint8_t { Free, Connecting, AwaitingJoin, Synchronizing, InGame, Loading };

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
		std::chrono::steady_clock::time_point loadDeadline{};   // Loading: SceneLoaded due by then

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

		bool IsJoined() const noexcept { return phase == Phase::Synchronizing || phase == Phase::InGame || phase == Phase::Loading; }
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

	/// An ownership change: requested by gameplay on the host, then applied
	/// by NetReceiveSystem and sent by the send system.
	struct OwnerChangeRequest
	{
		Net::NetworkId id = Net::kInvalidNetworkId;
		Net::PeerId    owner = Net::kServerPeer;
	};

	/// Host: a gameplay event waiting for the send system (NetworkSession::SendGameplayEvent).
	struct QueuedGameplayEvent
	{
		Net::PeerId            to = Net::kInvalidPeer;   // a client, or NetworkSession::kEveryone
		std::uint16_t          kind = 0;
		std::vector<std::byte> payload;
		Net::NetworkId         target = Net::kInvalidNetworkId;   // the object it is about, or none
		Net::PeerId            except = Net::kInvalidPeer;        // with kEveryone: the one player left out
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

		/// Bumped by every scene this session loads after it started (Networking C# Phase 9).
		/// Generation() + SceneEpoch() name "this session in this scene": per-scene state (scene
		/// objects, spawned players, per-client views) starts over when either changes.
		std::uint32_t SceneEpoch() const noexcept   { return m_sceneEpoch; }

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

		/// Why the latest session ended, as a number: on a client, the host's close reason
		/// (CloseReason, e.g. SessionEnded when the host left, ServerFull, LobbyClosed) or a GNS
		/// code (4001 / 5003: the host stopped answering); CloseReason when this client left
		/// over a problem; 0 when this machine ended it (Stop, Leave) or a start failed.
		int LastEndCode() const noexcept { return m_lastEndCode; }

		// ---- the LAN lobby list (Networking C# lobby browser) -----------------------------------
		//
		// A running host answers lobby queries on ActiveSettings().discoveryPort with its name,
		// players and whether it is open; a browser lists those that answered (LanDiscovery.h).
		// Both are pumped by Update, so they work in every play state.
		//
		// With a connection server set (lobbyServerAddress, ConnectionServer/), a host also
		// registers there and a browser asks it for the list (LobbyServerClient.h). Which is used
		// is lobbyDiscovery:
		//   Auto:   host: registers and answers LAN queries. Browser: the server's list only - empty
		//           while it is tried (kListAttempts requests, about 5 s) and after it failed -
		//           until the player chooses LAN discovery (UseLanDiscovery); then the LAN list
		//           until the next StartBrowsing. LAN is kept running underneath, so the choice is
		//           immediate. No address set: LAN only.
		//   Server: the connection server only.   Lan: LAN broadcast only.
		// Neither is ever in the way of a running session: a server that goes down only changes
		// LobbyServerState() and which list is shown.

		/// Host: whether new players may join (refused with CloseReason::LobbyClosed if not) and a
		/// status shown in lobby lists ("In lobby", "In match"). Every Start resets it to open, "".
		/// Only an open lobby is registered with the connection server: closing it unregisters it
		/// (its slot is freed; the session and its players carry on), opening it registers it
		/// again. LAN discovery keeps answering either way, saying whether it is open.
		void SetLobby(bool open, std::string status);
		bool LobbyOpen() const noexcept { return m_lobbyOpen; }

		/// Lists hosts until StopBrowsing, the way Settings().lobbyDiscovery says (read now; a
		/// later change applies at the next StartBrowsing). False with a reason if nothing can
		/// browse: LAN mode and the socket does not open, Server mode and no server address.
		bool StartBrowsing(std::string& error);
		void StopBrowsing() noexcept;
		bool IsBrowsing() const noexcept { return m_browsing; }

		/// Auto mode: list from LAN discovery instead of the connection server, from now until the
		/// next StartBrowsing (asked after the server was not accessible). True if the list is now
		/// LAN's (already so in Lan mode, or Auto with no server address). False with a reason:
		/// not browsing, Server mode (server only), or LAN discovery could not start on this PC.
		bool UseLanDiscovery(std::string& error);
		/// UseLanDiscovery() would switch the list now: Auto, a server set, LAN running, not chosen yet.
		bool CanUseLanDiscovery() const noexcept;
		/// The player chose LAN discovery (UseLanDiscovery) for this browse.
		bool LanDiscoveryChosen() const noexcept { return m_lanChosen; }
		/// The browser's unanswered list requests in a row (LobbyServer::kListAttempts = gave up).
		int LobbyServerFailedAttempts() const noexcept { return m_lobbyServer.FailedListAttempts(); }

		/// The lobbies to show, rebuilt by Update: one entry per lobby id (LobbyInfo::sources says
		/// where it was seen). Order: the server's (open first, then first registered first) or the
		/// LAN's (first heard first); heartbeats never reorder either.
		std::span<const LobbyInfo> Lobbies() const noexcept { return m_lobbyList; }

		/// True while Lobbies() is the connection server's list (else it is the LAN list).
		bool LobbyListFromServer() const noexcept { return m_listFromServer; }
		/// The server's count of matching lobbies, and whether it sent fewer than that (one packet
		/// holds about 25). Both 0/false while the LAN list is shown.
		std::uint16_t LobbyListTotal() const noexcept { return m_listFromServer ? m_lobbyServer.ListTotal() : 0; }
		bool LobbyListTruncated() const noexcept { return m_listFromServer && m_lobbyServer.ListTruncated(); }
		/// The mode in use by the browser (set by StartBrowsing).
		LobbyDiscovery BrowseDiscovery() const noexcept { return m_browseMode; }

		/// The connection server as this session sees it: Off when none is set or nothing is asked
		/// of it (not hosting a listed session, not browsing).
		LobbyServerStatus LobbyServerState() const noexcept;
		/// Host: the server has acknowledged this session's lobby.
		bool LobbyRegistered() const noexcept { return m_lobbyServer.IsRegistered(); }
		/// "192.168.0.5:27018" of the server in use, or "" if none.
		std::string LobbyServerEndpoint() const;
		/// Host: why lobby browsers cannot find this running session, or "" (it is listed, or still
		/// trying). Server mode with no server address; or LAN broadcast that could not start when
		/// it is the only way (Lan mode, or Auto with no server address). An unreachable server is
		/// LobbyServerState(), not a problem here: it may come back.
		std::string LobbyListingProblem() const;

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

		/// How long a peer may stay Loading after a scene change before it is closed with Timeout.
		static constexpr std::chrono::milliseconds kSceneLoadTimeout{ 30000 };

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
		/// except: one peer (0 = the host itself) who does not get it, or kInvalidPeer.
		bool SendGameplayEvent(std::uint16_t kind, std::span<const std::byte> payload, Net::PeerId to = kEveryone,
			Net::NetworkId about = Net::kInvalidNetworkId, Net::PeerId except = Net::kInvalidPeer);

		/// Host or offline: the same event, but sent to every client in the game ONCE, unreliably,
		/// right now (lane 1, no lifecycle number): it may be lost, and it may overtake a reliable
		/// message sent earlier - so it must not be about an object spawned this frame, and nothing
		/// may depend on it arriving. For frequent effects (footsteps, hit sparks) where a lost one
		/// does not matter. The host (and offline, the only receiver) gets it locally like any
		/// other. payload: at most kMaxUnreliableEventPayload bytes. False as SendGameplayEvent.
		bool SendUnreliableEvent(std::uint16_t kind, std::span<const std::byte> payload,
			Net::NetworkId about = Net::kInvalidNetworkId, Net::PeerId except = Net::kInvalidPeer);

		/// The send system: this frame's events for clients (moved into `out`, in send order).
		void TakeGameplayEvents(std::vector<QueuedGameplayEvent>& out);

		// ---- ownership ----------------------------------------------------
		//
		// The host gives a network object to a player: RequestOwnerChange queues it, NetReceiveSystem
		// applies it next frame (NetworkIdentity::owner, owner-only components switched) and calls
		// NoteOwnerChanged, which posts OwnershipChanged and queues the OwnerChange the send system
		// sends every client in the game. A client applies the message the same way and posts the
		// same event. A joining client's world already carries the new owner. A player (the
		// spawnPerPlayer prefab) never changes owner; when a client leaves, NetReceiveSystem gives
		// its other objects back to the host.

		/// Host, Running: give network object `id` to `owner` (0 = the host, or a client in the
		/// session) at the next frame. False (logged) otherwise. A second request for the same
		/// object in one frame wins.
		bool RequestOwnerChange(Net::NetworkId id, Net::PeerId owner);

		/// NetReceiveSystem (host): the requests since last frame.
		void TakeOwnerRequests(std::vector<OwnerChangeRequest>& out);

		/// NetReceiveSystem: a change was applied to the World. Posts OwnershipChanged; on the host
		/// also queues the OwnerChange for the clients.
		void NoteOwnerChanged(Net::NetworkId id, Net::PeerId previous, Net::PeerId owner);

		/// The send system: this frame's applied changes, in order.
		void TakeOwnerChanges(std::vector<OwnerChangeRequest>& out);

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

		// ---- scene changes (Networking C# Phase 9) --------------------------------------------
		//
		// Host: RequestSceneChange(asset). At the next frame's safe point Core takes it
		// (TakeSceneChangeRequest), asks SceneManager for the load and calls BeginSceneTransition:
		// the id map is emptied first - so tearing down the old scene announces nothing - every
		// joined client is told (SceneChange) and set Loading. The load happens; Core's scene-
		// changed listener calls AcceptSceneChange (else it would end the session) and the epoch
		// moves on. NetReceiveSystem adopts the new scene's objects, NetInputSystem spawns the
		// players again. Each client loads the scene too and answers SceneLoaded; it is then sent
		// the new world exactly as when it joined.
		//
		// Client: NetReceiveSystem applies SceneChange through BeginClientSceneLoad and asks
		// SceneManager for the load (next frame). After it, AcceptSceneChange, the scene scan, then
		// ConfirmSceneLoaded sends SceneLoaded.
		//
		// A scene change nobody asked for this way (the Editor opening a file, a script's
		// Scene.Load) still ends the session.

		/// Host: load the scene with this asset GUID for the whole session, at the next frame's
		/// safe point. False (logged) unless a running host; a second request in one frame wins.
		/// The new scene gets a random seed (SceneSeed).
		bool RequestSceneChange(std::uint64_t sceneAsset);

		/// The same, with the new scene's seed chosen by the caller (any value, 0 included).
		bool RequestSceneChange(std::uint64_t sceneAsset, std::uint32_t seed);

		// ---- the scene seed (protocol 9) --------------------------------------------------------
		//
		// One number per scene of the session, the same on every machine, for procedural content
		// (a generated level). The host picks it when the session starts (at random) and for every
		// scene change (at random, or the caller's). A client receives it in SceneChange - so it is
		// known before the new scene's scripts start - and again in WorldBegin (a client that
		// joined into a running scene learns it there). Unknown (HasSceneSeed false) while offline
		// and on a client before its first SceneChange or WorldBegin.

		std::uint32_t SceneSeed() const noexcept    { return m_sceneSeed; }
		bool          HasSceneSeed() const noexcept { return m_sceneSeedKnown && IsActive(); }

		/// Client, NetReceiveSystem: the seed a SceneChange or WorldBegin carried.
		void SetSceneSeedFromHost(std::uint32_t seed) noexcept { m_sceneSeed = seed; m_sceneSeedKnown = true; }

		/// Core, at the safe point: the requested scene, once. False if none.
		bool TakeSceneChangeRequest(std::uint64_t& sceneAsset) noexcept;

		/// Core, host, after SceneManager accepted the load request and before it is applied: see
		/// above. NetworkIds are never reused, so nothing late from the old scene can touch the new.
		void BeginSceneTransition(std::uint64_t sceneAsset);

		/// Client, NetReceiveSystem: a SceneChange arrived. Forgets the old scene's network objects
		/// (silently: they go with the scene) and waits for its new world. The caller requests the load.
		void BeginClientSceneLoad(const SceneChange& change);

		/// The caller asked SceneManager for the scene of the current change (Core checks after the
		/// next load that it happened: CheckSceneLoad).
		void NoteSceneLoadIssued() noexcept { m_sceneLoadIssued = true; }

		/// Core's scene-changed listener: true if this scene change is the session's own (the epoch
		/// moves on); false if it is not, and the session must end.
		bool AcceptSceneChange() noexcept;

		/// Core, right after SceneManager::ApplyPending: a load this session issued did not happen
		/// (the file could not be read): the host stops, a client leaves.
		void CheckSceneLoad();

		/// Client, NetReceiveSystem, after scanning the new scene: sends SceneLoaded (once).
		void ConfirmSceneLoaded();

		/// The current scene change: its number (0: none yet) and whether it is still running.
		std::uint32_t SceneTransition() const noexcept { return m_transition; }
		bool          IsChangingScene() const noexcept { return m_sceneChangeExpected || m_waitingForPeers || m_confirmPending; }

		// ---- starting from gameplay (Networking C# Phase 10) ----------------------------------

		/// A script asks for a session (C# Network.StartHost / Join). Checked now (Validate, and
		/// that none is running or asked for); started at the next frame's safe point by Core
		/// (ApplyStartRequest), where the network content is prepared as for Play. False with a
		/// reason otherwise. The outcome reaches gameplay as usual: SessionStarted / JoinRefused /
		/// SessionEnded (a start that fails there is posted as SessionEnded with the reason).
		bool RequestStart(const NetworkSettings& settings, std::string& error);
		bool HasStartRequest() const noexcept { return m_startRequested; }

		/// How Core prepares the network content before a Host or Client start (NetContent::Prepare
		/// and the content hash). Unset: nothing is prepared.
		void SetContentPreparer(std::function<void()> prepare) { m_prepareContent = std::move(prepare); }

		/// Core, at the safe point: starts the requested session, if any. Settings() is left as it
		/// was: what Play starts (the Editor's Network panel, EngineConfig.json) does not change.
		void ApplyStartRequest();

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
		void HandleSceneLoaded(PeerState& peer, ByteReader& reader);
		void ResetSceneChange() noexcept;
		static std::uint32_t NewSceneSeed();   // random (std::random_device)
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
		int                            m_endCode = 0;       // why the running session is ending (see LastEndCode)
		int                            m_lastEndCode = 0;

		// The LAN lobby list
		LanDiscovery                   m_discovery;
		bool                           m_lobbyOpen = true;
		std::string                    m_lobbyStatus;
		std::uint64_t                  m_lobbyInstance = 0;   // random per hosted session (BCryptGenRandom)
		std::uint64_t                  m_lobbyToken = 0;      // proves this host owns the id at the server
		bool                           m_advertiseFailed = false;   // logged once per session
		bool                           m_noServerWarned = false;    // Server mode without an address, logged once
		LobbyServerClient              m_lobbyServer;
		std::vector<LobbyServer::HostAddress> m_hostNetworks;  // this PC's, sent when registering
		std::chrono::steady_clock::time_point m_hostNetworksAt{};
		bool                           m_browsing = false;
		bool                           m_lanChosen = false;   // Auto: UseLanDiscovery since StartBrowsing
		LobbyDiscovery                 m_browseMode = LobbyDiscovery::Auto;
		std::string                    m_browseServerAddress;
		std::uint16_t                  m_browseServerPort = 0;
		std::vector<LobbyInfo>         m_lobbyList;
		bool                           m_listFromServer = false;
		void                           PumpDiscovery();
		void                           NewLobbyIdentity();
		void                           RebuildLobbyList(std::chrono::steady_clock::time_point now);

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

		// Scene changes (Phase 9)
		std::uint32_t                  m_sceneEpoch = 0;          // scenes loaded since Start
		std::uint32_t                  m_transition = 0;          // host: the latest change's number; client: the one being loaded
		std::uint64_t                  m_requestedScene = 0;      // host: RequestSceneChange, for the next safe point
		std::uint32_t                  m_requestedSeed = 0;       // host: the seed asked for with it ...
		bool                           m_requestedSeedSet = false;   // ... if one was
		std::uint32_t                  m_sceneSeed = 0;           // the current scene's seed (protocol 9)
		bool                           m_sceneSeedKnown = false;  // host: from the start; client: once received
		bool                           m_sceneChangeExpected = false;   // the next scene change is the session's own
		bool                           m_sceneLoadIssued = false;       // a load for it was requested and not seen yet
		bool                           m_waitingForPeers = false;       // host: AllPeersReady not posted yet
		bool                           m_confirmPending = false;        // client: SceneLoaded not sent yet

		// Starting from gameplay (Phase 10)
		bool                           m_startRequested = false;
		NetworkSettings                m_startSettings;
		std::function<void()>          m_prepareContent;

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
		std::vector<OwnerChangeRequest>  m_ownerRequests;   // host: asked for, applied next frame
		std::vector<OwnerChangeRequest>  m_ownerChanges;    // host: applied, for the send system

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
