/*************************************************************************
* \file:         ReplicationRegistry.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                ReplicationRegistry and the built-in serializers (see the
*                header).
*
* \history
*    -v1.0       replication registry and serializers; review
*                fixes: maxBytes limited to one record, bounded Write;
*                decode/commit, bounded readers, owned names; records
*                applied as transactions with rollback
*
**************************************************************************/
#include "Network/ReplicationRegistry.h"

#include <algorithm>
#include <exception>
#include <format>

#include "Network/ByteStream.h"
#include "Network/CanonicalHash.h"
#include "Network/NetPrefabTable.h"
#include "Network/Protocol.h"
#include "Network/Quantize.h"

namespace Crucible
{
	namespace
	{
		// The most entries one record can have (its mask is a u8).
		constexpr std::size_t kMaxRecordEntries = 8;

		struct alignas(std::max_align_t) StateSlot { std::byte bytes[kMaxDecodedStateBytes]; };
		struct alignas(std::max_align_t) CopySlot  { std::byte bytes[kMaxComponentCopyBytes]; };

		// Reads x, y, z in that order. Not as three arguments of one call: C++ leaves argument
		// evaluation order unspecified (MSVC reads right to left), which would swap x and z.
		Math::vec3 ReadPosition(ByteReader& in) noexcept
		{
			const float x = Quantize::FixedToPosition(in.I32());
			const float y = Quantize::FixedToPosition(in.I32());
			const float z = Quantize::FixedToPosition(in.I32());
			return Math::vec3(x, y, z);
		}

		// ---- Transform: TransformFull (16 bytes) and TransformYawOnly (14 bytes) ------------

		struct TransformState
		{
			Math::vec3 position;
			Math::quat rotation;
		};

		bool WriteTransformFull(const World& world, Entity e, const ReplicationContext&, ByteWriter& out)
		{
			const Transform* t = world.Read<Transform>(e);
			if (t == nullptr) return false;
			out.I32(Quantize::PositionToFixed(t->positions.x));
			out.I32(Quantize::PositionToFixed(t->positions.y));
			out.I32(Quantize::PositionToFixed(t->positions.z));
			out.U32(Quantize::PackQuat(t->rotation));
			return out.Ok();
		}

		// Fixed-point positions and packed rotations are finite whatever the bytes: only the
		// length can be wrong.
		bool DecodeTransformFull(ByteReader& in, const ReplicationContext&, TransformState& out)
		{
			out.position = ReadPosition(in);
			out.rotation = Quantize::UnpackQuat(in.U32());
			return in.Ok();
		}

		bool WriteTransformYaw(const World& world, Entity e, const ReplicationContext&, ByteWriter& out)
		{
			const Transform* t = world.Read<Transform>(e);
			if (t == nullptr) return false;
			out.I32(Quantize::PositionToFixed(t->positions.x));
			out.I32(Quantize::PositionToFixed(t->positions.y));
			out.I32(Quantize::PositionToFixed(t->positions.z));
			out.U16(Quantize::AngleToU16(Quantize::YawDegrees(t->rotation)));
			return out.Ok();
		}

		bool DecodeTransformYaw(ByteReader& in, const ReplicationContext&, TransformState& out)
		{
			out.position = ReadPosition(in);
			out.rotation = Quantize::YawRotation(Quantize::U16ToAngle(in.U16()));
			return in.Ok();
		}

		bool CommitTransform(Transform& t, const TransformState& state, const ReplicationContext&)
		{
			t.positions = state.position;
			t.rotation = state.rotation;
			return true;
		}

		// ---- EnabledMask: bit i = desc.enabledReplicated[i] is on, 1 byte --------------------

		struct MaskState
		{
			std::uint8_t mask;
		};

		bool WriteEnabledMask(const World& world, Entity e, const ReplicationContext& ctx, ByteWriter& out)
		{
			if (ctx.desc == nullptr) return false;
			std::uint8_t mask = 0;
			for (std::size_t i = 0; i < ctx.desc->enabledReplicated.size(); ++i)
				if (world.IsComponentEnabled(e, ctx.desc->enabledReplicated[i])) mask |= static_cast<std::uint8_t>(1u << i);
			out.U8(mask);
			return out.Ok();
		}

