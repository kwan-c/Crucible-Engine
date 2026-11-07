/*************************************************************************
* \file:         GnsTransport.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.2
* \brief
*                GnsTransport on GameNetworkingSockets v1.6.1: the only
*                file in the engine that includes GNS headers.
*
* \history
*    -v1.0       Networking Guide 7.5, 19.4, 18.3 issues 10 and 14
*    -v1.1       review fixes: ownership checks on every connection-
*                specific call, safe Accept, geometric Receive growth
*    -v1.2       IsIpAddress
*
**************************************************************************/
#include "Network/GnsTransport.h"

#include "Diagnostics/Logger.h"

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>

namespace Crucible
{
	namespace
	{
		bool g_libraryInitialized = false;

		// Every live transport, so a status callback can be routed to the one that owns the
		// connection. Main thread only, like the rest of the class.
		std::vector<GnsTransport*> g_transports;

		// GNS may call this from its own service thread; the engine logger is thread-safe.
		void GnsDebugOutput(ESteamNetworkingSocketsDebugOutputType type, const char* text)
		{
			if (type <= k_ESteamNetworkingSocketsDebugOutputType_Error)        CRU_ERROR("[GNS] {}", text);
			else if (type == k_ESteamNetworkingSocketsDebugOutputType_Warning) CRU_WARN("[GNS] {}", text);
			else                                                               CRU_INFO("[GNS] {}", text);
		}

		std::string AddressText(const SteamNetworkingIPAddr& address)
		{
			char text[SteamNetworkingIPAddr::k_cchMaxString];
			address.ToString(text, sizeof(text), true);
			return text;
		}

		// An IP literal with no port of its own ("192.168.1.20", "::1"); the port comes from the spec.
		bool ParseIp(const IpEndpoint& endpoint, SteamNetworkingIPAddr& out, std::string& error)
		{
			out.Clear();
			if (!endpoint.address.empty())
			{
				if (!out.ParseString(endpoint.address.c_str()))
				{
					error = "'" + endpoint.address + "' is not an IP address";
					return false;
				}
				if (out.m_port != 0)
				{
					error = "put the port in IpEndpoint::port, not in the address";
					return false;
				}
			}
			out.m_port = endpoint.port;
			return true;
		}

		int ToSendFlags(Delivery delivery) noexcept
		{
			switch (delivery)
			{
			case Delivery::Reliable:          return k_nSteamNetworkingSend_Reliable;
			case Delivery::UnreliableNoDelay: return k_nSteamNetworkingSend_UnreliableNoDelay;
			default:                          return k_nSteamNetworkingSend_Unreliable;
			}
		}

		// InvalidParam is NoConnection: the size, the only other cause, is checked before sending.
		SendResult ToSendResult(EResult result) noexcept
		{
			switch (result)
			{
			case k_EResultOK:            return SendResult::Ok;
			case k_EResultIgnored:       return SendResult::Dropped;
			case k_EResultLimitExceeded: return SendResult::QueueFull;
			case k_EResultNoConnection:
			case k_EResultInvalidState:
			case k_EResultInvalidParam:  return SendResult::NoConnection;
			default:                     return SendResult::Failed;
			}
		}

