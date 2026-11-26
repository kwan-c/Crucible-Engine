/*************************************************************************
* \file:         Protocol.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.6
* \brief
*                Encoders and decoders for every protocol message (see
*                Protocol.h for the wire layouts and the rules).
*
* \history
*    -v1.0       protocol version 3 codecs
*    -v1.1       PeerRoster and Command codecs (protocol version 5)
*    -v1.2       GameplayEvent's target (protocol version 6)
*    -v1.3       UnreliableEvent codec (protocol version 7)
*    -v1.4       SceneChange and SceneLoaded codecs (protocol version 8)
*    -v1.5       protocol version 9: records' varint mask and length (WriteRecordHeader,
*                ReadRecord); the scene seed in WorldBegin and SceneChange
*    -v1.6       OwnerChange codec
*
**************************************************************************/
#include "Network/Protocol.h"

#include <algorithm>

namespace Crucible
{
	namespace
	{
		// Fixed sizes, type byte included.
		constexpr std::size_t kJoinRequestMax  = 1 + 4 + 8 + 1 + kMaxPlayerNameBytes;   // 46
		constexpr std::size_t kJoinAcceptedMax = 1 + 4 + 1 + 4 + 2;                      // 12
		constexpr std::size_t kSeqOnlyMax      = 1 + 4;                                  // 5
		constexpr std::size_t kWorldBeginMax   = 1 + 4 + 8 + 8 + 4;                      // 25
		constexpr std::size_t kInputCommandSize = 4 + 4 + 1 + 1 + 2 + 1;                 // 13
		constexpr std::size_t kInputMax = 1 + 1 + kMaxInputCommands * kInputCommandSize + 2 + 4 + 4;   // 51
		constexpr std::size_t kPeerRosterMax = 1 + 4 + 1 + Net::kMaxPlayers * (1 + 1 + 1 + kMaxPlayerNameBytes);   // 146
		constexpr std::size_t kCommandMax = 1 + 4 + 2 + 5 + kMaxCommandPayload;                    // 1036
		constexpr std::size_t kSceneChangeMax = 1 + 4 + 4 + 8 + 4;                                // 21
		constexpr std::size_t kOwnerChangeMax = 1 + 4 + 5 + 1;                                    // 11

		void WriteType(ByteWriter& w, MessageType type) noexcept { w.U8(static_cast<std::uint8_t>(type)); }

		// Writer still Ok and this message (from `start`) within its type's limit.
		bool Finished(const ByteWriter& w, std::size_t start, MessageType type) noexcept
		{
			return w.Ok() && w.Size() - start <= MaxMessageBytes(type);
		}

		// Every id non-zero and none twice. Sorts a copy, so the caller's order is kept.
		template <typename GetId>
		bool IdsUniqueAndValid(std::size_t count, GetId getId) noexcept
		{
			if (count > kMaxEntitiesPerMessage) return false;
			std::array<Net::NetworkId, kMaxEntitiesPerMessage> ids;
			for (std::size_t i = 0; i < count; ++i) ids[i] = getId(i);
			const auto end = ids.begin() + static_cast<std::ptrdiff_t>(count);
			std::sort(ids.begin(), end);
			if (count > 0 && ids[0] == Net::kInvalidNetworkId) return false;   // sorted: a zero would be first
			return std::adjacent_find(ids.begin(), end) == end;
		}

		bool IsValidOwner(Net::PeerId owner) noexcept { return owner <= Net::kMaxClients; }   // 0 = server
		bool IsClientPeer(Net::PeerId peer) noexcept  { return peer >= 1 && peer <= Net::kMaxClients; }

		bool EncodeSeqOnly(ByteWriter& w, MessageType type, std::uint32_t seq) noexcept
		{
			if (seq == 0) return false;
			const std::size_t start = w.Size();
			WriteType(w, type);
			w.U32(seq);
			return Finished(w, start, type);
		}

		bool DecodeSeqOnly(ByteReader& r, std::uint32_t& seq) noexcept
		{
			seq = r.U32();
			return r.Ok() && r.AtEnd() && seq != 0;
		}
	}