		// Bits past the prefab's list mean the sender disagrees about the prefab: refused.
		bool DecodeEnabledMask(ByteReader& in, const ReplicationContext& ctx, MaskState& out)
		{
			out.mask = in.U8();
			return in.Ok() && ctx.desc != nullptr && ctx.desc->enabledReplicated.size() <= kMaxRecordEntries
				&& (out.mask >> ctx.desc->enabledReplicated.size()) == 0;
		}

		// Every component the mask switches must exist and be switchable.
		bool EnabledMaskReady(const World& world, Entity e, const MaskState&, const ReplicationContext& ctx)
		{
			if (ctx.desc == nullptr || !world.IsAlive(e)) return false;
			const ComponentSignature has = world.GetSignature(e);
			for (const ComponentId c : ctx.desc->enabledReplicated)
				if (c >= kMaxComponents || !has.test(c)) return false;
			return true;
		}

		ReplicatedComponent EnabledMaskSerializer()
		{
			ReplicatedComponent c;
			c.id = NetComponentId::EnabledMask;
			c.name = "EnabledMask";
			c.schemaVersion = 1;
			c.maxBytes = 1;
			c.stateBytes = sizeof(MaskState);
			c.write = &WriteEnabledMask;
			c.decode = [](ByteReader& in, const ReplicationContext& ctx, void* state) -> bool {
				return DecodeEnabledMask(in, ctx, *::new (state) MaskState{});
			};
			c.ready = [](const World& world, Entity e, const void* state, const ReplicationContext& ctx) -> bool {
				return EnabledMaskReady(world, e, *static_cast<const MaskState*>(state), ctx);
			};
			c.enabledFlags = true;   // committed (and rolled back) by the registry: see Transact
			return c;
		}

		// What every commit needs: a live entity that has the serializer's component, and whatever
		// its ready check asks for. Read-only.
		bool CanCommit(const World& world, Entity e, const ReplicatedComponent& c, const void* state, const ReplicationContext& ctx)
		{
			if (!world.IsAlive(e)) return false;
			if (c.local != kInvalidComponentId && (c.local >= kMaxComponents || !world.GetSignature(e).test(c.local))) return false;
			return c.ready == nullptr || c.ready(world, e, state, ctx);
		}

		void SetError(std::string* error, std::string text) noexcept
		{
			if (error == nullptr) return;
			try { *error = std::move(text); }
			catch (...) {}
		}
	}

	// One entry of a transaction: the decoded state, the saved copy of its component (or the old
	// on/off flags), and how far it got.
	struct ReplicationRegistry::Entry
	{
		const ReplicatedComponent* c = nullptr;
		StateSlot    state;
		CopySlot     copy;
		bool         saved = false;          // `copy` holds the component's value from before the commit
		void*        live = nullptr;         // the component the commit wrote into
		std::uint8_t oldFlags = 0;           // EnabledMask: on/off before
		std::uint8_t changedFlags = 0;       // EnabledMask: which ones the commit actually switched
	};

	bool ReplicationRegistry::Register(const ReplicatedComponent& component, std::string& error)
	{
		if (component.id == NetComponentId::None) { error = "a serializer needs an id other than None"; return false; }
		const ReplicatedComponent& existing = m_table[static_cast<std::uint8_t>(component.id)];
		if (existing.id != NetComponentId::None)
		{
			error = std::format("serializer id {} is already '{}'; cannot also register '{}'",
				static_cast<int>(component.id), existing.name, component.name);
			return false;
		}
		if (component.name.empty()) { error = std::format("serializer id {} has no name", static_cast<int>(component.id)); return false; }
		if (component.maxBytes == 0 || component.maxBytes > kMaxRecordBytes)
		{
			error = std::format("serializer '{}' declares maxBytes {}: it must be 1 to {} (one record's length is a byte)",
				component.name, component.maxBytes, kMaxRecordBytes);
			return false;
		}
		if (component.stateBytes == 0 || component.stateBytes > kMaxDecodedStateBytes)
		{
			error = std::format("serializer '{}' declares a decoded state of {} bytes: it must be 1 to {} (build it with MakeSerializer)",
				component.name, component.stateBytes, kMaxDecodedStateBytes);
			return false;
		}
		if (component.enabledFlags)
		{
			if (component.id != NetComponentId::EnabledMask || component.write == nullptr || component.decode == nullptr)
			{
				error = std::format("serializer '{}': only the built-in EnabledMask commits on/off flags", component.name);
				return false;
			}
		}
		else if (component.write == nullptr || component.decode == nullptr || component.commit == nullptr || component.read == nullptr
			|| component.resolve == nullptr || component.save == nullptr || component.restore == nullptr || component.discard == nullptr
			|| component.local == kInvalidComponentId)
		{
			error = std::format("serializer '{}' is incomplete (build it with MakeSerializer<Component, State, Decode, Commit>)", component.name);
			return false;
		}
		m_table[static_cast<std::uint8_t>(component.id)] = component;   // a copy: the name is now ours
		return true;
	}

