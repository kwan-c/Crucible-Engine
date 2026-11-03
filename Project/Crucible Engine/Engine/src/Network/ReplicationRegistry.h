/*************************************************************************
* \file:         ReplicationRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                ReplicationRegistry: how each replicated piece of state is
*                written, decoded and committed, keyed by a stable protocol
*                id (NetComponentId).
*
* \details
*                NetComponentId is a PROTOCOL id: written in the source,
*                append-only, never reordered, unrelated to ComponentKind
*                (which teammates extend all the time). A prefab lists the
*                NetComponentIds it replicates (NetPrefabTable); a spawn or
*                snapshot record carries them in that order, concatenated.
*
*                A serializer replicates ONE ECS component's value (built
*                with MakeSerializer<Component, State, ...>):
*                  write   (server) appends its encoding;
*                  decode  (client) reads the encoding into a typed scratch
*                          State. No World: it cannot change the ECS. Its
*                          reader is bounded to its maxBytes and the record;
*                  ready   (optional) checks preconditions on a CONST World;
*                  commit  receives only a reference to its own component
*                          (and the decoded State) - no World - so all it can
*                          change is that one component's value. It may
*                          still fail or throw: the registry rolls back.
*                The built-in EnabledMask is the one exception: it switches
*                components on/off, and the registry commits it itself.
*
*                Applying a record is a transaction (ApplyRecord):
*                  1. decode every entry (bounded; nothing changes);
*                  2. check every precondition: entity alive, every target
*                     component present, every ready (nothing changes);
*                  3. save a copy of every target component and the old
*                     on/off flags (a copy may throw: still nothing changed);
*                  4. commit every entry in list order;
*                  5. if a commit fails or throws, restore every saved copy
*                     (move assignment, required noexcept) and flag, in
*                     reverse, and report failure.
*                So a record is applied completely or not at all. Nothing in
*                steps 4-5 can add, remove or destroy anything: commits have
*                no World, and the hooks that on/off changes fire may not
*                make structural changes - so every target outlives the
*                transaction. See ApplyRecord for the remaining limits.
*
*                schemaVersion is bumped by hand on ANY change to an
*                encoding; it is part of the content hash, so two builds that
*                encode differently refuse each other at join.
*
*                Size: maxBytes is a serializer's worst case and may not
*                exceed one record (kMaxRecordBytes, 255: a record's length
*                is one byte); NetPrefabTable::Validate also checks a
*                prefab's whole replicated list fits one record. Write holds
*                every serializer to its maxBytes at run time.
*
*                Names are std::string values owned by the registry (a
*                caller's string may die right after Register). The table is
*                a fixed std::array that never grows or moves, and the
*                registry cannot be copied or moved, so a pointer from Find()
*                - and the name inside it - stays valid for the registry's
*                whole life.
*
*                Built in (RegisterBuiltIns):
*                  TransformFull     position (3 x 24.8 fixed) + rotation
*                                    (smallest three): 16 bytes
*                  TransformYawOnly  position + yaw (u16): 14 bytes
*                  EnabledMask       on/off of the prefab's enabledReplicated
*                                    components, bit i = entry i: 1 byte
*
* \history
*    -v1.0       Networking Guide 5.4, 8.4, 18.3 issue 7 (PR 7); review
*                fixes: maxBytes limited to one record, bounded Write;
*                decode/commit, bounded readers, owned serializer names;
*                transactional records: per-component commits with saved
*                copies, rolled back when any commit fails or throws
*
**************************************************************************/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

