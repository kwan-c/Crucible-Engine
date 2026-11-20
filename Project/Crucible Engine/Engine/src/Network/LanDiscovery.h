/*************************************************************************
* \file:         LanDiscovery.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          21 November 2026
* \version       1.0
* \brief
*                Finding sessions on the local network: hosts answer, and
*                a lobby browser lists who answered. Plain UDP broadcast,
*                beside GameNetworkingSockets, not through it.
*
* \details
*                Joining still goes through GNS (NetworkSession::Start as a
*                client, to the address and port a host gave here). This
*                only answers "who is hosting near me?":
*
*                  browser                          host
*                  Query (broadcast and 127.0.0.1)
*                     every kQueryInterval  ---->   listening on the
*                                                   discovery port
*                                     <----------   Reply (to the sender):
*                                                   game port, players,
*                                                   open, name, status
*
*                A host is listed for kLobbyExpiry after its last reply: one
*                that closed, crashed or left the network drops off the list
*                by itself, and a browser never joins an entry it has not
*                heard from recently (Age()).
*
*                One host reached two ways (broadcast and loopback, on the
*                same PC) is listed once - replies carry a random instance
*                id per session - under the loopback address if it has one.
*
*                Packets: "CRLD", u8 kind (1 query, 2 reply), u8 format
*                version, then for a reply: u64 instance, u32 protocol
*                version, u16 game port, u8 players, u8 max players, u8
*                flags (1 = open), u8 name length (<= 32), name, u8 status
*                length (<= 64), status. Anything else is ignored. Nothing
*                in a reply is trusted beyond showing it: the join itself
*                is refused by the host if it is full or closed.
*
*                Main thread only; Pump does all the work, without
*                blocking. Windows sockets (the engine's only platform).
*
* \history
*    -v1.0       hosts answer, browsers list (Networking C# lobby browser)
*
**************************************************************************/
#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "EngineExport.h"

namespace Crucible
{
	/// The UDP port hosts listen on for lobby queries (the game itself uses kDefaultNetPort).
	inline constexpr std::uint16_t kDefaultDiscoveryPort = 27019;

	/// What a host tells browsers about its session.
	struct LobbyAdvert
	{
		std::uint64_t instance = 0;          // random per session: tells two hosts apart
		std::uint32_t protocolVersion = 0;   // kProtocolVersion: a browser greys out others
		std::uint16_t gamePort = 0;          // where to join it (GNS)
		std::uint8_t  players = 0;           // the host included
		std::uint8_t  maxPlayers = 0;
		bool          open = true;           // taking players (the game decides; full is separate)
		std::string   name;                  // the host's player name, <= 32 bytes
		std::string   status;                // "In lobby", "In match"..., <= 64 bytes
	};

	/// One host a browser heard from.
	struct LobbyInfo
	{
		LobbyAdvert   advert;
		std::string   address;               // the IP to join it at ("127.0.0.1" on this PC)
		std::chrono::steady_clock::time_point lastHeard{};

		/// Seconds since its last reply.
		float Age(std::chrono::steady_clock::time_point now) const noexcept
		{
			return std::chrono::duration<float>(now - lastHeard).count();
		}
	};

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members of an exported class: used only inside Engine.dll
#endif

	class ENGINE_API LanDiscovery
	{
	public:
		static constexpr std::chrono::milliseconds kQueryInterval{ 1000 };
		static constexpr std::chrono::milliseconds kLobbyExpiry{ 3500 };
		static constexpr std::size_t               kMaxLobbies = 32;

		LanDiscovery() = default;
		~LanDiscovery();
		LanDiscovery(const LanDiscovery&) = delete;
		LanDiscovery& operator=(const LanDiscovery&) = delete;

		/// Host: answer queries on `port` with `advert` (refreshed by every call). False (and not
		/// answering) if the port cannot be opened; `error` says why. Port 0: stop answering.
		bool Advertise(std::uint16_t port, const LobbyAdvert& advert, std::string& error);
		void StopAdvertising() noexcept;
		bool IsAdvertising() const noexcept { return m_hostSocket != kNoSocket; }

		/// Browser: ask the network for hosts on `port`, every kQueryInterval, until StopBrowsing.
		/// The first query goes out at the next Pump. False (not browsing) if no socket opens.
		bool StartBrowsing(std::uint16_t port, std::string& error);
		void StopBrowsing() noexcept;
		bool IsBrowsing() const noexcept { return m_browseSocket != kNoSocket; }

		/// Hosts heard from within kLobbyExpiry, in the order first heard. Updated by Pump only.
		std::span<const LobbyInfo> Lobbies() const noexcept { return m_lobbies; }

		/// Answers queries, sends queries when due, reads replies, drops expired hosts.
		void Pump(std::chrono::steady_clock::time_point now);

	private:
		static constexpr std::uintptr_t kNoSocket = ~std::uintptr_t{ 0 };

		void AnswerQueries();
		void ReadReplies(std::chrono::steady_clock::time_point now);

		std::uintptr_t         m_hostSocket = kNoSocket;
		std::uintptr_t         m_browseSocket = kNoSocket;
		std::uint16_t          m_hostPort = 0;
		std::uint16_t          m_browsePort = 0;
		LobbyAdvert            m_advert;
		std::chrono::steady_clock::time_point m_nextQuery{};
		std::vector<LobbyInfo> m_lobbies;
		bool                   m_winsock = false;   // WSAStartup done (one per object)
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
