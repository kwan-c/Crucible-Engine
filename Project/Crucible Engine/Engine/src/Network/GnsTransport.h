/*************************************************************************
* \file:         GnsTransport.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.2
* \brief
*                The thin adapter between Crucible and GameNetworkingSockets
*                (GNS): listen, connect, accept or refuse, send bytes on a
*                lane, receive messages, close with a reason, statistics.
*
* \details
*                This header uses Crucible types only. GnsTransport.cpp is
*                the ONLY file in the engine that includes <steam/...>, so
*                replacing GNS, or adding internet P2P (ConnectionSpec.h),
*                changes one file.
*
*                What GNS does for us: UDP sockets, encryption, the
*                handshake, reliable delivery (resend, ordering within a
*                lane), fragmentation of large messages, congestion
*                control, keep-alives and timeouts, all on its own service
*                thread. What Crucible still owns: deciding who may join,
*                message contents, choosing reliable or unreliable, freeing
*                every handle and message, and reacting to failures.
*
*                Threading: call everything from ONE thread (the main
*                thread). GNS invokes the status callback inside
*                PollEvents (RunCallbacks), on that same thread.
*
*                Handle lifetime (every handle is freed exactly once):
*                  - A connection we created (Connect) or that arrived on
*                    our listen socket stays ours until freed, even after
*                    the peer closes it.
*                  - Close() frees it at once.
*                  - A connection the PEER closed (or that timed out) is
*                    reported as a Closed event and freed at the start of
*                    the NEXT PollEvents, so messages that arrived before
*                    the close can still be read with Receive in between.
*                  - CloseAll() and the destructor free everything.
*
*                Several transports may exist in one process (tests run a
*                server and a client together). Each event goes to the
*                transport that owns the connection, whichever transport's
*                PollEvents happened to run the callback.
*
*                Ownership rule: GNS handles are process-wide, so GNS itself
*                would let any transport send on, configure, read or close
*                any handle. Every connection-specific call here first
*                checks that THIS transport owns the handle (Owns), and
*                refuses another transport's handle without touching it.
*                A connection is owned from the moment it is created
*                (Connect) or arrives (Incoming event) until it is freed.
*
* \history
*    -v1.0       Networking Guide 7.5, 19.4, 18.3 issues 10 and 14: spec-
*                based Listen/Connect, SendResult, per-lane metrics,
*                ownership-based event routing, deferred free of closed
*                connections, one poll group for every connection
*    -v1.1       review fixes: every connection-specific call checks
*                ownership (SetUserData and Flush now return bool); Accept
*                only accepts a not-yet-accepted incoming connection and
*                never frees anything else; Receive grows its output
*                geometrically; IsKnownToGns for leak checks
*    -v1.2       IsIpAddress, so settings can be validated before Play
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Network/ConnectionSpec.h"

// GNS types, only ever used through pointers here.
struct SteamNetworkingMessage_t;
struct SteamNetConnectionStatusChangedCallback_t;
class ISteamNetworkingSockets;

namespace Crucible
{
	/// A GNS connection handle (same bits as HSteamNetConnection). 0 is never valid.
	using NetConnection = std::uint32_t;
	inline constexpr NetConnection kInvalidConnection = 0;

	enum class Delivery : std::uint8_t
	{
		Unreliable,          // may be lost, duplicated or reordered; Nagle may delay it up to 5 ms
		UnreliableNoDelay,   // as above, sent now or dropped (input commands)
		Reliable,            // arrives once, in order with other reliable messages on the same lane
	};

	/// Outbound lanes. The SERVER configures all three on each client connection (Accept);
	/// a client sends on Gameplay only. Lower lane = sent first.
	enum class Lane : std::uint16_t { Gameplay = 0, Snapshot = 1, Bulk = 2 };
	inline constexpr std::size_t kLaneCount = 3;

	/// Application close reasons. GNS reserves 1000-1999 for normal application closes; the
	/// peer receives the number in its Closed event even when no message gets through.
	enum class CloseReason : int
	{
		Generic          = 1000,
		ProtocolMismatch = 1001,
		ServerFull       = 1002,
		Malformed        = 1003,
		SessionEnded     = 1004,
		SpawnFailed      = 1005,
		SendFailed       = 1006,
		Timeout          = 1007,
	};

	/// Why a send did not happen (Networking Guide issue 10). Each caller has a policy:
	/// a failed reliable lifecycle message closes that peer; a failed snapshot or input is
	/// counted and superseded by the next one.
	enum class SendResult : std::uint8_t
	{
		Ok,             // queued
		Dropped,        // UnreliableNoDelay and GNS could not send it right now
		QueueFull,      // the connection's send buffer is full (SendBufferSize)
		NoConnection,   // invalid handle, closed, or not in a state that can send
		Failed,         // a caller bug or GNS failure: over 512 KB, or lane 1/2 on a connection without lanes
	};

	constexpr const char* ToString(SendResult result) noexcept
	{
		switch (result)
		{
		case SendResult::Ok:           return "Ok";
		case SendResult::Dropped:      return "Dropped";
		case SendResult::QueueFull:    return "QueueFull";
		case SendResult::NoConnection: return "NoConnection";
		default:                       return "Failed";
		}
	}

	struct ConnectionEvent
	{
		enum class Kind : std::uint8_t
		{
			Incoming,    // someone is connecting to our listen socket: Accept() or Close() it promptly
			Connected,   // handshake finished (both sides get this)
			Closed,      // the peer closed it, or it timed out / failed locally
		};
		Kind          kind = Kind::Closed;
		NetConnection connection = kInvalidConnection;
		int           endReason = 0;                  // Closed: CloseReason value, or GNS's own 3000-5999 code
		std::string   debug;                          // Closed: human-readable reason
		std::string   remoteAddress;                  // "ip:port" of the other side
		bool          problemDetectedLocally = false; // Closed: timeout or local failure, not the peer's choice
	};

	/// Connection-wide numbers from GNS. Values are estimates GNS updates continuously;
	/// for about the first second qualities read -1 ("not measured yet").
	struct ConnectionStatus
	{
		int          pingMs = 0;
		float        qualityLocal = 0.f;    // 0..1, share of packets the peer sent that arrived
		float        qualityRemote = 0.f;   // 0..1, share of our packets the peer received
		float        outBytesPerSec = 0.f;
		float        inBytesPerSec = 0.f;
		int          sendRateBytesPerSec = 0;
		int          pendingUnreliable = 0;
		int          pendingReliable = 0;
		int          sentUnackedReliable = 0;
		std::int64_t queueTimeUs = 0;       // how long a message queued now would wait
	};

	/// One outbound lane's queue (Networking Guide issue 14: budget snapshots from lane 1
	/// alone, so a burst of spawns on lane 0 does not shrink them).
	struct LaneMetrics
	{
		int          pendingUnreliable = 0;
		int          pendingReliable = 0;
		int          sentUnackedReliable = 0;
		std::int64_t queueTimeUs = 0;
	};

	/*********************************************************************
	 \brief
		Owns one received GNS message and releases it in its destructor.
	 \details
		Move-only. Data() points into GNS's buffer and is valid as long
		as this object lives. Release every message before KillLibrary.
	 *********************************************************************/
	class ReceivedMessage
	{
	public:
		explicit ReceivedMessage(SteamNetworkingMessage_t* message) noexcept : m_message(message) {}

		std::span<const std::byte> Data() const noexcept;
		NetConnection              Connection() const noexcept;
		std::int64_t               ConnectionUserData() const noexcept;   // see GnsTransport::SetUserData
		/// The lane the peer sent it on. May be outside Lane's named values if the peer
		/// configured more lanes than we use: validate before trusting it.
		Lane                       LaneIndex() const noexcept;
		bool                       IsReliable() const noexcept;

	private:
		struct Release { void operator()(SteamNetworkingMessage_t* message) const noexcept; };
		std::unique_ptr<SteamNetworkingMessage_t, Release> m_message;
	};

	/*********************************************************************
	 \brief
		One endpoint's connections: a listen socket and its accepted
		clients (server), or one outgoing connection (client).
	 \details
		The GNS library must be initialized first (InitLibrary). Every
		connection, incoming or outgoing, is in this transport's single
		poll group, so Receive() is the same call for both roles.
	 *********************************************************************/
	class GnsTransport
	{
	public:
		/// Starts GNS once per process. Safe to call again (no-op). GNS diagnostics go to the
		/// engine log (warnings and errors, or everything with verbose).
		static bool InitLibrary(std::string& error, bool verbose = false);

		/// Stops GNS. Closes every live transport's connections first; those transports
		/// can be used again after the next InitLibrary. Init after Kill works (verified).
		static void KillLibrary() noexcept;

		static bool IsLibraryInitialized() noexcept;

		/// Diagnostics: true if GNS still knows this handle, whichever transport (if any) owns
		/// it. Leak checks use it to prove a freed handle is really gone from GNS.
		static bool IsKnownToGns(NetConnection connection) noexcept;

		/// True if `text` is an IPv4 or IPv6 address literal with no port ("192.168.1.20",
		/// "::1"), exactly what Listen and Connect accept. Host names are not addresses.
		/// Works before InitLibrary (pure parsing).
		static bool IsIpAddress(std::string_view text);

		GnsTransport();
		~GnsTransport();   // CloseAll(SessionEnded, no linger)
		GnsTransport(const GnsTransport&) = delete;
		GnsTransport& operator=(const GnsTransport&) = delete;
		GnsTransport(GnsTransport&&) = delete;              // the callback router holds `this`
		GnsTransport& operator=(GnsTransport&&) = delete;

		/// Starts listening. Fails if this transport already listens, the port is 0 (GNS cannot
		/// pick one), the address is not an IP, or the port is in use. Note: a port closed a
		/// moment ago stays busy for a few milliseconds (measured 5-7 ms in Phase 1).
		bool Listen(const ListenSpec& spec, std::string& error);

		/// Starts connecting; `out` is valid at once (state Connecting). The result arrives
		/// later as a Connected or Closed event.
		bool Connect(const ConnectSpec& spec, NetConnection& out, std::string& error);

		bool          IsListening() const noexcept;
		std::uint16_t ListeningPort() const noexcept;   // the bound port, 0 if not listening

		/// Server, after an Incoming event: completes the handshake and configures the three
		/// outbound lanes. Only for an incoming connection not yet accepted: anything else
		/// (accepted already, our own outgoing one, another transport's) returns false and is
		/// left untouched. False with the handle freed if the peer gave up before we accepted.
		bool Accept(NetConnection connection);

		/// Configures the three outbound lanes (Accept does this for the server).
		bool ConfigureLanes(NetConnection connection);

		/// A number GNS copies into every message received on this connection
		/// (ReceivedMessage::ConnectionUserData): the session stores the PeerId here.
		/// False for a connection this transport does not own.
		bool SetUserData(NetConnection connection, std::int64_t value);

		/// Bytes GNS may queue on this connection before Send returns QueueFull (default 512 KB).
		bool SetSendBufferSize(NetConnection connection, int bytes);

		/// Closes and frees a connection, sending `reason` to the peer. Linger = try to deliver
		/// queued reliable messages first. Refusing an Incoming connection is just Close.
		void Close(NetConnection connection, CloseReason reason, const char* debug, bool linger) noexcept;

		/// Closes every connection, the listen socket and the poll group.
		void CloseAll(CloseReason reason, bool linger) noexcept;

		/// Queues one message. NoConnection for a handle this transport does not own (see the
		/// ownership rule above), or one that is closed.
		SendResult Send(NetConnection connection, std::span<const std::byte> data, Delivery delivery, Lane lane) noexcept;

		/// Sends anything waiting on the Nagle timer now. False for a connection not owned here.
		bool Flush(NetConnection connection) noexcept;

		/// Runs GNS callbacks and appends this transport's connection events. Call every frame:
		/// without it no connection ever changes state, and closed handles are never freed.
		void PollEvents(std::vector<ConnectionEvent>& out);

		/// Appends up to maxMessages received messages (from every connection) to `out`.
		/// Returns how many were added. The rest stay queued in GNS for the next call.
		std::size_t Receive(std::vector<ReceivedMessage>& out, std::size_t maxMessages);

		bool Status(NetConnection connection, ConnectionStatus& out) const;

		/// Per-lane queues. A connection with fewer lanes (a client's) fills lane 0 only.
		bool LaneStatus(NetConnection connection, std::span<LaneMetrics, kLaneCount> out) const;

		/// Handles this transport has not freed yet (diagnostics and leak tests).
		std::size_t OpenConnectionCount() const noexcept { return m_connections.size(); }

	private:
		static void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
		void HandleStatus(const SteamNetConnectionStatusChangedCallback_t& info);
		bool EnsureReady(std::string& error);
		void Free(NetConnection connection) noexcept;     // CloseConnection(0) + Forget: a dead handle
		void Forget(NetConnection connection) noexcept;   // drop it from every list below
		bool Owns(NetConnection connection) const noexcept;

		ISteamNetworkingSockets*     m_api = nullptr;
		std::uint32_t                m_listenSocket = 0;   // HSteamListenSocket
		std::uint32_t                m_pollGroup = 0;      // HSteamNetPollGroup
		std::vector<NetConnection>   m_connections;        // every handle not yet freed
		std::vector<NetConnection>   m_freeNextPoll;       // closed by the peer, already reported
		std::vector<NetConnection>   m_threeLanes;         // connections whose lanes are configured
		std::vector<NetConnection>   m_awaitingAccept;     // incoming, neither accepted nor closed yet
		std::vector<ConnectionEvent> m_pendingEvents;      // routed here by the callback
	};
}