#include "EngineExport.h"
#include "ECS/World.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	class ByteWriter;
	class ByteReader;
	class CanonicalHash;
	struct NetPrefabDesc;

	/// Stable protocol ids. Never reorder or reuse; append only. Game-defined ids start at 64.
	enum class NetComponentId : std::uint8_t
	{
		None             = 0,
		TransformFull    = 1,   // position + rotation (smallest three)
		TransformYawOnly = 2,   // position + yaw: upright characters and enemies
		EnabledMask      = 3,   // on/off state of the prefab's enabledReplicated components
		FirstGameId      = 64,
	};

	/// What a serializer may need besides the entity: its network prefab.
	struct ReplicationContext
	{
		const NetPrefabDesc* desc = nullptr;
		Net::NetTick         tick = 0;   // server tick of the record (commit)
	};

	/// The largest decoded state one serializer may use (its scratch slot).
	inline constexpr std::size_t kMaxDecodedStateBytes = 256;
	/// The largest component a serializer may replicate (the saved copy for rollback).
	inline constexpr std::size_t kMaxComponentCopyBytes = 512;

	/// One serializer. Build it with MakeSerializer, which fills every function from typed code.
	struct ReplicatedComponent
	{
		NetComponentId id            = NetComponentId::None;
		std::string    name;                                  // owned: the caller's string may die after Register
		std::uint16_t  schemaVersion = 1;                     // bump by hand on ANY encoding change
		ComponentId    local         = kInvalidComponentId;   // its ECS component (set by MakeSerializer)
		std::uint16_t  maxBytes      = 0;                     // worst-case encoded size, 1..kMaxRecordBytes
		std::uint16_t  stateBytes    = 0;                     // decoded state size, 1..kMaxDecodedStateBytes

		bool (*write)(const World& world, Entity entity, const ReplicationContext& ctx, ByteWriter& out) = nullptr;
		bool (*decode)(ByteReader& in, const ReplicationContext& ctx, void* state) = nullptr;                   // no World
		bool (*ready)(const World& world, Entity entity, const void* state, const ReplicationContext& ctx) = nullptr;   // optional, read-only

		// The component's value: reading, saving, committing into, and restoring it.
		const void* (*read)(const World& world, Entity entity) = nullptr;                    // null if absent
		void*       (*resolve)(World& world, Entity entity) = nullptr;                       // the live component, null if absent
		void        (*save)(const void* component, void* copy) = nullptr;                    // copy-construct into `copy` (may throw)
		void        (*restore)(void* component, void* copy) noexcept = nullptr;              // move `copy` back and destroy it
		void        (*discard)(void* copy) noexcept = nullptr;                               // destroy `copy`
		bool        (*commit)(void* component, const void* state, const ReplicationContext& ctx) = nullptr;

		bool enabledFlags = false;   // the built-in EnabledMask only: the registry commits on/off flags itself
	};

	/// Builds a serializer for one ECS component from typed functions:
	///   bool Decode(ByteReader&, const ReplicationContext&, State& out);                 // no World
	///   bool Commit(Component&, const State&, const ReplicationContext&);                // that component only
	///   bool Ready(const World&, Entity, const State&, const ReplicationContext&);       // optional, read-only
	/// Fill id, name, schemaVersion, maxBytes and write in `base`; `local` is set to Component.
	template <typename Component, typename State, auto Decode, auto Commit, auto Ready = nullptr>
	ReplicatedComponent MakeSerializer(ReplicatedComponent base)
	{
		static_assert(!WorldManaged<Component>::value && !WorldValidated<Component>::value,
			"only World may write this component, so it cannot be replicated by value");
		static_assert(std::is_copy_constructible_v<Component>, "rollback saves a copy of the component");
		static_assert(std::is_nothrow_move_assignable_v<Component> && std::is_nothrow_destructible_v<Component>,
			"rollback moves the saved copy back: that must not throw");
		static_assert(sizeof(Component) <= kMaxComponentCopyBytes && alignof(Component) <= alignof(std::max_align_t),
			"the component's saved copy must fit one slot");
		static_assert(std::is_trivially_copyable_v<State> && std::is_trivially_destructible_v<State>,
			"a serializer's decoded state lives in raw scratch memory");
		static_assert(sizeof(State) <= kMaxDecodedStateBytes && alignof(State) <= alignof(std::max_align_t),
			"a serializer's decoded state must fit one scratch slot");

		base.local = ComponentTypeId<Component>();
		base.stateBytes = static_cast<std::uint16_t>(sizeof(State));
		base.enabledFlags = false;
		base.decode = [](ByteReader& in, const ReplicationContext& ctx, void* state) -> bool {
			return Decode(in, ctx, *::new (state) State{});
		};
		if constexpr (!std::is_same_v<decltype(Ready), std::nullptr_t>)
		{
			base.ready = [](const World& world, Entity entity, const void* state, const ReplicationContext& ctx) -> bool {
				return Ready(world, entity, *static_cast<const State*>(state), ctx);
			};
		}
		base.read = [](const World& world, Entity entity) -> const void* { return world.Read<Component>(entity); };
		base.resolve = [](World& world, Entity entity) -> void* { return world.Write<Component>(entity); };
		base.save = [](const void* component, void* copy) { ::new (copy) Component(*static_cast<const Component*>(component)); };
		base.restore = [](void* component, void* copy) noexcept {
			Component& saved = *static_cast<Component*>(copy);
			*static_cast<Component*>(component) = std::move(saved);
			saved.~Component();
		};
		base.discard = [](void* copy) noexcept { static_cast<Component*>(copy)->~Component(); };
		base.commit = [](void* component, const void* state, const ReplicationContext& ctx) -> bool {
			return Commit(*static_cast<Component*>(component), *static_cast<const State*>(state), ctx);
		};
		return base;
	}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // std::array member of an exported class: used only inside Engine.dll
