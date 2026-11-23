/*************************************************************************
* \file:         LobbyServerClient.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          21 November 2026
* \version       1.1
* \brief
*                The engine's side of the LAN connection server
*                (ConnectionServer/): a host registers its lobby and
*                keeps it alive; a browser asks for the lobby list.
*
* \details
*                Separate from the game connection: its own UDP socket, and
*                nothing the session needs to run waits for it. Sends never
*                block and are never retried beyond the rules below; a server
*                that is down only shows in Status().
*
*                Host (Host / StopHosting):
*                  Register at once, then every kRegisterRetry (1 s) until the
*                  server answers, then every kHeartbeat (2 s). Registering is
*                  "insert or update", so a lost or doubled one is harmless and
*                  a restarted server has the lobby back within one heartbeat.
*                  IdInUse (another host has this lobby id) is reported once
*                  through TakeIdConflict: the session draws a new id.
*                  StopHosting sends Unregister twice and forgets the lobby; if
*                  both are lost the server expires it.
*
*                Browser (StartBrowsing / StopBrowsing):
*                  One ListRequest at a time. A new one goes out kListInterval
*                  (1 s) after the last reply, or as soon as the outstanding one
*                  has waited kListTimeout (1 s). Only a reply to the outstanding
*                  request counts; it replaces the whole list (a snapshot).
*                  kListAttempts (5) requests in a row unanswered (about 5 s):
*                  Unreachable, and the server's list is dropped. It keeps
*                  asking, and the first reply makes it Online again.
*
*                Monitor (StartMonitoring / StopMonitoring): a StatusRequest
*                every kListInterval, one at a time like the list; the last
*                reply is ServerStatus(). The editor's Connection Server tab.
*
*                Each listed lobby's join address is chosen by
*                LobbyServer::SelectHostAddress from this machine's own
*                networks (LocalNetworks), refreshed every few seconds.
*
*                Main thread only; Pump does the work.
*
* \history
*    -v1.0       LAN connection server client
*    -v1.1       monitoring: StartMonitoring, ServerStatus (StatusRequest / StatusReply);
*                the browser is Unreachable after kListAttempts unanswered requests (FailedListAttempts)
*
**************************************************************************/
#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "EngineExport.h"
#include "Network/LanDiscovery.h"         // LobbyInfo
#include "Network/LobbyServerProtocol.h"

namespace Crucible
{
	enum class LobbyServerStatus : std::uint8_t
	{
		Off,          // no server set, or nothing asked of it (not hosting, not browsing)
		Connecting,   // asked, no answer yet
		Online,       // answering
		Unreachable,  // not answering
	};

	ENGINE_API const char* ToString(LobbyServerStatus status) noexcept;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members of an exported class: used only inside Engine.dll
#endif

	class ENGINE_API LobbyServerClient
	{
	public:
		using Clock = std::chrono::steady_clock;

		LobbyServerClient() = default;
		~LobbyServerClient();
		LobbyServerClient(const LobbyServerClient&) = delete;
		LobbyServerClient& operator=(const LobbyServerClient&) = delete;

		/// A non-zero random number from the operating system's secure generator (BCryptGenRandom):
		/// lobby ids and owner tokens. False only if Windows refuses.
		static bool SecureRandom64(std::uint64_t& out) noexcept;

		/// This PC's IPv4 networks (adapters that are up; no loopback, no 169.254): wired and Wi-Fi
		/// adapters first, then the rest, each group by address - the same order every time.
		static std::vector<LobbyServer::HostAddress> LocalNetworks();

		/// Where the server is: an IPv4 address ("" = none) and port. A change drops what was
		/// learned from the old one (the lobby is registered again with the new one).
		void SetServer(const std::string& address, std::uint16_t port);
		bool HasServer() const noexcept { return m_serverIp != 0; }
		std::uint32_t ServerIp() const noexcept { return m_serverIp; }
		std::uint16_t ServerPort() const noexcept { return m_serverPort; }

		// ---- host ---------------------------------------------------------------------------

		/// Registers this lobby, or updates it (call every frame with the live numbers; the
		/// request number is managed here). A different lobby id starts a new registration.
		void Host(const LobbyServer::RegisterMsg& registration);