	bool ReplicationRegistry::RegisterBuiltIns(std::string& error)
	{
		return Register(MakeSerializer<Transform, TransformState, &DecodeTransformFull, &CommitTransform>({
				.id = NetComponentId::TransformFull, .name = "Transform/Full", .schemaVersion = 1,
				.maxBytes = 16, .write = &WriteTransformFull }), error)
			&& Register(MakeSerializer<Transform, TransformState, &DecodeTransformYaw, &CommitTransform>({
				.id = NetComponentId::TransformYawOnly, .name = "Transform/YawOnly", .schemaVersion = 1,
				.maxBytes = 14, .write = &WriteTransformYaw }), error)
			&& Register(EnabledMaskSerializer(), error);
	}

	void ReplicationRegistry::HashInto(CanonicalHash& hash) const
	{
		hash.Str("serializers");
		for (const ReplicatedComponent& c : m_table)
		{
			if (c.id == NetComponentId::None) continue;
			hash.U8(static_cast<std::uint8_t>(c.id));
			hash.Str(c.name);
			hash.U32(c.schemaVersion);
			hash.U32(c.maxBytes);
		}
	}

	// ---- one component ---------------------------------------------------------------------

	bool ReplicationRegistry::Write(NetComponentId id, const World& world, Entity entity, const ReplicationContext& ctx,
		ByteWriter& out, std::string* error) const
	{
		const ReplicatedComponent* c = Find(id);
		if (c == nullptr)
		{
			if (error) *error = std::format("serializer id {} is not registered", static_cast<int>(id));
			return false;
		}
		// Exactly maxBytes of room: a serializer that writes more fails here (the writer refuses
		// what does not fit) instead of running into the next component or past the record.
		std::array<std::byte, kMaxRecordBytes> scratch;
		ByteWriter bounded(std::span<std::byte>(scratch).first(c->maxBytes));
		if (!c->write(world, entity, ctx, bounded) || !bounded.Ok())
		{
			if (error) *error = std::format("serializer '{}' failed or wrote more than its {} bytes", c->name, c->maxBytes);
			return false;
		}
		if (out.Remaining() < bounded.Size())
		{
			if (error) *error = std::format("no room for serializer '{}' ({} bytes)", c->name, bounded.Size());
			return false;
		}
		out.Bytes(bounded.Written());
		return out.Ok();
	}

	// The serializer sees only the next min(maxBytes, remaining) bytes: it cannot read past its
	// declared size or past the record. `in` advances by what was decoded, and only on success.
	bool ReplicationRegistry::DecodeBounded(const ReplicatedComponent& c, ByteReader& in, const ReplicationContext& ctx, void* state) const
	{
		if (!in.Ok()) return false;
		ByteReader probe = in;   // a copy: `in` does not move while decoding
		const std::span<const std::byte> window = probe.Sub(std::min<std::size_t>(c.maxBytes, in.Remaining()));
		ByteReader bounded(window);
		if (!c.decode(bounded, ctx, state) || !bounded.Ok()) return false;
		in.Skip(window.size() - bounded.Remaining());
		return in.Ok();
	}

	bool ReplicationRegistry::Validate(NetComponentId id, ByteReader& in, const ReplicationContext& ctx) const
	{
		const ReplicatedComponent* c = Find(id);
		if (c == nullptr) return false;
		StateSlot scratch;
		return DecodeBounded(*c, in, ctx, scratch.bytes);
	}

