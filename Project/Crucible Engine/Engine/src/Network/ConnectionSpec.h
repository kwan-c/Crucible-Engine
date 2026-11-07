/*************************************************************************
* \file:         ConnectionSpec.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                How to listen and how to connect: the only connectivity-
*                specific types above the transport.
*
* \details
*                Authority (who decides what is true) never changes; how
*                the packets get there does. Phase A (now) is direct IP on
*                a LAN. Phase B (later) is internet peer-to-peer through
*                GNS's ICE support, which needs a signaling service.
*
*                Both paths end in the same thing, a connection handle, so
*                everything above GnsTransport (session, protocol, ECS
*                systems) is identical for both. Adding Phase B means
*                filling in the P2PEndpoint branch inside GnsTransport.cpp
*                and nothing else (Networking Guide 19.4).
*
* \history
*    -v1.0       IpEndpoint, P2PEndpoint (reserved), ListenSpec,
*                ConnectSpec with optional timeouts
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace Crucible
{
	inline constexpr std::uint16_t kDefaultNetPort = 27020;

	/// Phase A: a plain IP address and UDP port.
	struct IpEndpoint
	{
		/// Connect: the host's address ("192.168.1.20", "::1"). Listen: empty = every local
		/// interface (IPv4 and IPv6); an address = only that interface ("127.0.0.1" = this
		/// machine only).
		std::string   address;
		std::uint16_t port = kDefaultNetPort;
	};

	/// Phase B (not implemented): a peer identity reached through a signaling service.
	/// Listen and Connect refuse it with "internet play is not available in this build".
	struct P2PEndpoint
	{
		std::string peerIdentity;   // e.g. "str:host-7f3a..."
		int         virtualPort = 0;
	};

	/// How long a connection may stay silent before GNS gives up on it. 0 = GNS default
	/// (10 s to connect, 10 s of silence once connected).
	struct ConnectionTimeouts
	{
		int initialMs   = 0;   // connecting: handshake not finished
		int connectedMs = 0;   // connected: nothing received
	};

	struct ListenSpec
	{
		std::variant<IpEndpoint, P2PEndpoint> how = IpEndpoint{};
		ConnectionTimeouts                    timeouts;   // applied to every accepted connection
	};

	struct ConnectSpec
	{
		std::variant<IpEndpoint, P2PEndpoint> to = IpEndpoint{};
		ConnectionTimeouts                    timeouts;
	};
}