		/// Sends Unregister (twice) and forgets the lobby. `unregister` false: just forget it, as
		/// if the host crashed (tests).
		void StopHosting(bool unregister = true) noexcept;
		bool IsHosting() const noexcept { return m_hosting; }
		bool IsRegistered() const noexcept { return m_registered; }

		/// True once after the server answered IdInUse for the current lobby id.
		bool TakeIdConflict() noexcept;

		// ---- browser ------------------------------------------------------------------------

		/// protocolVersion: only lobbies of this game version are listed.
		void StartBrowsing(std::uint32_t protocolVersion);
		void StopBrowsing() noexcept;
		bool IsBrowsing() const noexcept { return m_browsing; }

		/// The latest snapshot, with join addresses chosen (sources = kLobbyFromServer).
		std::span<const LobbyInfo> Lobbies() const noexcept { return m_lobbies; }
		std::uint16_t ListTotal() const noexcept { return m_total; }
		bool          ListTruncated() const noexcept { return m_truncated; }
		/// List requests in a row that got no reply (0 after a reply). kListAttempts or more:
		/// Status() is Unreachable. While fewer, the one in flight is attempt FailedListAttempts() + 1.
		int FailedListAttempts() const noexcept { return m_failedLists; }

		// ---- monitor ------------------------------------------------------------------------

		/// Asks the server how it is, every second, until StopMonitoring.
		void StartMonitoring();
		void StopMonitoring() noexcept;
		bool IsMonitoring() const noexcept { return m_monitoring; }
		/// The latest StatusReply; HasServerStatus() false until one arrived (cleared when the
		/// server stops answering, or by SetServer / StopMonitoring).
		bool HasServerStatus() const noexcept { return m_monitorAnswered && m_hasStatus; }
		const LobbyServer::StatusReplyMsg& ServerStatus() const noexcept { return m_status; }

		// ---- both ---------------------------------------------------------------------------

		void Pump(Clock::time_point now);
		LobbyServerStatus Status(Clock::time_point now) const noexcept;

	private:
		bool OpenSocket();
		void CloseSocket() noexcept;
		void Send(std::span<const std::byte> bytes) noexcept;
		void ReadReplies(Clock::time_point now);
		void OnListReply(const LobbyServer::ListReplyMsg& reply, Clock::time_point now);

		std::uintptr_t m_socket = ~std::uintptr_t{ 0 };
		bool           m_winsock = false;
		std::uint32_t  m_serverIp = 0;
		std::uint16_t  m_serverPort = 0;
		std::uint32_t  m_nextRequest = 1;

		// Host
		bool                       m_hosting = false;
		bool                       m_registered = false;   // the server answered Ok for this lobby
		bool                       m_idConflict = false;
		LobbyServer::RegisterMsg   m_registration;
		Clock::time_point          m_nextRegister{};
		Clock::time_point          m_hostWaitingSince{};   // first unanswered Register; {} = none
		Clock::time_point          m_lastHostReply{};

		// Browser
		bool                       m_browsing = false;
		std::uint32_t              m_protocolVersion = 0;
		std::uint32_t              m_outstanding = 0;      // the one ListRequest awaiting its reply
		Clock::time_point          m_outstandingSince{};
		Clock::time_point          m_nextList{};
		Clock::time_point          m_browseWaitingSince{}; // since the last reply (or the start)
		bool                       m_browseAnswered = false;
		int                        m_failedLists = 0;      // unanswered list requests in a row
		std::vector<LobbyInfo>     m_lobbies;
		std::uint16_t              m_total = 0;
		bool                       m_truncated = false;
		std::vector<LobbyServer::HostAddress> m_networks;  // this PC's, for choosing join addresses
		Clock::time_point          m_networksAt{};

		// Monitor
		bool                       m_monitoring = false;
		std::uint32_t              m_statusOutstanding = 0;
		Clock::time_point          m_statusSince{};
		Clock::time_point          m_nextStatus{};
		Clock::time_point          m_monitorWaitingSince{};
		bool                       m_monitorAnswered = false;
		bool                       m_hasStatus = false;
		LobbyServer::StatusReplyMsg m_status;
		bool                       Idle() const noexcept { return !m_hosting && !m_browsing && !m_monitoring; }
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
