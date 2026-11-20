/*************************************************************************
* \file:         Protocol.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.6
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
*                  WorldBegin     u32 lifecycleSeq, u64 sceneAsset, u64 sceneHash
*                  WorldEnd       u32 lifecycleSeq
*                  ClientReady    u32 worldEndSeq
*                  SpawnBatch     u32 lifecycleSeq, u32 serverTick, u16 count,
*                                 count x { varint id, u16 prefab, u8 owner,
*                                           u8 mask, u8 length, state }
*                  DespawnBatch   u32 lifecycleSeq, u16 count, count x varint id
*                  SceneObjectBatch u32 lifecycleSeq, u32 serverTick, u16 count,
*                                 count x { varint id, u64 sceneId, u16 profile,
*                                           u8 owner, u8 mask, u8 length, state }
*                  Snapshot       u16 sequence, u32 serverTick, u16 count,
*                                 count x { varint id, u8 mask, u8 length, body }
*                  Input          u8 count (0..3), count x { u32 sequence,
*                                 u32 clientTick, i8 moveX, i8 moveZ, u16 yaw,
*                                 u8 buttons }, u16 ackedSnapshot, u32 ackBits,
*                                 u32 appliedLifecycle  (12 / 25 / 38 / 51 bytes)
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
*                                 u64 sceneAsset (non-zero)
*                  SceneLoaded    u32 transition (non-zero)
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
	// Bump on ANY change to the bytes of any message. (8 = SceneChange and SceneLoaded; 7 =
	// UnreliableEvent, and the NetVariables serializer; 6 = GameplayEvent's target; 5 = PeerRoster
	// and Command; 4 = scene objects and the scene identity in WorldBegin; 3 = lifecycle
	// sequence; versions 1 and 2 were earlier development versions.)
	inline constexpr std::uint32_t kProtocolVersion = 8;

	inline constexpr std::size_t   kMaxSnapshotBytes      = 1100;        // one MTU-sized unreliable message
	inline constexpr std::size_t   kMaxReliableBytes      = 16 * 1024;   // spawn / despawn / event messages
	inline constexpr std::size_t   kMaxGameplayEventPayload = kMaxReliableBytes - 12;  // after u8 type, u32 seq, u16 kind, varint target (<= 5)
	inline constexpr std::size_t   kMaxCommandPayload     = 1024;        // a client's request: small on purpose
	inline constexpr std::size_t   kMaxUnreliableEventPayload = kMaxSnapshotBytes - 8;   // one packet: after u8 type, u16 kind, varint target (<= 5)
	inline constexpr std::uint16_t kMaxEntitiesPerMessage = 512;
	inline constexpr std::size_t   kMaxRecordBytes        = 255;         // a record's length is one byte
	inline constexpr std::size_t   kMaxPlayerNameBytes    = 32;
	inline constexpr std::size_t   kMaxInputCommands      = 3;           // each command is sent 3 times
	inline constexpr std::uint32_t kFirstLifecycleSeq     = 1;           // JoinAccepted's; 0 = none

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
	/// networked content (0, 0: no saved scene, so no scene objects).
	struct WorldBegin
	{
		std::uint32_t lifecycleSeq = 0;
		std::uint64_t sceneAsset = 0;
		std::uint64_t sceneHash = 0;
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
	};
	struct SceneLoaded { std::uint32_t transition = 0; };   // must equal the SceneChange's

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
		std::uint8_t               mask = 0;   // which of the prefab's replicated components follow
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
		std::uint8_t               mask = 0;
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
		std::uint8_t               mask = 0;
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
}