		// The options every listen socket and connection is created with.
		int MakeOptions(const ConnectionTimeouts& timeouts, SteamNetworkingConfigValue_t (&options)[3], FnSteamNetConnectionStatusChanged callback)
		{
			int count = 0;
			options[count++].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(callback));
			if (timeouts.initialMs > 0)   options[count++].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, timeouts.initialMs);
			if (timeouts.connectedMs > 0) options[count++].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, timeouts.connectedMs);
			return count;
		}
	}

	// ---- library lifetime ---------------------------------------------------------------

	bool GnsTransport::InitLibrary(std::string& error, bool verbose)
	{
		if (g_libraryInitialized) return true;
		SteamNetworkingErrMsg message{};
		if (!GameNetworkingSockets_Init(nullptr, message))
		{
			error = std::string("GameNetworkingSockets_Init failed: ") + message;
			return false;
		}
		SteamNetworkingUtils()->SetDebugOutputFunction(
			verbose ? k_ESteamNetworkingSocketsDebugOutputType_Msg : k_ESteamNetworkingSocketsDebugOutputType_Warning,
			&GnsDebugOutput);
		g_libraryInitialized = true;
		return true;
	}

	void GnsTransport::KillLibrary() noexcept
	{
		if (!g_libraryInitialized) return;
		for (GnsTransport* transport : g_transports)
		{
			transport->CloseAll(CloseReason::SessionEnded, false);
			transport->m_api = nullptr;   // reacquired by EnsureReady after the next InitLibrary
		}
		GameNetworkingSockets_Kill();
		g_libraryInitialized = false;
	}

	bool GnsTransport::IsLibraryInitialized() noexcept { return g_libraryInitialized; }

	// ---- received messages ----------------------------------------------------------------

	void ReceivedMessage::Release::operator()(SteamNetworkingMessage_t* message) const noexcept { message->Release(); }

	std::span<const std::byte> ReceivedMessage::Data() const noexcept
	{
		return { static_cast<const std::byte*>(m_message->m_pData), static_cast<std::size_t>(m_message->m_cbSize) };
	}

	NetConnection ReceivedMessage::Connection() const noexcept         { return m_message->m_conn; }
	std::int64_t  ReceivedMessage::ConnectionUserData() const noexcept { return m_message->m_nConnUserData; }
	Lane          ReceivedMessage::LaneIndex() const noexcept          { return static_cast<Lane>(m_message->m_idxLane); }
	bool          ReceivedMessage::IsReliable() const noexcept         { return (m_message->m_nFlags & k_nSteamNetworkingSend_Reliable) != 0; }

	// ---- setup ------------------------------------------------------------------------------

	GnsTransport::GnsTransport()
	{
		g_transports.push_back(this);
	}

	GnsTransport::~GnsTransport()
	{
		CloseAll(CloseReason::SessionEnded, false);
		std::erase(g_transports, this);
	}

	bool GnsTransport::EnsureReady(std::string& error)
	{
		if (!g_libraryInitialized) { error = "GNS is not initialized (GnsTransport::InitLibrary)"; return false; }
		if (m_api == nullptr) m_api = SteamNetworkingSockets();
		if (m_pollGroup == k_HSteamNetPollGroup_Invalid)
		{
			m_pollGroup = m_api->CreatePollGroup();
			if (m_pollGroup == k_HSteamNetPollGroup_Invalid) { error = "CreatePollGroup failed"; return false; }
		}
		return true;
	}

	bool GnsTransport::Listen(const ListenSpec& spec, std::string& error)
	{
		const IpEndpoint* ip = std::get_if<IpEndpoint>(&spec.how);
		if (ip == nullptr) { error = "internet play is not available in this build"; return false; }   // Phase B
		if (IsListening()) { error = "already listening"; return false; }
		if (ip->port == 0) { error = "listen needs a non-zero port (GNS does not pick one)"; return false; }
		if (!EnsureReady(error)) return false;

		SteamNetworkingIPAddr local;
		if (!ParseIp(*ip, local, error)) return false;
		SteamNetworkingConfigValue_t options[3];
		const int count = MakeOptions(spec.timeouts, options, &GnsTransport::OnStatusChanged);   // inherited by accepted connections
		m_listenSocket = m_api->CreateListenSocketIP(local, count, options);
		if (m_listenSocket == k_HSteamListenSocket_Invalid)
		{
			m_listenSocket = 0;
			error = "cannot listen on " + AddressText(local) + " (port in use?)";
			return false;
		}
		return true;
	}

	bool GnsTransport::Connect(const ConnectSpec& spec, NetConnection& out, std::string& error)
	{
		out = kInvalidConnection;
		const IpEndpoint* ip = std::get_if<IpEndpoint>(&spec.to);
		if (ip == nullptr) { error = "internet play is not available in this build"; return false; }   // Phase B
		if (ip->address.empty() || ip->port == 0) { error = "connect needs an IP address and a non-zero port"; return false; }
		if (!EnsureReady(error)) return false;

		SteamNetworkingIPAddr remote;
		if (!ParseIp(*ip, remote, error)) return false;
		SteamNetworkingConfigValue_t options[3];
		const int count = MakeOptions(spec.timeouts, options, &GnsTransport::OnStatusChanged);
		const HSteamNetConnection connection = m_api->ConnectByIPAddress(remote, count, options);
		if (connection == k_HSteamNetConnection_Invalid) { error = "ConnectByIPAddress failed for " + AddressText(remote); return false; }

		m_connections.push_back(connection);
		m_api->SetConnectionPollGroup(connection, m_pollGroup);
		out = connection;
		return true;
	}

	bool GnsTransport::IsListening() const noexcept { return m_listenSocket != k_HSteamListenSocket_Invalid; }

	std::uint16_t GnsTransport::ListeningPort() const noexcept
	{
		SteamNetworkingIPAddr address;
		if (!IsListening() || m_api == nullptr || !m_api->GetListenSocketAddress(m_listenSocket, &address)) return 0;
		return address.m_port;
	}

	bool GnsTransport::Accept(NetConnection connection)
	{
		// Only an incoming connection not yet accepted. Anything else (a second Accept, our own
		// outgoing connection, another transport's) is refused WITHOUT touching it.
		if (m_api == nullptr || std::ranges::find(m_awaitingAccept, connection) == m_awaitingAccept.end()) return false;
		std::erase(m_awaitingAccept, connection);
		if (m_api->AcceptConnection(connection) != k_EResultOK)
		{
			Free(connection);   // the client gave up before we accepted: its handle is dead
			return false;
		}
		m_api->SetConnectionPollGroup(connection, m_pollGroup);
		return ConfigureLanes(connection);
	}

	bool GnsTransport::ConfigureLanes(NetConnection connection)
	{
		if (m_api == nullptr || !Owns(connection)) return false;
		// Lower priority NUMBER is served first: gameplay (inputs, spawns, events) before
		// snapshots before bulk data. Weights only matter between equal priorities.
		const int    priorities[kLaneCount] = { 0, 1, 2 };
		const uint16 weights[kLaneCount]    = { 1, 1, 1 };
		if (m_api->ConfigureConnectionLanes(connection, static_cast<int>(kLaneCount), priorities, weights) != k_EResultOK) return false;
		if (std::ranges::find(m_threeLanes, connection) == m_threeLanes.end()) m_threeLanes.push_back(connection);
		return true;
	}

	bool GnsTransport::SetUserData(NetConnection connection, std::int64_t value)
	{
		return m_api != nullptr && Owns(connection) && m_api->SetConnectionUserData(connection, value);
	}

	bool GnsTransport::SetSendBufferSize(NetConnection connection, int bytes)
	{
		return m_api != nullptr && Owns(connection)
			&& SteamNetworkingUtils()->SetConnectionConfigValueInt32(connection, k_ESteamNetworkingConfig_SendBufferSize, bytes);
	}

	// ---- teardown -----------------------------------------------------------------------------

	bool GnsTransport::Owns(NetConnection connection) const noexcept
	{
		return std::ranges::find(m_connections, connection) != m_connections.end();
	}

	void GnsTransport::Forget(NetConnection connection) noexcept
	{
		std::erase(m_connections, connection);
		std::erase(m_freeNextPoll, connection);
		std::erase(m_threeLanes, connection);
		std::erase(m_awaitingAccept, connection);
	}

	void GnsTransport::Free(NetConnection connection) noexcept
	{
		if (m_api != nullptr) m_api->CloseConnection(connection, 0, nullptr, false);   // a dead connection: frees the handle
		Forget(connection);
	}

	void GnsTransport::Close(NetConnection connection, CloseReason reason, const char* debug, bool linger) noexcept
	{
		if (!Owns(connection)) return;   // never close another transport's connection
		if (m_api != nullptr) m_api->CloseConnection(connection, static_cast<int>(reason), debug, linger);   // the handle is invalid from here on
		Forget(connection);
	}

	void GnsTransport::CloseAll(CloseReason reason, bool linger) noexcept
	{
		if (m_api != nullptr)
		{
			for (const NetConnection connection : m_connections) m_api->CloseConnection(connection, static_cast<int>(reason), "session ended", linger);
			if (m_listenSocket != k_HSteamListenSocket_Invalid) m_api->CloseListenSocket(m_listenSocket);
			if (m_pollGroup != k_HSteamNetPollGroup_Invalid) m_api->DestroyPollGroup(m_pollGroup);
		}
		m_connections.clear();
		m_freeNextPoll.clear();
		m_threeLanes.clear();
		m_awaitingAccept.clear();
		m_pendingEvents.clear();
		m_listenSocket = k_HSteamListenSocket_Invalid;
		m_pollGroup = k_HSteamNetPollGroup_Invalid;
	}

	// ---- sending ------------------------------------------------------------------------------

	SendResult GnsTransport::Send(NetConnection connection, std::span<const std::byte> data, Delivery delivery, Lane lane) noexcept
	{
		// GNS handles are process-wide: GNS would happily send on another transport's handle.
		if (m_api == nullptr || !Owns(connection)) return SendResult::NoConnection;
		if (data.size() > static_cast<std::size_t>(k_cbMaxSteamNetworkingSocketsMessageSizeSend)) return SendResult::Failed;
		if (lane != Lane::Gameplay && std::ranges::find(m_threeLanes, connection) == m_threeLanes.end())
			return SendResult::Failed;   // lane 1/2 needs ConfigureLanes: a caller bug

		// SendMessageToConnection has no lane parameter, so a lane needs AllocateMessage +
		// SendMessages. With bDeleteFailedMessages = true GNS owns the message afterwards,
		// sent or not: it is never released here.
		SteamNetworkingMessage_t* message = SteamNetworkingUtils()->AllocateMessage(static_cast<int>(data.size()));
		if (message == nullptr) return SendResult::Failed;
		if (!data.empty()) std::memcpy(message->m_pData, data.data(), data.size());
		message->m_conn = connection;
		message->m_nFlags = ToSendFlags(delivery);
		message->m_idxLane = static_cast<uint16>(lane);

		int64 result = 0;   // message number (> 0), or -EResult
		m_api->SendMessages(1, &message, &result, true);
		if (result > 0) return SendResult::Ok;
		return result == 0 ? SendResult::Failed : ToSendResult(static_cast<EResult>(-result));
	}

	bool GnsTransport::Flush(NetConnection connection) noexcept
	{
		return m_api != nullptr && Owns(connection) && m_api->FlushMessagesOnConnection(connection) == k_EResultOK;
	}

	// ---- callbacks and receiving --------------------------------------------------------------

	void GnsTransport::PollEvents(std::vector<ConnectionEvent>& out)
	{
		// Connections whose Closed event was handed out last time: the caller has had one
		// chance to Receive their last messages, so free them now.
		while (!m_freeNextPoll.empty()) Free(m_freeNextPoll.back());

		if (m_api == nullptr) return;
		m_api->RunCallbacks();   // OnStatusChanged runs inside this call, on this thread

		for (ConnectionEvent& event : m_pendingEvents)
		{
			if (event.kind == ConnectionEvent::Kind::Closed) m_freeNextPoll.push_back(event.connection);
			out.push_back(std::move(event));
		}
		m_pendingEvents.clear();
	}

	void GnsTransport::OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
	{
		try
		{
			const HSteamListenSocket listenSocket = info->m_info.m_hListenSocket;
			for (GnsTransport* transport : g_transports)
			{
				const bool incomingHere = listenSocket != k_HSteamListenSocket_Invalid && listenSocket == transport->m_listenSocket;
				if (incomingHere || transport->Owns(info->m_hConn))
				{
					transport->HandleStatus(*info);
					return;
				}
			}
			// Nobody owns it (its transport was destroyed): never leak a dead handle.
			const ESteamNetworkingConnectionState state = info->m_info.m_eState;
			if (state == k_ESteamNetworkingConnectionState_ClosedByPeer || state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
				SteamNetworkingSockets()->CloseConnection(info->m_hConn, 0, nullptr, false);
		}
		catch (...)
		{
			// An exception must not unwind through GNS's C code.
			CRU_ERROR("[Net] exception in the GNS status callback; connection {} event dropped", info->m_hConn);
		}
	}

	void GnsTransport::HandleStatus(const SteamNetConnectionStatusChangedCallback_t& info)
	{
		ConnectionEvent event;
		event.connection = info.m_hConn;
		event.remoteAddress = AddressText(info.m_info.m_addrRemote);

		switch (info.m_info.m_eState)
		{
		case k_ESteamNetworkingConnectionState_Connecting:
			// Fires on both sides. Only an incoming connection needs a decision.
			if (info.m_info.m_hListenSocket == k_HSteamListenSocket_Invalid) return;
			if (!Owns(info.m_hConn))
			{
				m_connections.push_back(info.m_hConn);      // ours to free from now on
				m_awaitingAccept.push_back(info.m_hConn);   // until Accept or Close
			}
			event.kind = ConnectionEvent::Kind::Incoming;
			break;
		case k_ESteamNetworkingConnectionState_Connected:
			event.kind = ConnectionEvent::Kind::Connected;
			break;
		case k_ESteamNetworkingConnectionState_ClosedByPeer:
		case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
			// Dead, but the handle stays allocated until we free it (next PollEvents).
			event.kind = ConnectionEvent::Kind::Closed;
			event.endReason = info.m_info.m_eEndReason;
			event.debug = info.m_info.m_szEndDebug;
			event.problemDetectedLocally = info.m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally;
			break;
		default:
			return;   // None (the echo of our own close), FindingRoute
		}
		m_pendingEvents.push_back(std::move(event));
	}

	std::size_t GnsTransport::Receive(std::vector<ReceivedMessage>& out, std::size_t maxMessages)
	{
		if (m_api == nullptr || m_pollGroup == k_HSteamNetPollGroup_Invalid) return 0;
		std::size_t total = 0;
		SteamNetworkingMessage_t* batch[64];
		while (total < maxMessages)
		{
			const int capacity = static_cast<int>(std::min<std::size_t>(maxMessages - total, std::size(batch)));
			// Room for a whole batch BEFORE receiving it: if reserve throws, GNS still holds every
			// message, so nothing leaks, and the emplace_back calls below cannot reallocate or throw.
			// Grow geometrically: reserve(size + 64) every batch would reallocate, and move every
			// message already in `out`, once per batch.
			const std::size_t needed = out.size() + static_cast<std::size_t>(capacity);
			if (needed > out.capacity()) out.reserve(std::max(needed, out.capacity() * 2));
			const int count = m_api->ReceiveMessagesOnPollGroup(m_pollGroup, batch, capacity);
			if (count <= 0) break;
			for (int i = 0; i < count; ++i) out.emplace_back(batch[i]);   // cannot throw now: ownership moves into RAII
			total += static_cast<std::size_t>(count);
			if (count < capacity) break;   // drained
		}
		return total;
	}

	// ---- statistics ---------------------------------------------------------------------------

	bool GnsTransport::IsKnownToGns(NetConnection connection) noexcept
	{
		SteamNetConnectionInfo_t info;
		return g_libraryInitialized && SteamNetworkingSockets()->GetConnectionInfo(connection, &info);
	}

	bool GnsTransport::IsIpAddress(std::string_view text)
	{
		if (text.empty() || text.size() >= SteamNetworkingIPAddr::k_cchMaxString) return false;
		// GNS's parser tolerates surrounding spaces ("1.2.3.4 "); a stray space in a settings
		// field should be reported, not silently accepted.
		if (std::ranges::any_of(text, [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; })) return false;
		const std::string copy(text);   // ParseString needs a terminated string
		SteamNetworkingIPAddr address;
		return address.ParseString(copy.c_str()) && address.m_port == 0;   // the same rule as ParseIp
	}

	bool GnsTransport::Status(NetConnection connection, ConnectionStatus& out) const
	{
		if (m_api == nullptr || !Owns(connection)) return false;
		SteamNetConnectionRealTimeStatus_t s{};
		if (m_api->GetConnectionRealTimeStatus(connection, &s, 0, nullptr) != k_EResultOK) return false;
		out.pingMs = s.m_nPing;
		out.qualityLocal = s.m_flConnectionQualityLocal;
		out.qualityRemote = s.m_flConnectionQualityRemote;
		out.outBytesPerSec = s.m_flOutBytesPerSec;
		out.inBytesPerSec = s.m_flInBytesPerSec;
		out.sendRateBytesPerSec = s.m_nSendRateBytesPerSecond;
		out.pendingUnreliable = s.m_cbPendingUnreliable;
		out.pendingReliable = s.m_cbPendingReliable;
		out.sentUnackedReliable = s.m_cbSentUnackedReliable;
		out.queueTimeUs = s.m_usecQueueTime;
		return true;
	}

	bool GnsTransport::LaneStatus(NetConnection connection, std::span<LaneMetrics, kLaneCount> out) const
	{
		if (m_api == nullptr || !Owns(connection)) return false;
		SteamNetConnectionRealTimeLaneStatus_t lanes[kLaneCount]{};
		// Asking for more lanes than are configured is an error (GNS logs it), so ask for
		// what this connection has: three after ConfigureLanes, otherwise GNS's default one.
		const bool threeLanes = std::ranges::find(m_threeLanes, connection) != m_threeLanes.end();
		const int laneCount = threeLanes ? static_cast<int>(kLaneCount) : 1;
		if (m_api->GetConnectionRealTimeStatus(connection, nullptr, laneCount, lanes) != k_EResultOK) return false;
		for (std::size_t i = 0; i < kLaneCount; ++i)
		{
			out[i] = {};
			if (static_cast<int>(i) >= laneCount) continue;
			out[i].pendingUnreliable = lanes[i].m_cbPendingUnreliable;
			out[i].pendingReliable = lanes[i].m_cbPendingReliable;
			out[i].sentUnackedReliable = lanes[i].m_cbSentUnackedReliable;
			out[i].queueTimeUs = lanes[i].m_usecQueueTime;
		}
		return true;
	}
}
