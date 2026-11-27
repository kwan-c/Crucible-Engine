/*************************************************************************
* \file:         Protocol.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.10
* \brief
*                The wire protocol between the host and its clients:
*                message types, their fields, and an Encode/Decode pair
*                for every message.
*
* \details
*                Framing. A GNS message already carries its length, so
*                every message is just: u8 MessageType, then its fields,
*                little-endian (ByteStream.h). There is no per-message
*                magic or version; the version is checked once, in the
*                join handshake (JoinRequest.protocolVersion), together
*                with the content hash (CanonicalHash.h).
*
*                Decoding rules:
*                  - Decode* reads a message whose type byte was already
*                    consumed (ReadMessageType), and returns true only if
*                    the WHOLE message is well-formed: every count within
*                    its limit, every id non-zero and unique, every length
*                    inside the message, and no trailing bytes. On false,
*                    the output is unspecified and must not be used.
*                  - Decoders never touch the World. A handler decodes and
*                    validates everything first, then applies, so a bad
*                    record can never leave a message half-applied.
*                  - Views (SpawnView, RecordView, GameplayEventView) hold
*                    spans INTO the received bytes: no copies, and they
*                    must not outlive the received message.
*                  - Checks that need game data (does this prefab exist,
*                    does the mask fit it, is the state in range) are NOT
*                    here: the replication registry adds them (Phase 7).
*
*                Encoding rules: Encode* applies the same structural
*                rules as Decode*, plus MaxMessageBytes, and returns
*                false instead of writing something the receiver would
*                reject. Never aggregate-initialize these structs
*                positionally; set fields by name.
*
*                Wire layouts (after the u8 type):
*                  JoinRequest    u32 protocolVersion, u64 contentHash,
*                                 u8 nameLength (<= 32), name bytes
*                  JoinAccepted   u32 lifecycleSeq (= 1), u8 yourPeerId,
*                                 u32 serverTick, u16 tickRate
*                  WorldBegin     u32 lifecycleSeq, u64 sceneAsset, u64 sceneHash,
*                                 u32 sceneSeed
*                  WorldEnd       u32 lifecycleSeq
*                  ClientReady    u32 worldEndSeq
*                  SpawnBatch     u32 lifecycleSeq, u32 serverTick, u16 count,
*                                 count x { varint id, u16 prefab, u8 owner,
*                                           varint mask, varint length, state }
*                  DespawnBatch   u32 lifecycleSeq, u16 count, count x varint id
*                  SceneObjectBatch u32 lifecycleSeq, u32 serverTick, u16 count,
*                                 count x { varint id, u64 sceneId, u16 profile,
*                                           u8 owner, varint mask, varint length,
*                                           state }
*                  Snapshot       u16 sequence, u32 serverTick, u16 count,
*                                 count x { varint id, varint mask,
*                                           varint length, body }
*                  Input          u8 count (0..3), count x { u32 sequence,
*                                 u32 clientTick, i8 moveX, i8 moveZ, u16 yaw,
*                                 u8 buttons, varint actions, varint
*                                 actionsPressed, u16 pitch }, u16 ackedSnapshot,
*                                 u32 ackBits, u32 appliedLifecycle
*                                 (12 bytes plus 17..25 per command; at most 87)
*                  GameplayEvent  u32 lifecycleSeq, u16 kind (non-zero),
*                                 varint target (a NetworkId, 0 = none),
*                                 payload = the rest of the message
*                  PeerRoster     u32 lifecycleSeq, u8 count (1..4),
*                                 count x { u8 peer, u8 flags, u8 nameLength
*                                           (<= 32), name bytes }
*                  UnreliableEvent u16 kind (non-zero), varint target, payload
*                                 = the rest (<= kMaxUnreliableEventPayload)
*                  Command        u32 sequence (non-zero), u16 kind (non-zero),
*                                 varint target (a NetworkId, 0 = none),
*                                 payload = the rest (<= kMaxCommandPayload)
*                  SceneChange    u32 lifecycleSeq, u32 transition (non-zero),
*                                 u64 sceneAsset (non-zero), u32 sceneSeed,
*                                 varint keptCount (<= 64), keptCount x varint
*                                 id (non-zero, unique; protocol 12)
*                  SceneLoaded    u32 transition (non-zero)
*                  OwnerChange    u32 lifecycleSeq, varint id (non-zero),
*                                 u8 owner (0 = host, 1..kMaxClients)
*                  PeerPings      u8 count (1..4), count x { u8 peer, u16 pingMs
*                                 (0xFFFF = unknown) }, peers unique
*
*                Lifecycle sequence: every reliable lane-0 message the
*                host sends a client (JoinAccepted, WorldBegin, SpawnBatch,
*                SceneObjectBatch, DespawnBatch, WorldEnd, GameplayEvent,
*                PeerRoster, SceneChange) carries a per-client counter
*                starting at 1. 0 never appears on the wire.
*
*                Scene changes (protocol 8): the host loads another scene
*                and the session goes on. It tells every joined client
*                (SceneChange: the scene's asset GUID and a transition
*                number); each loads that scene from its own project,
*                answers SceneLoaded with the same number, and is then sent
*                the new scene's world exactly as when it joined (WorldBegin
*                ... WorldEnd, ClientReady).
*
*                The roster (protocol 5): who is in the session. The host
*                sends every joined client the whole list - itself (peer 0)
*                first, then the joined clients in PeerId order, each with
*                its name and whether it is in the game yet - whenever it
*                changes, so a client can show the other players and tell
*                them apart. It is always the complete list, never a change.
*
*                Commands (protocol 5): the one way gameplay on a client
*                asks the host to do something ("open this door", "I picked
*                this up"). A game-defined kind and payload, optionally
*                aimed at one network object; reliable and in order. The
*                sequence is per client, starts at 1 and only grows. The
*                host never trusts a command: it knows the sender from the
*                connection, and gameplay decides whether to obey.
*
*                Records (protocol 9): a record's mask (which entries of
*                the object's replicated list follow, bit i = entry i, at
*                most kMaxRecordEntries) and its length (at most
*                kMaxRecordBytes) are varints: one byte each for the usual
*                small record, as in protocol 8. A mask above 16 bits or a
*                length above the limit is malformed before anything is
*                read past it.
*
*                Player input (protocol 10): besides the movement intent,
*                every input command carries the player's game actions -
*                32 bits a script binds to whatever it likes (fire, use,
*                reload) - as held this frame and as pressed since the last
*                command, so a tap shorter than one send is never lost; and
*                the aim pitch next to the yaw.
*
*                Kept objects (protocol 12): a SceneChange lists the network
*                objects the host keeps through the change (KeepAcrossScenes).
*                Each client keeps its copies of them; the new world names
*                them again with the same ids, and a client that kept one
*                applies that record to its copy instead of spawning it.
*
*                Pings (protocol 11): once a second the host tells every
*                client each player's ping to the host (its own is 0), on
*                the unreliable lane: a lost one is replaced a second later.
*
*                The scene seed (protocol 9): one 32-bit number the host
*                picks for every scene of the session - at random, or the
*                one its script asked for - so procedural content (a
*                generated level) comes out the same on every machine.
*                SceneChange carries it before the client loads the scene;
*                WorldBegin carries it again for a client that joins into
*                a scene already running.
*
*                Scene objects (protocol 4, Networking Phase 14): entities
*                of the loaded scene that both machines already have. The
*                world stream binds them instead of spawning them: a
*                SceneObjectBatch names each by its persistent scene id,
*                with the NetworkId the host gave it, the profile (network
*                prefab id) it replicates with, its owner and its current
*                state. WorldBegin names the scene (its asset GUID) and a
*                hash of its networked content, so a client with a
*                different scene leaves before binding anything. Both are 0
*                when the host's scene has no saved asset.
*
* \history
*    -v1.0       protocol version 3: every message type, codecs, size
*                limits, views. SpawnBatch decoding is split: structure here,
*                prefab/state checks in the replication registry later.
*    -v1.1       kMaxGameplayEventPayload (a constant; no wire change)
*    -v1.2       protocol version 4: SceneObjectBatch; WorldBegin carries the
*                scene identity (Networking Phase 14.4)
*    -v1.3       protocol version 5: PeerRoster (who is in the session, for
*                clients) and Command (client to host gameplay requests)
*    -v1.4       protocol version 6: GameplayEvent carries a target, like
*                Command (Networking C# Phase 5A)
*    -v1.5       protocol version 7: UnreliableEvent (Networking C# Phase 5B)
*    -v1.6       protocol version 8: SceneChange and SceneLoaded (Networking C# Phase 9)
*    -v1.7       protocol version 9: varint record masks and lengths (records up to
*                kMaxRecordBytes, up to 16 entries); the scene seed in WorldBegin and
*                SceneChange; OwnerChange (still protocol 9:
*                no build with protocol 9 was released before it)
*    -v1.8       protocol version 10: input commands carry game actions (held and pressed)
*                and the aim pitch
*    -v1.9       protocol version 11: PeerPings (message 18); 64 NetVariables slots and the NetData
*                serializer (ReplicationRegistry)
*    -v1.10      protocol version 12: SceneChange lists the network objects kept through the change
*
**************************************************************************/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "EngineExport.h"
#include "Network/ByteStream.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	// Bump on ANY change to the bytes of any message. (12 = kept objects in SceneChange; 11 =
	// PeerPings, 64 NetVariables slots, the
	// NetData serializer; 10 = actions and pitch in input commands;
	// 9 = varint record mask and length, the scene seed; 8 = SceneChange and SceneLoaded; 7 =
	// UnreliableEvent, and the NetVariables serializer; 6 = GameplayEvent's target; 5 = PeerRoster
	// and Command; 4 = scene objects and the scene identity in WorldBegin; 3 = lifecycle
	// sequence; versions 1 and 2 were earlier development versions.)
	inline constexpr std::uint32_t kProtocolVersion = 12;

	inline constexpr std::size_t   kMaxSnapshotBytes      = 1100;        // one MTU-sized unreliable message
	inline constexpr std::size_t   kMaxReliableBytes      = 16 * 1024;   // spawn / despawn / event messages
	inline constexpr std::size_t   kMaxGameplayEventPayload = kMaxReliableBytes - 12;  // after u8 type, u32 seq, u16 kind, varint target (<= 5)
	inline constexpr std::size_t   kMaxCommandPayload     = 1024;        // a client's request: small on purpose
	inline constexpr std::size_t   kMaxUnreliableEventPayload = kMaxSnapshotBytes - 8;   // one packet: after u8 type, u16 kind, varint target (<= 5)
	inline constexpr std::uint16_t kMaxEntitiesPerMessage = 512;
	inline constexpr std::size_t   kMaxRecordBytes        = 1024;        // one object's record (a varint length)
	inline constexpr std::size_t   kMaxRecordEntries      = 16;          // entries in one record (bits of its mask)
	inline constexpr std::size_t   kMaxPlayerNameBytes    = 32;
	inline constexpr std::size_t   kMaxInputCommands      = 3;           // each command is sent 3 times
	inline constexpr std::size_t   kMaxKeptObjects        = 64;          // network objects one scene change keeps
	inline constexpr std::uint32_t kFirstLifecycleSeq     = 1;           // JoinAccepted's; 0 = none

	/// Which entries of an object's replicated list a record carries: bit i = entry i.
	using RecordMask = std::uint16_t;
	static_assert(sizeof(RecordMask) * 8 >= kMaxRecordEntries, "a record mask needs one bit per entry");

	// The biggest record must fit one snapshot on its own: type, sequence, tick, count, then its
	// id (varint, 5), mask (varint of 16 bits, 3) and length (varint, 2).
	static_assert(1 + 2 + 4 + 2 + 5 + 3 + 2 + kMaxRecordBytes <= kMaxSnapshotBytes, "a record must fit one snapshot");

	enum class MessageType : std::uint8_t
	{
		JoinRequest   = 1,    // C->H reliable, lane 0
		JoinAccepted  = 2,    // H->C reliable, lane 0, lifecycle
		SpawnBatch    = 3,    // H->C reliable, lane 0, lifecycle
		DespawnBatch  = 4,    // H->C reliable, lane 0, lifecycle
		Snapshot      = 5,    // H->C unreliable, lane 1
		Input         = 6,    // C->H unreliable no-delay, lane 0: commands, acks, heartbeat
		GameplayEvent = 7,    // H->C reliable, lane 0, lifecycle
		WorldBegin    = 8,    // H->C reliable, lane 0, lifecycle: the full world follows
		WorldEnd      = 9,    // H->C reliable, lane 0, lifecycle: the full world was sent
		ClientReady   = 10,   // C->H reliable, lane 0: the world is applied
		SceneObjectBatch = 11,   // H->C reliable, lane 0, lifecycle: scene entities to bind (protocol 4)
		PeerRoster    = 12,   // H->C reliable, lane 0, lifecycle: everyone in the session (protocol 5)
		Command       = 13,   // C->H reliable, lane 0: a gameplay request (protocol 5)
		UnreliableEvent = 14, // H->C unreliable, lane 1: a gameplay event that may be lost (protocol 7)
		SceneChange   = 15,   // H->C reliable, lane 0, lifecycle: load this scene (protocol 8)
		SceneLoaded   = 16,   // C->H reliable, lane 0: that scene is loaded (protocol 8)
		OwnerChange   = 17,   // H->C reliable, lane 0, lifecycle: a network object has a new owner (protocol 9)
		PeerPings     = 18,   // H->C unreliable, lane 1: every player's ping to the host, once a second (protocol 11)
		Count                 // not a message; append new types above it, never renumber
	};

	// Refusals and disconnects are NOT messages: they are GNS close reasons, which the peer
	// receives in its closed event even when no data gets through.

	/// True for 1..Count-1.
	constexpr bool IsKnownMessageType(std::uint8_t raw) noexcept
	{
		return raw >= 1 && raw < static_cast<std::uint8_t>(MessageType::Count);
	}

	/// The largest encoded size (type byte included) a message of this type may have.
	/// Receivers drop a bigger message before decoding it. 0 for an unknown type.
	ENGINE_API std::size_t MaxMessageBytes(MessageType type) noexcept;

	/// Reads the leading type byte. False if it is missing or not a known MessageType.
	ENGINE_API bool ReadMessageType(ByteReader& r, MessageType& out) noexcept;

	/// 16-bit sequence comparison that survives wrap-around: 2 is newer than 65530.
	/// Valid while the two are less than 32768 apart.
	constexpr bool SequenceGreater(std::uint16_t a, std::uint16_t b) noexcept
	{
		return static_cast<std::int16_t>(static_cast<std::uint16_t>(a - b)) > 0;
	}

	// ---- join handshake ------------------------------------------------------------------

	/// At most kMaxPlayerNameBytes bytes, no control characters (bytes below 0x20, or 0x7F).
	ENGINE_API bool IsValidPlayerName(std::string_view name) noexcept;

	struct JoinRequest
	{
		std::uint32_t protocolVersion = 0;
		std::uint64_t contentHash = 0;
		std::array<char, kMaxPlayerNameBytes> name{};
		std::uint8_t  nameLength = 0;

		std::string_view Name() const noexcept { return { name.data(), nameLength }; }

		/// False (name unchanged) if IsValidPlayerName rejects it.
		bool SetName(std::string_view text) noexcept
		{
			if (!IsValidPlayerName(text)) return false;
			name = {};
			for (std::size_t i = 0; i < text.size(); ++i) name[i] = text[i];
			nameLength = static_cast<std::uint8_t>(text.size());
			return true;
		}
	};

	struct JoinAccepted
	{
		std::uint32_t lifecycleSeq = kFirstLifecycleSeq;   // always 1: it opens the lifecycle stream
		Net::PeerId   peer = Net::kInvalidPeer;            // the client's own id, 1..kMaxClients
		Net::NetTick  serverTick = 0;
		std::uint16_t tickRate = 0;                         // simulation steps per second, non-zero
	};

	/// Opens the world stream. sceneAsset and sceneHash name the host's loaded scene and its
	/// networked content (0, 0: no saved scene, so no scene objects). sceneSeed is the scene's
	/// seed (protocol 9): any value, 0 included.
	struct WorldBegin
	{
		std::uint32_t lifecycleSeq = 0;
		std::uint64_t sceneAsset = 0;
		std::uint64_t sceneHash = 0;
		std::uint32_t sceneSeed = 0;
	};
	struct WorldEnd    { std::uint32_t lifecycleSeq = 0; };
	struct ClientReady { std::uint32_t worldEndSeq = 0; };   // must equal the WorldEnd's lifecycleSeq

	/// The host loads another scene; the client loads the same one (by its asset GUID), then
	/// answers SceneLoaded with the same transition number.
	struct SceneChange
	{
		std::uint32_t lifecycleSeq = 0;
		std::uint32_t transition = 0;   // non-zero; one per scene change in a session
		std::uint64_t sceneAsset = 0;   // non-zero
		std::uint32_t sceneSeed = 0;    // the new scene's seed (protocol 9): any value
		std::array<Net::NetworkId, kMaxKeptObjects> kept{};   // kept through the change (protocol 12)
		std::uint8_t  keptCount = 0;    // 0..kMaxKeptObjects; ids non-zero and unique

		std::span<const Net::NetworkId> Kept() const noexcept { return { kept.data(), keptCount }; }
	};
	struct SceneLoaded { std::uint32_t transition = 0; };   // must equal the SceneChange's

	/// The host gave network object `id` to another player. In the lifecycle
	/// stream, after the spawns and despawns of its frame; a joining client's world already has it.
	struct OwnerChange
	{
		std::uint32_t  lifecycleSeq = 0;
		Net::NetworkId id = Net::kInvalidNetworkId;   // non-zero
		Net::PeerId    owner = Net::kServerPeer;      // 0 = the host, 1..kMaxClients = a client
	};

	/// Every player's ping to the host, as the host measures it (protocol 11).
	struct PeerPings
	{
		static constexpr std::uint16_t kUnknown = 0xFFFF;

		struct Entry
		{
			Net::PeerId   peer = Net::kInvalidPeer;   // 0 = the host (ping 0), 1..kMaxClients = a client
			std::uint16_t pingMs = kUnknown;
		};
		std::array<Entry, Net::kMaxPlayers> entries{};
		std::uint8_t count = 0;   // 1..kMaxPlayers, peers unique

		std::span<const Entry> Entries() const noexcept { return { entries.data(), count }; }
	};

	// ---- the roster (host to client, reliable) ---------------------------------------------

	/// One player in a PeerRoster.
	struct RosterEntry
	{
		static constexpr std::uint8_t kInGame     = 1u << 0;   // its world is complete: it is playing
		static constexpr std::uint8_t kKnownFlags = kInGame;

		Net::PeerId   peer = Net::kInvalidPeer;   // 0 = the host, 1..kMaxClients = a client
		std::uint8_t  flags = 0;                  // only kKnownFlags bits
		std::array<char, kMaxPlayerNameBytes> name{};
		std::uint8_t  nameLength = 0;

		std::string_view Name() const noexcept { return { name.data(), nameLength }; }
		bool             InGame() const noexcept { return (flags & kInGame) != 0; }

		/// False (name unchanged) if IsValidPlayerName rejects it.
		bool SetName(std::string_view text) noexcept
		{
			if (!IsValidPlayerName(text)) return false;
			name = {};
			for (std::size_t i = 0; i < text.size(); ++i) name[i] = text[i];
			nameLength = static_cast<std::uint8_t>(text.size());
			return true;
		}
	};

	/// Everyone in the session: the host (peer 0) first, then the joined clients, PeerIds
	/// strictly ascending. Always the whole list.
	struct PeerRoster
	{
		std::uint32_t lifecycleSeq = 0;
		std::array<RosterEntry, Net::kMaxPlayers> entries{};
		std::uint8_t  count = 0;   // 1..kMaxPlayers

		std::span<const RosterEntry> Entries() const noexcept { return { entries.data(), count }; }
	};

	// ---- input (client to host) -------------------------------------------------------------

	struct InputCommand
	{
		static constexpr std::uint8_t kJump       = 1u << 0;
		static constexpr std::uint8_t kKnownButtons = kJump;

		std::uint32_t sequence = 0;     // per client, strictly increasing
		Net::NetTick  clientTick = 0;   // the client's estimate of the server tick
		std::int8_t   moveX = 0;        // Quantize::SignedUnitToI8
		std::int8_t   moveZ = 0;
		std::uint16_t yaw = 0;          // Quantize::AngleToU16
		std::uint8_t  buttons = 0;      // only kKnownButtons bits
		std::uint32_t actions = 0;          // game actions held (bit i = action i; the script's meaning)
		std::uint32_t actionsPressed = 0;   // actions that went down since the previous command
		std::uint16_t pitch = 0;            // aim pitch, Quantize::AngleToU16 (-10 degrees = 350)
	};

	/// Also the client's heartbeat and acknowledgement carrier: sent every input interval,
	/// with count = 0 when the client owns no player.
	struct InputMessage
	{
		std::array<InputCommand, kMaxInputCommands> commands{};   // oldest first
		std::uint8_t  count = 0;
		std::uint16_t ackedSnapshot = 0;      // newest snapshot sequence received
		std::uint32_t ackBits = 0;            // bit i = snapshot (ackedSnapshot - 1 - i) received
		std::uint32_t appliedLifecycle = 0;   // newest lifecycle message fully applied

		std::span<const InputCommand> Commands() const noexcept { return { commands.data(), count }; }
	};

	// ---- entity lifecycle (host to client, reliable) ----------------------------------------

	/// One spawn record. `state` points into the message (decoding) or into the sender's
	/// own buffer (encoding).
	struct SpawnView
	{
		Net::NetworkId             id = Net::kInvalidNetworkId;
		Net::NetPrefabId           prefab = Net::kInvalidNetPrefab;
		Net::PeerId                owner = Net::kServerPeer;
		RecordMask                 mask = 0;   // which of the prefab's replicated components follow
		std::span<const std::byte> state;      // at most kMaxRecordBytes
	};

	struct SpawnBatchView
	{
		std::uint32_t          lifecycleSeq = 0;
		Net::NetTick           serverTick = 0;
		std::vector<SpawnView> spawns;   // 1..kMaxEntitiesPerMessage, unique ids
	};

	/// One scene object to bind: the client's own entity with scene id `sceneId` becomes network
	/// object `id`. `state` points into the message (decoding) or the sender's buffer (encoding).
	struct SceneObjectView
	{
		Net::NetworkId             id = Net::kInvalidNetworkId;
		std::uint64_t              sceneId = 0;   // persistent scene GUID of the entity (Scene::IdOf), non-zero
		Net::NetPrefabId           profile = Net::kInvalidNetPrefab;   // the replication profile it uses
		Net::PeerId                owner = Net::kServerPeer;
		RecordMask                 mask = 0;
		std::span<const std::byte> state;     // at most kMaxRecordBytes
	};

	struct SceneObjectBatchView
	{
		std::uint32_t                lifecycleSeq = 0;
		Net::NetTick                 serverTick = 0;
		std::vector<SceneObjectView> objects;   // 1..kMaxEntitiesPerMessage, unique ids and unique scene ids
	};

	struct DespawnBatchView
	{
		std::uint32_t               lifecycleSeq = 0;
		std::vector<Net::NetworkId> ids;   // 1..kMaxEntitiesPerMessage, unique, non-zero
	};

	struct GameplayEventView
	{
		std::uint32_t              lifecycleSeq = 0;
		std::uint16_t              kind = 0;   // non-zero; meaning defined by gameplay
		Net::NetworkId             target = Net::kInvalidNetworkId;   // the object it is about, or none
		std::span<const std::byte> payload;    // the rest of the message
	};

	/// A gameplay event sent once, unreliably: no lifecycle number, so it may be lost, arrive
	/// late, or arrive before a reliable message sent earlier. For frequent effects where the next
	/// one makes a lost one irrelevant.
	struct UnreliableEventView
	{
		std::uint16_t              kind = 0;   // non-zero
		Net::NetworkId             target = Net::kInvalidNetworkId;
		std::span<const std::byte> payload;    // at most kMaxUnreliableEventPayload
	};

	// ---- commands (client to host, reliable) -------------------------------------------------

	/// A client's gameplay request. `payload` points into the message (decoding) or into the
	/// sender's own buffer (encoding).
	struct CommandView
	{
		std::uint32_t              sequence = 0;   // per client, from 1, only grows
		std::uint16_t              kind = 0;       // non-zero; meaning defined by gameplay
		Net::NetworkId             target = Net::kInvalidNetworkId;   // the object it is about, or none
		std::span<const std::byte> payload;        // at most kMaxCommandPayload; the game's own format
	};

	// ---- snapshots (host to client, unreliable) ----------------------------------------------

	/// One entity's state in a snapshot. The length lets a client skip a record it cannot
	/// use (an entity it does not know) without losing the rest of the message.
	struct RecordView
	{
		Net::NetworkId             id = Net::kInvalidNetworkId;
		RecordMask                 mask = 0;
		std::span<const std::byte> body;   // at most kMaxRecordBytes
	};

	/// Fixed capacity, so decoding a snapshot never allocates.
	struct SnapshotView
	{
		std::uint16_t sequence = 0;
		Net::NetTick  serverTick = 0;
		std::uint16_t count = 0;   // 0..kMaxEntitiesPerMessage, unique ids
		std::array<RecordView, kMaxEntitiesPerMessage> records{};

		std::span<const RecordView> Records() const noexcept { return { records.data(), count }; }
	};

	// ---- codecs ------------------------------------------------------------------------------
	// Encode writes the type byte and the message. Decode reads everything AFTER the type byte.

	ENGINE_API bool Encode(ByteWriter& w, const JoinRequest& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, JoinRequest& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const JoinAccepted& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, JoinAccepted& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const WorldBegin& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, WorldBegin& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const WorldEnd& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, WorldEnd& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const ClientReady& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, ClientReady& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const InputMessage& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, InputMessage& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const SpawnBatchView& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, SpawnBatchView& m);   // may allocate (bounded)

	ENGINE_API bool Encode(ByteWriter& w, const SceneObjectBatchView& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, SceneObjectBatchView& m);   // may allocate (bounded)

	ENGINE_API bool Encode(ByteWriter& w, const DespawnBatchView& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, DespawnBatchView& m);   // may allocate (bounded)

	ENGINE_API bool Encode(ByteWriter& w, const SnapshotView& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, SnapshotView& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const GameplayEventView& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, GameplayEventView& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const PeerRoster& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, PeerRoster& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const CommandView& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, CommandView& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const UnreliableEventView& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, UnreliableEventView& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const SceneChange& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, SceneChange& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const SceneLoaded& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, SceneLoaded& m) noexcept;

	ENGINE_API bool Encode(ByteWriter& w, const OwnerChange& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, OwnerChange& m) noexcept;
	ENGINE_API bool Encode(ByteWriter& w, const PeerPings& m) noexcept;
	ENGINE_API bool Decode(ByteReader& r, PeerPings& m) noexcept;
}
