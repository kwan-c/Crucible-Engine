/*************************************************************************
* \file:         LobbyServerProtocol.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          21 November 2026
* \version       1.1
* \brief
*                The messages between the engine and the LAN connection
*                server (ConnectionServer/): lobby registration,
*                heartbeats, unregistration and lobby lists. Shared by
*                both sides; header-only, no engine or socket code.
*
* \details
*                The connection server only keeps a list of lobbies. It
*                never takes part in a game: a client that picks a lobby
*                joins the host directly over GameNetworkingSockets, as
*                with LAN discovery.
*
*                Every packet: "CRLS", u8 kind, u8 format version, then the
*                kind's fields, little-endian (ByteStream.h). Strings are
*                u8 length + bytes. Addresses are IPv4, 4 bytes, most
*                significant first; in memory a uint32_t in host order
*                (192.168.0.5 = 0xC0A80005).
*
*                  Register     host -> server   u32 request, u64 lobby,
*                               u64 owner token, u32 protocol version,
*                               u16 game port, u8 players, u8 max, u8 open,
*                               name, status, u8 count (<= 3) x {u32 address,
*                               u8 prefix length}. Also the heartbeat: the
*                               same message again (insert or update).
*                  Registered   server -> host   u32 request, u64 lobby,
*                               u8 result, u16 heartbeat ms, u16 lifetime ms
*                  Unregister   host -> server   u64 lobby, u64 owner token
*                  ListRequest  client -> server u32 request, u32 protocol
*                  ListReply    server -> client u32 request, u16 total,
*                               u8 truncated, u8 count, count x entry:
*                               {u64 lobby, u32 observed address, u8 flags
*                               (1 open, 2 observed is loopback), u16 game
*                               port, u8 players, u8 max, u16 age ms, name,
*                               status, u8 count x {u32 address, u8 prefix}}
*                  StatusRequest anyone -> server u32 request
*                  StatusReply  server -> asker  u32 request, u32 uptime s,
*                               u16 lobbies, u16 max lobbies, u32 lifetime
*                               ms, u32 process id (an editor that started
*                               the server checks it is the same process)
*
*                Rules:
*                  - Every packet is at most kMaxPacket bytes (below any
*                    normal MTU: never fragmented). A ListReply is a whole
*                    snapshot, never a page: it lists as many lobbies as fit
*                    and says how many matched (total, truncated).
*                  - Decoders accept a message only if every field is within
*                    its limit and nothing is left over; text is cleaned of
*                    control characters.
*                  - The owner token stops other machines on the LAN from
*                    changing or removing a lobby by accident or casually; it
*                    is not authentication (it travels unencrypted).
*                  - Kinds 16..31 are reserved for internet signaling later.
*                    Nothing here implements them.
*
* \history
*    -v1.0       format 1 (LAN connection server)
*    -v1.1       StatusRequest / StatusReply (monitoring; additive, still format 1);
*                kListAttempts (a browser gives up after 5 unanswered list requests)
*
**************************************************************************/
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Network/ByteStream.h"

namespace Crucible::LobbyServer
{
	inline constexpr std::array<std::byte, 4> kMagic{ std::byte{ 'C' }, std::byte{ 'R' }, std::byte{ 'L' }, std::byte{ 'S' } };
	inline constexpr std::uint8_t  kFormat = 1;
	inline constexpr std::uint16_t kDefaultPort = 27018;
	inline constexpr std::size_t   kMaxPacket = 1200;
	inline constexpr std::size_t   kMaxName = 32;
	inline constexpr std::size_t   kMaxStatus = 32;
	inline constexpr std::size_t   kMaxAddresses = 3;
	inline constexpr std::size_t   kHeaderBytes = 6;