#endif

	class ENGINE_API ReplicationRegistry
	{
	public:
		ReplicationRegistry() = default;
		// Find() hands out pointers into the table: the registry must never be copied or moved.
		ReplicationRegistry(const ReplicationRegistry&) = delete;
		ReplicationRegistry& operator=(const ReplicationRegistry&) = delete;

		/// Adds a copy of `component` (its name included). Refuses id None, an id already registered
		/// (the message names both), an empty name, maxBytes 0 or over kMaxRecordBytes, stateBytes 0
		/// or over kMaxDecodedStateBytes, a missing function (build it with MakeSerializer), or
		/// enabledFlags on anything but the built-in EnabledMask.
		bool Register(const ReplicatedComponent& component, std::string& error);

		/// TransformFull, TransformYawOnly and EnabledMask.
		bool RegisterBuiltIns(std::string& error);

		const ReplicatedComponent* Find(NetComponentId id) const noexcept
		{
			const ReplicatedComponent& c = m_table[static_cast<std::uint8_t>(id)];
			return c.id == NetComponentId::None ? nullptr : &c;
		}

		/// Feeds every serializer in NetComponentId order: id, name, schemaVersion, maxBytes.
		void HashInto(CanonicalHash& hash) const;

		// ---- one component -------------------------------------------------------------------

		/// Appends component `id`'s encoding to `out`. The serializer writes into a scratch buffer of
		/// exactly maxBytes, so writing more fails instead of overrunning the record. False - with
		/// the serializer named in *error - if it is not registered, fails, writes too much, or
		/// `out` has no room; `out` then holds nothing of it.
		bool Write(NetComponentId id, const World& world, Entity entity, const ReplicationContext& ctx,
			ByteWriter& out, std::string* error = nullptr) const;

		/// Decodes component `id` at the reader's position, through a reader bounded to its maxBytes,
		/// without touching any World. On success `in` moves past exactly what was decoded; on
		/// failure `in` is unchanged.
		bool Validate(NetComponentId id, ByteReader& in, const ReplicationContext& ctx) const;

		/// A one-entry transaction (see ApplyRecord). On failure the entity and `in` are unchanged.
		bool Apply(NetComponentId id, World& world, Entity entity, ByteReader& in, const ReplicationContext& ctx,
			std::string* error = nullptr) const;

		// ---- one record: a prefab's replicated list (ctx.desc), bit i of mask = entry i present ----

		/// Writes the entries `mask` selects, in list order. All or nothing: on failure `out` holds
		/// nothing of the record.
		bool WriteRecord(const World& world, Entity entity, std::uint8_t mask, const ReplicationContext& ctx,
			ByteWriter& out, std::string* error = nullptr) const;

		/// True if `body` is exactly the entries `mask` selects, each decoding within its bound.
		/// No World access.
		bool ValidateRecord(std::uint8_t mask, std::span<const std::byte> body, const ReplicationContext& ctx) const;

		/// Applies the record as one transaction (see the file notes): completely, or - on any
		/// decode or precondition failure, a failed or throwing commit, or a failed copy - not at
		/// all. Never throws.
		/// Limits: on/off changes fire OnComponentActiveChanged hooks, whose own side effects (and
		/// those of restoring the old state) are not undone; a rolled-back component still counts
		/// as written for change detection; a commit with undefined behaviour (writing outside
		/// its component) cannot be undone.
		bool ApplyRecord(World& world, Entity entity, std::uint8_t mask, std::span<const std::byte> body,
			const ReplicationContext& ctx, std::string* error = nullptr) const noexcept;

		/// The same, for a record of `length` bytes at the reader's position: `in` moves past it only
		/// if the whole record was applied.
		bool ApplyRecord(World& world, Entity entity, std::uint8_t mask, ByteReader& in, std::size_t length,
			const ReplicationContext& ctx, std::string* error = nullptr) const noexcept;

	private:
		struct Entry;
		bool DecodeBounded(const ReplicatedComponent& c, ByteReader& in, const ReplicationContext& ctx, void* state) const;
		static bool Transact(World& world, Entity entity, std::span<Entry> entries, const ReplicationContext& ctx, std::string* error) noexcept;

		std::array<ReplicatedComponent, 256> m_table{};
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
