/*************************************************************************
* \file:         NetworkSession.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.1
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
*                         -> Synchronizing (JoinAccepted sent; the world
*                            stream and ClientReady arrive in Phase 8)
*                         -> InGame (Phase 8)          -> Free when it leaves
*                  Client (ClientPhase):
*                    Connecting -> Joining (JoinRequest sent; JoinAccepted due
*                    within joinTimeoutMs, else Close(Timeout)) -> Joined
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
*                  - A message bigger than the largest valid protocol message
*                    (16 KB) is malformed: released at once, counted against
*                    its sender, never carried and never billed to the budget.
*                  - At most 64 messages per client per frame; a client over
*                    it for 30 frames in a row is closed (Malformed).
*                  - Every message: size within MaxMessageBytes, lane 0, a
*                    type the client may send. More than 50 bad messages from
*                    one client closes it.
*
*                Threading and call rules: main thread only. Start and Stop
*                at a safe point (Core calls them from the PlayStateChanged
*                listener), never from inside Update; code running inside
*                Update calls RequestStop, which the next Update performs.
*
* \history
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
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "EngineExport.h"
#include "Network/GnsTransport.h"
#include "Network/NetClock.h"
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
		std::uint32_t nextLifecycleSeq = kFirstLifecycleSeq;   // stamped on the next lane-0 message (Phase 8)
		std::uint32_t appliedLifecycle = 0;                    // reported by the client (Phase 8)

		bool IsJoined() const noexcept { return phase == Phase::Synchronizing || phase == Phase::InGame; }
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

		/// The hash both sides must agree on (JoinRequest). Default: DefaultContentHash();
		/// the network prefab table extends it in Phase 7.
		void          SetContentHash(std::uint64_t hash) noexcept { m_contentHash = hash; }
		std::uint64_t ContentHash() const noexcept { return m_contentHash; }

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

	private:
		void HandleServerEvent(const ConnectionEvent& event);
		void HandleClientEvent(const ConnectionEvent& event);
		void HandleServerMessage(const ReceivedMessage& message);
		void HandleClientMessage(const ReceivedMessage& message);
		void HandleJoinRequest(PeerState& peer, ByteReader& reader);
		void CollectFrameMessages();
		void RejectOversized(const ReceivedMessage& message);
		void EnforceQuotas();
		void EnforceJoinTimeouts(std::chrono::steady_clock::time_point now);

		PeerState* FreeSlot() noexcept;
		PeerState* PeerByConnection(NetConnection connection) noexcept;
		void       Malformed(PeerState& peer, const char* what);
		void       ClosePeer(PeerState& peer, CloseReason reason, const std::string& debug);
		void       CloseAndStop(CloseReason reason, const std::string& debug);   // client
		void       Post(auto event);

		template <typename Msg>
		SendResult SendReliable(NetConnection connection, const Msg& message);

		EventBuses&                    m_events;
		NetworkSettings                m_settings;    // for the next Start
		NetworkSettings                m_active;      // the running session's own copy
		std::uint64_t                  m_contentHash;
		std::unique_ptr<GnsTransport>  m_transport;   // null while Offline

		Net::NetRole                   m_role = Net::NetRole::Offline;
		SessionState                   m_state = SessionState::Offline;
		std::uint32_t                  m_generation = 0;
		bool                           m_stopRequested = false;
		std::string                    m_stopReason;

		// Client
		ClientPhase                    m_clientPhase = ClientPhase::None;
		NetConnection                  m_serverConnection = kInvalidConnection;
		Net::PeerId                    m_localPeer = Net::kInvalidPeer;
		std::uint32_t                  m_appliedLifecycle = 0;
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
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