	// Timing (both sides; the server tells hosts its own heartbeat and lifetime).
	inline constexpr std::chrono::milliseconds kRegisterRetry{ 1000 };     // until the first Registered
	inline constexpr std::chrono::milliseconds kHeartbeat{ 2000 };         // afterwards
	inline constexpr std::chrono::milliseconds kDefaultLifetime{ 7000 };   // server: no heartbeat for this long = gone
	inline constexpr std::chrono::milliseconds kListInterval{ 1000 };      // client: one list request a second
	inline constexpr std::chrono::milliseconds kListTimeout{ 1000 };       // client: no reply = retry
	inline constexpr std::chrono::milliseconds kBrowseUnreachable{ 3000 }; // monitor: no status reply for this long
	inline constexpr int                       kListAttempts = 5;          // browser: unanswered list requests in a row = Unreachable
	inline constexpr std::chrono::milliseconds kHostUnreachable{ 6000 };   // host: no Registered for this long

	enum class Kind : std::uint8_t
	{
		Register = 1,
		Registered = 2,
		Unregister = 3,
		ListRequest = 4,
		ListReply = 5,
		StatusRequest = 6,   // "are you there, how are you": monitoring (the editor's Connection Server tab)
		StatusReply = 7,
		// 16..31: reserved for internet signaling (not implemented)
	};

	enum class RegisterResult : std::uint8_t
	{
		Ok = 0,
		IdInUse = 1,             // another host registered this lobby id (different token): draw a new id
		ServerFull = 2,          // the server lists as many lobbies as it will
		TooManyFromAddress = 3,  // this address has as many lobbies as it may
		BadRequest = 4,          // a field out of range
	};

	/// An IPv4 address and how many leading bits name its network (24 = 255.255.255.0).
	struct HostAddress
	{
		std::uint32_t ipv4 = 0;   // host order
		std::uint8_t  prefix = 0; // 0..32
		bool operator==(const HostAddress&) const = default;
	};

	struct RegisterMsg
	{
		std::uint32_t request = 0;
		std::uint64_t lobby = 0;          // non-zero
		std::uint64_t ownerToken = 0;     // non-zero
		std::uint32_t protocolVersion = 0;
		std::uint16_t gamePort = 0;       // non-zero
		std::uint8_t  players = 0;
		std::uint8_t  maxPlayers = 0;     // non-zero, >= players
		bool          open = true;
		std::string   name;               // <= kMaxName
		std::string   status;             // <= kMaxStatus
		std::vector<HostAddress> addresses;   // the host's own, <= kMaxAddresses, best first
	};

	struct RegisteredMsg
	{
		std::uint32_t  request = 0;
		std::uint64_t  lobby = 0;
		RegisterResult result = RegisterResult::Ok;
		std::uint16_t  heartbeatMs = 0;
		std::uint16_t  lifetimeMs = 0;
	};

	struct UnregisterMsg
	{
		std::uint64_t lobby = 0;
		std::uint64_t ownerToken = 0;
	};

	struct ListRequestMsg
	{
		std::uint32_t request = 0;
		std::uint32_t protocolVersion = 0;   // only lobbies of this version are listed
	};

	struct StatusRequestMsg
	{
		std::uint32_t request = 0;
	};

	struct StatusReplyMsg
	{
		std::uint32_t request = 0;
		std::uint32_t uptimeSeconds = 0;
		std::uint16_t lobbies = 0;
		std::uint16_t maxLobbies = 0;
		std::uint32_t lifetimeMs = 0;
		std::uint32_t processId = 0;
	};

	struct ListEntry
	{
		std::uint64_t lobby = 0;
		std::uint32_t observed = 0;          // the source address of the host's Register
		bool          observedLoopback = false;
		bool          open = true;
		std::uint16_t gamePort = 0;
		std::uint8_t  players = 0;
		std::uint8_t  maxPlayers = 0;
		std::uint16_t ageMs = 0;             // since its last heartbeat
		std::string   name;
		std::string   status;
		std::vector<HostAddress> addresses;  // what the host reported
	};

	struct ListReplyMsg
	{
		std::uint32_t request = 0;
		std::uint16_t total = 0;             // lobbies that matched the request
		bool          truncated = false;     // fewer entries than total: the rest did not fit
		std::vector<ListEntry> entries;
	};