	std::size_t MaxMessageBytes(MessageType type) noexcept
	{
		switch (type)
		{
		case MessageType::JoinRequest:   return kJoinRequestMax;
		case MessageType::JoinAccepted:  return kJoinAcceptedMax;
		case MessageType::WorldBegin:    return kWorldBeginMax;
		case MessageType::WorldEnd:
		case MessageType::ClientReady:   return kSeqOnlyMax;
		case MessageType::Input:         return kInputMax;
		case MessageType::PeerRoster:    return kPeerRosterMax;
		case MessageType::Command:       return kCommandMax;
		case MessageType::SceneChange:   return kSceneChangeMax;
		case MessageType::SceneLoaded:   return kSeqOnlyMax;
		case MessageType::OwnerChange:   return kOwnerChangeMax;
		case MessageType::Snapshot:
		case MessageType::UnreliableEvent: return kMaxSnapshotBytes;
		case MessageType::SpawnBatch:
		case MessageType::SceneObjectBatch:
		case MessageType::DespawnBatch:
		case MessageType::GameplayEvent: return kMaxReliableBytes;
		default:                         return 0;
		}
	}

	bool ReadMessageType(ByteReader& r, MessageType& out) noexcept
	{
		const std::uint8_t raw = r.U8();
		if (!r.Ok() || !IsKnownMessageType(raw)) return false;
		out = static_cast<MessageType>(raw);
		return true;
	}

	bool IsValidPlayerName(std::string_view name) noexcept
	{
		if (name.size() > kMaxPlayerNameBytes) return false;
		return std::ranges::none_of(name, [](char c) {
			const auto b = static_cast<unsigned char>(c);
			return b < 0x20 || b == 0x7F;
		});
	}

	// ---- join handshake ------------------------------------------------------------------