	bool ReplicationRegistry::Apply(NetComponentId id, World& world, Entity entity, ByteReader& in, const ReplicationContext& ctx,
		std::string* error) const
	{
		const ReplicatedComponent* c = Find(id);
		if (c == nullptr) { SetError(error, std::format("serializer id {} is not registered", static_cast<int>(id))); return false; }
		Entry entry;
		entry.c = c;
		ByteReader trial = in;   // `in` moves only once the whole transaction succeeded
		if (!DecodeBounded(*c, trial, ctx, entry.state.bytes)) { SetError(error, std::format("'{}' did not decode", c->name)); return false; }
		if (!Transact(world, entity, std::span<Entry>(&entry, 1), ctx, error)) return false;
		in = trial;
		return true;
	}

	// ---- one record ------------------------------------------------------------------------

	bool ReplicationRegistry::WriteRecord(const World& world, Entity entity, std::uint8_t mask, const ReplicationContext& ctx,
		ByteWriter& out, std::string* error) const
	{
		if (ctx.desc == nullptr) { if (error) *error = "no prefab"; return false; }
		const auto& list = ctx.desc->replicated;
		if (list.size() < kMaxRecordEntries && (mask >> list.size()) != 0) { if (error) *error = "mask selects an entry past the prefab's list"; return false; }
		std::array<std::byte, kMaxRecordBytes> scratch;
		ByteWriter record(scratch);
		for (std::size_t i = 0; i < list.size(); ++i)
			if ((mask >> i) & 1u)
				if (!Write(list[i], world, entity, ctx, record, error)) return false;
		if (out.Remaining() < record.Size()) { if (error) *error = "no room for the record"; return false; }
		out.Bytes(record.Written());
		return out.Ok();
	}

	bool ReplicationRegistry::ValidateRecord(std::uint8_t mask, std::span<const std::byte> body, const ReplicationContext& ctx) const
	{
		if (ctx.desc == nullptr || body.size() > kMaxRecordBytes) return false;
		const auto& list = ctx.desc->replicated;
		if (list.size() > kMaxRecordEntries || (list.size() < kMaxRecordEntries && (mask >> list.size()) != 0)) return false;
		ByteReader in(body);
		for (std::size_t i = 0; i < list.size(); ++i)
			if (((mask >> i) & 1u) && !Validate(list[i], in, ctx)) return false;
		return in.AtEnd();   // nothing left over: every byte belonged to an entry
	}

	bool ReplicationRegistry::ApplyRecord(World& world, Entity entity, std::uint8_t mask, std::span<const std::byte> body,
		const ReplicationContext& ctx, std::string* error) const noexcept
	{
		try
		{
			if (ctx.desc == nullptr || body.size() > kMaxRecordBytes) { SetError(error, "no prefab, or a body over one record"); return false; }
			const auto& list = ctx.desc->replicated;
			if (list.size() > kMaxRecordEntries || (list.size() < kMaxRecordEntries && (mask >> list.size()) != 0))
			{
				SetError(error, "the mask selects an entry past the prefab's list");
				return false;
			}

			// 1. Decode every selected entry. Nothing touches the World here.
			std::array<Entry, kMaxRecordEntries> entries;
			std::size_t count = 0;
			ByteReader in(body);
			for (std::size_t i = 0; i < list.size(); ++i)
			{
				if (((mask >> i) & 1u) == 0) continue;
				Entry& entry = entries[count];
				entry.c = Find(list[i]);
				if (entry.c == nullptr) { SetError(error, std::format("serializer id {} is not registered", static_cast<int>(list[i]))); return false; }
				if (!DecodeBounded(*entry.c, in, ctx, entry.state.bytes)) { SetError(error, std::format("'{}' did not decode", entry.c->name)); return false; }
				++count;
			}
			if (!in.AtEnd()) { SetError(error, "bytes left over after the last entry"); return false; }

			// 2-5. Preconditions, saved copies, commits, rollback.
			return Transact(world, entity, std::span<Entry>(entries.data(), count), ctx, error);
		}
		catch (...)   // a throwing decode: nothing was changed
		{
			SetError(error, "a serializer threw while decoding");
			return false;
		}
	}