	// ---- helpers ---------------------------------------------------------------------------

	inline bool IsLoopback(std::uint32_t ipv4) noexcept { return (ipv4 >> 24) == 127; }

	inline bool InSubnet(std::uint32_t ipv4, const HostAddress& network) noexcept
	{
		if (network.prefix == 0 || network.prefix > 32) return false;
		const std::uint32_t mask = network.prefix == 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> network.prefix);
		return (ipv4 & mask) == (network.ipv4 & mask);
	}

	inline bool InAnySubnet(std::uint32_t ipv4, std::span<const HostAddress> networks) noexcept
	{
		return std::ranges::any_of(networks, [&](const HostAddress& n) { return InSubnet(ipv4, n); });
	}

	/// The address a client joins a listed lobby at. Deterministic for the same inputs:
	///   1. the observed address, if it is not loopback and is on one of the client's networks;
	///   2. else the first address the host reported that is on one of the client's networks;
	///   3. else, if the observed address is loopback (the host runs on the server's PC), the
	///      address this client reaches the server at (127.0.0.1 when all three share a PC);
	///   4. else the observed address (not on a known network: the best guess).
	inline std::uint32_t SelectHostAddress(const ListEntry& entry, std::span<const HostAddress> clientNetworks,
		std::uint32_t serverAddress) noexcept
	{
		if (!entry.observedLoopback && InAnySubnet(entry.observed, clientNetworks)) return entry.observed;
		for (const HostAddress& a : entry.addresses)
			if (!IsLoopback(a.ipv4) && InAnySubnet(a.ipv4, clientNetworks)) return a.ipv4;
		if (entry.observedLoopback) return serverAddress;
		return entry.observed;
	}

	inline std::string AddressText(std::uint32_t ipv4)
	{
		return std::to_string(ipv4 >> 24) + "." + std::to_string((ipv4 >> 16) & 0xFF) + "." + std::to_string((ipv4 >> 8) & 0xFF) + "." +
			std::to_string(ipv4 & 0xFF);
	}

	// ---- encoding ----------------------------------------------------------------------------

	namespace Detail
	{
		inline void Header(ByteWriter& w, Kind kind)
		{
			w.Bytes(kMagic);
			w.U8(static_cast<std::uint8_t>(kind));
			w.U8(kFormat);
		}

		inline void Text(ByteWriter& w, const std::string& text, std::size_t limit)
		{
			const std::size_t n = std::min(text.size(), limit);
			w.U8(static_cast<std::uint8_t>(n));
			w.Bytes(std::as_bytes(std::span(text.data(), n)));
		}

		inline bool ReadText(ByteReader& r, std::string& out, std::size_t limit)
		{
			const std::uint8_t n = r.U8();
			if (!r.Ok() || n > limit) return false;
			const std::span<const std::byte> bytes = r.Sub(n);
			if (!r.Ok()) return false;
			out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			for (char& c : out)
				if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = '?';   // shown on screens
			return true;
		}

		inline bool ReadBool(ByteReader& r, bool& out)
		{
			const std::uint8_t v = r.U8();
			out = v == 1;
			return r.Ok() && v <= 1;
		}

		inline void Addresses(ByteWriter& w, const std::vector<HostAddress>& addresses)
		{
			const std::size_t n = std::min(addresses.size(), kMaxAddresses);
			w.U8(static_cast<std::uint8_t>(n));
			for (std::size_t i = 0; i < n; ++i)
			{
				w.U32(addresses[i].ipv4);
				w.U8(addresses[i].prefix);
			}
		}

		inline bool ReadAddresses(ByteReader& r, std::vector<HostAddress>& out)
		{
			out.clear();
			const std::uint8_t n = r.U8();
			if (!r.Ok() || n > kMaxAddresses) return false;
			for (std::uint8_t i = 0; i < n; ++i)
			{
				HostAddress a;
				a.ipv4 = r.U32();
				a.prefix = r.U8();
				if (!r.Ok() || a.prefix > 32) return false;
				out.push_back(a);
			}
			return true;
		}

		inline bool Fits(const ByteWriter& w) { return w.Ok() && w.Size() <= kMaxPacket; }
	}

	/// Bytes one list entry takes on the wire.
	inline std::size_t EntryBytes(const ListEntry& e) noexcept
	{
		return 8 + 4 + 1 + 2 + 1 + 1 + 2 + 1 + std::min(e.name.size(), kMaxName) + 1 + std::min(e.status.size(), kMaxStatus) + 1 +
			5 * std::min(e.addresses.size(), kMaxAddresses);
	}

	/// Bytes of a ListReply before its entries.
	inline constexpr std::size_t kListReplyHeaderBytes = kHeaderBytes + 4 + 2 + 1 + 1;

	/// Reads the header. False for anything that is not this protocol's (other format too).
	inline bool ReadHeader(ByteReader& r, Kind& kind)
	{
		const std::span<const std::byte> magic = r.Sub(4);
		const std::uint8_t k = r.U8();
		const std::uint8_t format = r.U8();
		if (!r.Ok() || !std::equal(magic.begin(), magic.end(), kMagic.begin()) || format != kFormat) return false;
		kind = static_cast<Kind>(k);
		return true;
	}

	inline bool Encode(ByteWriter& w, const RegisterMsg& m)
	{
		if (m.addresses.size() > kMaxAddresses || m.name.size() > kMaxName || m.status.size() > kMaxStatus) return false;
		Detail::Header(w, Kind::Register);
		w.U32(m.request);
		w.U64(m.lobby);
		w.U64(m.ownerToken);
		w.U32(m.protocolVersion);
		w.U16(m.gamePort);
		w.U8(m.players);
		w.U8(m.maxPlayers);
		w.U8(m.open ? 1 : 0);
		Detail::Text(w, m.name, kMaxName);
		Detail::Text(w, m.status, kMaxStatus);
		Detail::Addresses(w, m.addresses);
		return Detail::Fits(w);
	}

	inline bool Decode(ByteReader& r, RegisterMsg& m)
	{
		m.request = r.U32();
		m.lobby = r.U64();
		m.ownerToken = r.U64();
		m.protocolVersion = r.U32();
		m.gamePort = r.U16();
		m.players = r.U8();
		m.maxPlayers = r.U8();
		return Detail::ReadBool(r, m.open) && Detail::ReadText(r, m.name, kMaxName) && Detail::ReadText(r, m.status, kMaxStatus) &&
			Detail::ReadAddresses(r, m.addresses) && r.AtEnd();
	}

	inline bool Encode(ByteWriter& w, const RegisteredMsg& m)
	{
		Detail::Header(w, Kind::Registered);
		w.U32(m.request);
		w.U64(m.lobby);
		w.U8(static_cast<std::uint8_t>(m.result));
		w.U16(m.heartbeatMs);
		w.U16(m.lifetimeMs);
		return Detail::Fits(w);
	}

	inline bool Decode(ByteReader& r, RegisteredMsg& m)
	{
		m.request = r.U32();
		m.lobby = r.U64();
		const std::uint8_t result = r.U8();
		m.heartbeatMs = r.U16();
		m.lifetimeMs = r.U16();
		m.result = static_cast<RegisterResult>(result);
		return r.Ok() && r.AtEnd() && result <= static_cast<std::uint8_t>(RegisterResult::BadRequest);
	}

	inline bool Encode(ByteWriter& w, const UnregisterMsg& m)
	{
		Detail::Header(w, Kind::Unregister);
		w.U64(m.lobby);
		w.U64(m.ownerToken);
		return Detail::Fits(w);
	}

	inline bool Decode(ByteReader& r, UnregisterMsg& m)
	{
		m.lobby = r.U64();
		m.ownerToken = r.U64();
		return r.Ok() && r.AtEnd();
	}

	inline bool Encode(ByteWriter& w, const ListRequestMsg& m)
	{
		Detail::Header(w, Kind::ListRequest);
		w.U32(m.request);
		w.U32(m.protocolVersion);
		return Detail::Fits(w);
	}

	inline bool Decode(ByteReader& r, ListRequestMsg& m)
	{
		m.request = r.U32();
		m.protocolVersion = r.U32();
		return r.Ok() && r.AtEnd();
	}

	inline bool Encode(ByteWriter& w, const StatusRequestMsg& m)
	{
		Detail::Header(w, Kind::StatusRequest);
		w.U32(m.request);
		return Detail::Fits(w);
	}

	inline bool Decode(ByteReader& r, StatusRequestMsg& m)
	{
		m.request = r.U32();
		return r.Ok() && r.AtEnd();
	}

	inline bool Encode(ByteWriter& w, const StatusReplyMsg& m)
	{
		if (m.lobbies > m.maxLobbies) return false;
		Detail::Header(w, Kind::StatusReply);
		w.U32(m.request);
		w.U32(m.uptimeSeconds);
		w.U16(m.lobbies);
		w.U16(m.maxLobbies);
		w.U32(m.lifetimeMs);
		w.U32(m.processId);
		return Detail::Fits(w);
	}

	inline bool Decode(ByteReader& r, StatusReplyMsg& m)
	{
		m.request = r.U32();
		m.uptimeSeconds = r.U32();
		m.lobbies = r.U16();
		m.maxLobbies = r.U16();
		m.lifetimeMs = r.U32();
		m.processId = r.U32();
		return r.Ok() && r.AtEnd() && m.lobbies <= m.maxLobbies;
	}

	/// False (nothing usable written) if the entries do not fit kMaxPacket: the server sizes
	/// its replies with EntryBytes first.
	inline bool Encode(ByteWriter& w, const ListReplyMsg& m)
	{
		if (m.entries.size() > 255) return false;
		Detail::Header(w, Kind::ListReply);
		w.U32(m.request);
		w.U16(m.total);
		w.U8(m.truncated ? 1 : 0);
		w.U8(static_cast<std::uint8_t>(m.entries.size()));
		for (const ListEntry& e : m.entries)
		{
			w.U64(e.lobby);
			w.U32(e.observed);
			w.U8(static_cast<std::uint8_t>((e.open ? 1 : 0) | (e.observedLoopback ? 2 : 0)));
			w.U16(e.gamePort);
			w.U8(e.players);
			w.U8(e.maxPlayers);
			w.U16(e.ageMs);
			Detail::Text(w, e.name, kMaxName);
			Detail::Text(w, e.status, kMaxStatus);
			Detail::Addresses(w, e.addresses);
		}
		return Detail::Fits(w);
	}

	inline bool Decode(ByteReader& r, ListReplyMsg& m)
	{
		m.entries.clear();
		m.request = r.U32();
		m.total = r.U16();
		if (!Detail::ReadBool(r, m.truncated)) return false;
		const std::uint8_t count = r.U8();
		if (!r.Ok() || count > m.total) return false;
		for (std::uint8_t i = 0; i < count; ++i)
		{
			ListEntry e;
			e.lobby = r.U64();
			e.observed = r.U32();
			const std::uint8_t flags = r.U8();
			e.gamePort = r.U16();
			e.players = r.U8();
			e.maxPlayers = r.U8();
			e.ageMs = r.U16();
			if (!r.Ok() || (flags & ~3u) != 0 || e.lobby == 0) return false;
			e.open = (flags & 1u) != 0;
			e.observedLoopback = (flags & 2u) != 0;
			if (!Detail::ReadText(r, e.name, kMaxName) || !Detail::ReadText(r, e.status, kMaxStatus) || !Detail::ReadAddresses(r, e.addresses))
				return false;
			m.entries.push_back(std::move(e));
		}
		return r.AtEnd();
	}
}