	bool Encode(ByteWriter& w, const JoinRequest& m) noexcept
	{
		if (m.nameLength > kMaxPlayerNameBytes || !IsValidPlayerName(m.Name())) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::JoinRequest);
		w.U32(m.protocolVersion);
		w.U64(m.contentHash);
		w.U8(m.nameLength);
		w.Bytes(std::as_bytes(std::span(m.name.data(), m.nameLength)));
		return Finished(w, start, MessageType::JoinRequest);
	}

	bool Decode(ByteReader& r, JoinRequest& m) noexcept
	{
		m.protocolVersion = r.U32();   // any value: the session compares it and refuses with a reason
		m.contentHash = r.U64();
		const std::uint8_t length = r.U8();
		if (!r.Ok() || length > kMaxPlayerNameBytes) return false;   // check BEFORE using it
		const std::span<const std::byte> bytes = r.Sub(length);
		if (!r.Ok() || !r.AtEnd()) return false;
		m.name = {};
		for (std::size_t i = 0; i < bytes.size(); ++i) m.name[i] = static_cast<char>(bytes[i]);
		m.nameLength = length;
		return IsValidPlayerName(m.Name());
	}

	bool Encode(ByteWriter& w, const JoinAccepted& m) noexcept
	{
		if (m.lifecycleSeq != kFirstLifecycleSeq || !IsClientPeer(m.peer) || m.tickRate == 0) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::JoinAccepted);
		w.U32(m.lifecycleSeq);
		w.U8(m.peer);
		w.U32(m.serverTick);
		w.U16(m.tickRate);
		return Finished(w, start, MessageType::JoinAccepted);
	}

	bool Decode(ByteReader& r, JoinAccepted& m) noexcept
	{
		m.lifecycleSeq = r.U32();
		m.peer = r.U8();
		m.serverTick = r.U32();
		m.tickRate = r.U16();
		return r.Ok() && r.AtEnd() && m.lifecycleSeq == kFirstLifecycleSeq && IsClientPeer(m.peer) && m.tickRate != 0;
	}

	bool Encode(ByteWriter& w, const WorldBegin& m) noexcept
	{
		if (m.lifecycleSeq == 0) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::WorldBegin);
		w.U32(m.lifecycleSeq);
		w.U64(m.sceneAsset);
		w.U64(m.sceneHash);
		w.U32(m.sceneSeed);
		return Finished(w, start, MessageType::WorldBegin);
	}

	bool Decode(ByteReader& r, WorldBegin& m) noexcept
	{
		m.lifecycleSeq = r.U32();
		m.sceneAsset = r.U64();
		m.sceneHash = r.U64();
		m.sceneSeed = r.U32();
		return r.Ok() && r.AtEnd() && m.lifecycleSeq != 0;
	}
	bool Encode(ByteWriter& w, const WorldEnd& m) noexcept    { return EncodeSeqOnly(w, MessageType::WorldEnd, m.lifecycleSeq); }
	bool Decode(ByteReader& r, WorldEnd& m) noexcept          { return DecodeSeqOnly(r, m.lifecycleSeq); }
	bool Encode(ByteWriter& w, const ClientReady& m) noexcept { return EncodeSeqOnly(w, MessageType::ClientReady, m.worldEndSeq); }
	bool Decode(ByteReader& r, ClientReady& m) noexcept       { return DecodeSeqOnly(r, m.worldEndSeq); }

	// ---- input ---------------------------------------------------------------------------

	bool Encode(ByteWriter& w, const InputMessage& m) noexcept
	{
		if (m.count > m.commands.size()) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::Input);
		w.U8(m.count);   // 0 = heartbeat and acks only
		for (const InputCommand& c : m.Commands())
		{
			if ((c.buttons & ~InputCommand::kKnownButtons) != 0) return false;
			w.U32(c.sequence);
			w.U32(c.clientTick);
			w.I8(c.moveX);
			w.I8(c.moveZ);
			w.U16(c.yaw);
			w.U8(c.buttons);
		}
		w.U16(m.ackedSnapshot);
		w.U32(m.ackBits);
		w.U32(m.appliedLifecycle);
		return Finished(w, start, MessageType::Input);
	}

	bool Decode(ByteReader& r, InputMessage& m) noexcept
	{
		m.count = r.U8();
		if (!r.Ok() || m.count > m.commands.size()) return false;   // validate BEFORE looping on it; 0 is legal
		for (std::uint8_t i = 0; i < m.count; ++i)
		{
			InputCommand& c = m.commands[i];
			c.sequence = r.U32();
			c.clientTick = r.U32();
			c.moveX = r.I8();
			c.moveZ = r.I8();
			c.yaw = r.U16();
			c.buttons = r.U8();
			if ((c.buttons & ~InputCommand::kKnownButtons) != 0) return false;   // unknown bits: another format
		}
		m.ackedSnapshot = r.U16();
		m.ackBits = r.U32();
		m.appliedLifecycle = r.U32();
		return r.Ok() && r.AtEnd();   // trailing junk = malformed
	}

	// ---- records (protocol 9) ------------------------------------------------------------------

	namespace
	{
		// A record's mask and length, both varints (one byte each below 128). The caller has
		// checked the length against kMaxRecordBytes.
		void WriteRecordHeader(ByteWriter& w, RecordMask mask, std::size_t length) noexcept
		{
			w.VarU32(mask);
			w.VarU32(static_cast<std::uint32_t>(length));
		}

		// Both limits are checked BEFORE the body is taken: a mask wider than RecordMask or a
		// length over kMaxRecordBytes is malformed, whatever follows.
		bool ReadRecord(ByteReader& r, RecordMask& mask, std::span<const std::byte>& body) noexcept
		{
			const std::uint32_t rawMask = r.VarU32();
			const std::uint32_t length = r.VarU32();
			if (!r.Ok() || rawMask > 0xFFFFu || length > kMaxRecordBytes) return false;
			mask = static_cast<RecordMask>(rawMask);
			body = r.Sub(length);
			return r.Ok();
		}
	}

	// ---- entity lifecycle ------------------------------------------------------------------

	bool Encode(ByteWriter& w, const SpawnBatchView& m) noexcept
	{
		const std::size_t count = m.spawns.size();
		if (m.lifecycleSeq == 0 || count == 0 || count > kMaxEntitiesPerMessage) return false;
		if (!IdsUniqueAndValid(count, [&](std::size_t i) { return m.spawns[i].id; })) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::SpawnBatch);
		w.U32(m.lifecycleSeq);
		w.U32(m.serverTick);
		w.U16(static_cast<std::uint16_t>(count));
		for (const SpawnView& s : m.spawns)
		{
			if (s.prefab == Net::kInvalidNetPrefab || !IsValidOwner(s.owner) || s.state.size() > kMaxRecordBytes) return false;
			w.VarU32(s.id);
			w.U16(s.prefab);
			w.U8(s.owner);
			WriteRecordHeader(w, s.mask, s.state.size());
			w.Bytes(s.state);
		}
		return Finished(w, start, MessageType::SpawnBatch);
	}

	bool Decode(ByteReader& r, SpawnBatchView& m)
	{
		m.spawns.clear();
		m.lifecycleSeq = r.U32();
		m.serverTick = r.U32();
		const std::uint16_t count = r.U16();
		if (!r.Ok() || m.lifecycleSeq == 0 || count == 0 || count > kMaxEntitiesPerMessage) return false;
		m.spawns.reserve(count);   // bounded by the check above
		for (std::uint16_t i = 0; i < count; ++i)
		{
			SpawnView s;
			s.id = r.VarU32();
			s.prefab = r.U16();
			s.owner = r.U8();
			if (!ReadRecord(r, s.mask, s.state)) return false;
			if (!r.Ok() || s.id == Net::kInvalidNetworkId || s.prefab == Net::kInvalidNetPrefab || !IsValidOwner(s.owner)) return false;
			m.spawns.push_back(s);
		}
		if (!r.AtEnd()) return false;   // trailing bytes
		return IdsUniqueAndValid(m.spawns.size(), [&](std::size_t i) { return m.spawns[i].id; });
	}

	// ---- scene objects (protocol 4) -----------------------------------------------------------

	namespace
	{
		// Every scene id non-zero and none twice (sorted copy, as for ids).
		bool SceneIdsUniqueAndValid(const std::vector<SceneObjectView>& objects) noexcept
		{
			if (objects.size() > kMaxEntitiesPerMessage) return false;
			std::array<std::uint64_t, kMaxEntitiesPerMessage> ids;
			for (std::size_t i = 0; i < objects.size(); ++i) ids[i] = objects[i].sceneId;
			const auto end = ids.begin() + static_cast<std::ptrdiff_t>(objects.size());
			std::sort(ids.begin(), end);
			if (!objects.empty() && ids[0] == 0) return false;
			return std::adjacent_find(ids.begin(), end) == end;
		}
	}

	bool Encode(ByteWriter& w, const SceneObjectBatchView& m) noexcept
	{
		const std::size_t count = m.objects.size();
		if (m.lifecycleSeq == 0 || count == 0 || count > kMaxEntitiesPerMessage) return false;
		if (!IdsUniqueAndValid(count, [&](std::size_t i) { return m.objects[i].id; }) || !SceneIdsUniqueAndValid(m.objects)) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::SceneObjectBatch);
		w.U32(m.lifecycleSeq);
		w.U32(m.serverTick);
		w.U16(static_cast<std::uint16_t>(count));
		for (const SceneObjectView& o : m.objects)
		{
			if (o.profile == Net::kInvalidNetPrefab || !IsValidOwner(o.owner) || o.state.size() > kMaxRecordBytes) return false;
			w.VarU32(o.id);
			w.U64(o.sceneId);
			w.U16(o.profile);
			w.U8(o.owner);
			WriteRecordHeader(w, o.mask, o.state.size());
			w.Bytes(o.state);
		}
		return Finished(w, start, MessageType::SceneObjectBatch);
	}

	bool Decode(ByteReader& r, SceneObjectBatchView& m)
	{
		m.objects.clear();
		m.lifecycleSeq = r.U32();
		m.serverTick = r.U32();
		const std::uint16_t count = r.U16();
		if (!r.Ok() || m.lifecycleSeq == 0 || count == 0 || count > kMaxEntitiesPerMessage) return false;
		m.objects.reserve(count);   // bounded by the check above
		for (std::uint16_t i = 0; i < count; ++i)
		{
			SceneObjectView o;
			o.id = r.VarU32();
			o.sceneId = r.U64();
			o.profile = r.U16();
			o.owner = r.U8();
			if (!ReadRecord(r, o.mask, o.state)) return false;
			if (!r.Ok() || o.id == Net::kInvalidNetworkId || o.sceneId == 0 || o.profile == Net::kInvalidNetPrefab || !IsValidOwner(o.owner)) return false;
			m.objects.push_back(o);
		}
		if (!r.AtEnd()) return false;   // trailing bytes
		return IdsUniqueAndValid(m.objects.size(), [&](std::size_t i) { return m.objects[i].id; }) && SceneIdsUniqueAndValid(m.objects);
	}

	bool Encode(ByteWriter& w, const DespawnBatchView& m) noexcept
	{
		const std::size_t count = m.ids.size();
		if (m.lifecycleSeq == 0 || count == 0 || count > kMaxEntitiesPerMessage) return false;
		if (!IdsUniqueAndValid(count, [&](std::size_t i) { return m.ids[i]; })) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::DespawnBatch);
		w.U32(m.lifecycleSeq);
		w.U16(static_cast<std::uint16_t>(count));
		for (const Net::NetworkId id : m.ids) w.VarU32(id);
		return Finished(w, start, MessageType::DespawnBatch);
	}

	bool Decode(ByteReader& r, DespawnBatchView& m)
	{
		m.ids.clear();
		m.lifecycleSeq = r.U32();
		const std::uint16_t count = r.U16();
		if (!r.Ok() || m.lifecycleSeq == 0 || count == 0 || count > kMaxEntitiesPerMessage) return false;
		m.ids.reserve(count);
		for (std::uint16_t i = 0; i < count; ++i)
		{
			const Net::NetworkId id = r.VarU32();
			if (!r.Ok()) return false;
			m.ids.push_back(id);
		}
		if (!r.AtEnd()) return false;
		return IdsUniqueAndValid(m.ids.size(), [&](std::size_t i) { return m.ids[i]; });
	}

	bool Encode(ByteWriter& w, const GameplayEventView& m) noexcept
	{
		if (m.lifecycleSeq == 0 || m.kind == 0 || m.payload.size() > kMaxGameplayEventPayload) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::GameplayEvent);
		w.U32(m.lifecycleSeq);
		w.U16(m.kind);
		w.VarU32(m.target);
		w.Bytes(m.payload);
		return Finished(w, start, MessageType::GameplayEvent);
	}

	bool Decode(ByteReader& r, GameplayEventView& m) noexcept
	{
		m.lifecycleSeq = r.U32();
		m.kind = r.U16();
		m.target = r.VarU32();
		if (!r.Ok() || m.lifecycleSeq == 0 || m.kind == 0 || r.Remaining() > kMaxGameplayEventPayload) return false;
		m.payload = r.Sub(r.Remaining());   // the payload's own format is checked by its handler
		return r.Ok() && r.AtEnd();
	}

	// ---- the roster ------------------------------------------------------------------------

	namespace
	{
		// The host first, then clients in strictly ascending PeerId order: no peer twice.
		bool IsValidRoster(const PeerRoster& m) noexcept
		{
			if (m.lifecycleSeq == 0 || m.count == 0 || m.count > Net::kMaxPlayers) return false;
			if (m.entries[0].peer != Net::kServerPeer) return false;
			for (std::size_t i = 0; i < m.count; ++i)
			{
				const RosterEntry& e = m.entries[i];
				if (!IsValidOwner(e.peer) || (e.flags & ~RosterEntry::kKnownFlags) != 0) return false;
				if (i > 0 && e.peer <= m.entries[i - 1].peer) return false;
				if (e.nameLength > kMaxPlayerNameBytes || !IsValidPlayerName(e.Name())) return false;
			}
			return true;
		}
	}

	bool Encode(ByteWriter& w, const PeerRoster& m) noexcept
	{
		if (!IsValidRoster(m)) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::PeerRoster);
		w.U32(m.lifecycleSeq);
		w.U8(m.count);
		for (const RosterEntry& e : m.Entries())
		{
			w.U8(e.peer);
			w.U8(e.flags);
			w.U8(e.nameLength);
			w.Bytes(std::as_bytes(std::span(e.name.data(), e.nameLength)));
		}
		return Finished(w, start, MessageType::PeerRoster);
	}

	bool Decode(ByteReader& r, PeerRoster& m) noexcept
	{
		m.count = 0;
		m.lifecycleSeq = r.U32();
		const std::uint8_t count = r.U8();
		if (!r.Ok() || count == 0 || count > Net::kMaxPlayers) return false;   // check BEFORE using it
		for (std::uint8_t i = 0; i < count; ++i)
		{
			RosterEntry& e = m.entries[i];
			e.peer = r.U8();
			e.flags = r.U8();
			const std::uint8_t length = r.U8();
			if (!r.Ok() || length > kMaxPlayerNameBytes) return false;
			const std::span<const std::byte> bytes = r.Sub(length);
			if (!r.Ok()) return false;
			e.name = {};
			for (std::size_t b = 0; b < bytes.size(); ++b) e.name[b] = static_cast<char>(bytes[b]);
			e.nameLength = length;
		}
		if (!r.AtEnd()) return false;
		m.count = count;
		return IsValidRoster(m);
	}

	// ---- commands --------------------------------------------------------------------------

	bool Encode(ByteWriter& w, const CommandView& m) noexcept
	{
		if (m.sequence == 0 || m.kind == 0 || m.payload.size() > kMaxCommandPayload) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::Command);
		w.U32(m.sequence);
		w.U16(m.kind);
		w.VarU32(m.target);
		w.Bytes(m.payload);
		return Finished(w, start, MessageType::Command);
	}

	bool Decode(ByteReader& r, CommandView& m) noexcept
	{
		m.sequence = r.U32();
		m.kind = r.U16();
		m.target = r.VarU32();
		if (!r.Ok() || m.sequence == 0 || m.kind == 0) return false;
		if (r.Remaining() > kMaxCommandPayload) return false;
		m.payload = r.Sub(r.Remaining());   // the payload's own format is checked by its handler
		return r.Ok() && r.AtEnd();
	}

	bool Encode(ByteWriter& w, const SceneChange& m) noexcept
	{
		if (m.lifecycleSeq == 0 || m.transition == 0 || m.sceneAsset == 0) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::SceneChange);
		w.U32(m.lifecycleSeq);
		w.U32(m.transition);
		w.U64(m.sceneAsset);
		w.U32(m.sceneSeed);
		return Finished(w, start, MessageType::SceneChange);
	}

	bool Decode(ByteReader& r, SceneChange& m) noexcept
	{
		m.lifecycleSeq = r.U32();
		m.transition = r.U32();
		m.sceneAsset = r.U64();
		m.sceneSeed = r.U32();
		return r.Ok() && r.AtEnd() && m.lifecycleSeq != 0 && m.transition != 0 && m.sceneAsset != 0;
	}

	bool Encode(ByteWriter& w, const SceneLoaded& m) noexcept
	{
		return EncodeSeqOnly(w, MessageType::SceneLoaded, m.transition);
	}

	bool Decode(ByteReader& r, SceneLoaded& m) noexcept
	{
		return DecodeSeqOnly(r, m.transition);
	}

	// ---- ownership -----------------------------------------------------

	bool Encode(ByteWriter& w, const OwnerChange& m) noexcept
	{
		if (m.lifecycleSeq == 0 || m.id == Net::kInvalidNetworkId || m.owner > Net::kMaxClients) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::OwnerChange);
		w.U32(m.lifecycleSeq);
		w.VarU32(m.id);
		w.U8(m.owner);
		return Finished(w, start, MessageType::OwnerChange);
	}

	bool Decode(ByteReader& r, OwnerChange& m) noexcept
	{
		m.lifecycleSeq = r.U32();
		m.id = r.VarU32();
		m.owner = r.U8();
		return r.Ok() && r.AtEnd() && m.lifecycleSeq != 0 && m.id != Net::kInvalidNetworkId && m.owner <= Net::kMaxClients;
	}

	bool Encode(ByteWriter& w, const UnreliableEventView& m) noexcept
	{
		if (m.kind == 0 || m.payload.size() > kMaxUnreliableEventPayload) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::UnreliableEvent);
		w.U16(m.kind);
		w.VarU32(m.target);
		w.Bytes(m.payload);
		return Finished(w, start, MessageType::UnreliableEvent);
	}

	bool Decode(ByteReader& r, UnreliableEventView& m) noexcept
	{
		m.kind = r.U16();
		m.target = r.VarU32();
		if (!r.Ok() || m.kind == 0 || r.Remaining() > kMaxUnreliableEventPayload) return false;
		m.payload = r.Sub(r.Remaining());
		return r.Ok() && r.AtEnd();
	}

	// ---- snapshots -------------------------------------------------------------------------

	bool Encode(ByteWriter& w, const SnapshotView& m) noexcept
	{
		if (m.count > kMaxEntitiesPerMessage) return false;
		if (!IdsUniqueAndValid(m.count, [&](std::size_t i) { return m.records[i].id; })) return false;
		const std::size_t start = w.Size();
		WriteType(w, MessageType::Snapshot);
		w.U16(m.sequence);
		w.U32(m.serverTick);
		w.U16(m.count);
		for (const RecordView& rec : m.Records())
		{
			if (rec.body.size() > kMaxRecordBytes) return false;
			w.VarU32(rec.id);
			WriteRecordHeader(w, rec.mask, rec.body.size());
			w.Bytes(rec.body);
		}
		return Finished(w, start, MessageType::Snapshot);
	}

	bool Decode(ByteReader& r, SnapshotView& m) noexcept
	{
		m.count = 0;
		m.sequence = r.U16();
		m.serverTick = r.U32();
		const std::uint16_t count = r.U16();
		if (!r.Ok() || count > kMaxEntitiesPerMessage) return false;   // 0 records is a legal snapshot
		for (std::uint16_t i = 0; i < count; ++i)
		{
			RecordView& rec = m.records[i];
			rec.id = r.VarU32();
			if (!r.Ok() || !ReadRecord(r, rec.mask, rec.body) || rec.id == Net::kInvalidNetworkId) return false;
		}
		if (!r.AtEnd()) return false;
		m.count = count;
		return IdsUniqueAndValid(m.count, [&](std::size_t i) { return m.records[i].id; });
	}
}