	bool ReplicationRegistry::ApplyRecord(World& world, Entity entity, std::uint8_t mask, ByteReader& in, std::size_t length,
		const ReplicationContext& ctx, std::string* error) const noexcept
	{
		ByteReader probe = in;   // a copy: `in` moves only if the whole record was applied
		const std::span<const std::byte> body = probe.Sub(length);
		if (!probe.Ok()) { SetError(error, "the record is longer than what is left"); return false; }
		if (!ApplyRecord(world, entity, mask, body, ctx, error)) return false;
		in.Skip(length);
		return true;
	}

	bool ReplicationRegistry::Transact(World& world, Entity entity, std::span<Entry> entries, const ReplicationContext& ctx,
		std::string* error) noexcept
	{
		bool ok = true;
		std::size_t started = 0;   // entries whose commit has begun (and may have changed something)
		try
		{
			// 2. Every precondition, before anything changes.
			if (!world.IsAlive(entity)) { SetError(error, "the entity is not alive"); return false; }
			for (const Entry& e : entries)
			{
				if (!CanCommit(world, entity, *e.c, e.state.bytes, ctx))
				{
					SetError(error, std::format("'{}': the entity lacks its component, or its ready check failed", e.c->name));
					return false;
				}
			}

			// 3. Save what each commit may change. A copy may throw: nothing has changed yet.
			for (Entry& e : entries)
			{
				if (e.c->enabledFlags)
				{
					for (std::size_t i = 0; i < ctx.desc->enabledReplicated.size(); ++i)
						if (world.IsComponentEnabled(entity, ctx.desc->enabledReplicated[i])) e.oldFlags |= static_cast<std::uint8_t>(1u << i);
					continue;
				}
				const void* current = e.c->read(world, entity);
				if (current == nullptr) { SetError(error, std::format("'{}': the entity lacks its component", e.c->name)); ok = false; break; }
				e.c->save(current, e.copy.bytes);
				e.saved = true;
			}

			// 4. Commit, in list order.
			for (std::size_t k = 0; ok && k < entries.size(); ++k)
			{
				Entry& e = entries[k];
				started = k + 1;
				if (e.c->enabledFlags)
				{
					const std::uint8_t wanted = static_cast<const MaskState*>(static_cast<const void*>(e.state.bytes))->mask;
					for (std::size_t i = 0; i < ctx.desc->enabledReplicated.size(); ++i)
					{
						const bool on = ((wanted >> i) & 1u) != 0;
						if ((((e.oldFlags >> i) & 1u) != 0) == on) continue;
						if (!world.TrySetComponentEnabled(entity, ctx.desc->enabledReplicated[i], on))
						{
							SetError(error, std::format("'{}' could not switch a component", e.c->name));
							ok = false;
							break;
						}
						e.changedFlags |= static_cast<std::uint8_t>(1u << i);
					}
					continue;
				}
				e.live = e.c->resolve(world, entity);
				if (e.live == nullptr) { SetError(error, std::format("'{}': the entity lacks its component", e.c->name)); ok = false; break; }
				if (!e.c->commit(e.live, e.state.bytes, ctx)) { SetError(error, std::format("'{}' failed to commit", e.c->name)); ok = false; break; }
			}
		}
		catch (const std::exception& ex)
		{
			SetError(error, std::string("a commit threw: ") + ex.what());
			ok = false;
		}
		catch (...)
		{
			SetError(error, "a commit threw");
			ok = false;
		}

		// 5. Roll back everything that was started, newest first; then drop the saved copies.
		if (!ok)
		{
			for (std::size_t k = started; k-- > 0;)
			{
				Entry& e = entries[k];
				if (e.c->enabledFlags)
				{
					for (std::size_t i = 0; i < ctx.desc->enabledReplicated.size(); ++i)
					{
						if (((e.changedFlags >> i) & 1u) == 0) continue;
						try { world.TrySetComponentEnabled(entity, ctx.desc->enabledReplicated[i], ((e.oldFlags >> i) & 1u) != 0); }
						catch (...) {}   // cannot happen: the same call just succeeded in the same context
					}
				}
				else if (e.saved && e.live != nullptr)
				{
					e.c->restore(e.live, e.copy.bytes);   // noexcept move assignment; destroys the copy
					e.saved = false;
				}
			}
		}
		for (Entry& e : entries)
		{
			if (e.saved) { e.c->discard(e.copy.bytes); e.saved = false; }
		}
		return ok;
	}
}
