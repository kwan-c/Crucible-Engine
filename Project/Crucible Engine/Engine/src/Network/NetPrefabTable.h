/*************************************************************************
* \file:         NetPrefabTable.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.6
* \brief
*                NetPrefabTable: which prefabs can be spawned over the
*                network, under which explicit NetPrefabId, and what of each
*                one replicates.
*
* \details
*                Ids are written in the source (GamePrefabs.h), never taken
*                from registration order, so moving a Register call cannot
*                renumber prefabs.
*
*                Three steps, each stricter than the last:
*                  Register        the description alone: id range,
*                                  duplicates, list sizes and overlaps.
*                  LoadPrefabs     reads each .cprefab through the asset
*                                  system, so it has an asset guid and a
*                                  file content hash (both hashed).
*                  Validate        against the serializers and the ECS: every
*                                  replicated id is registered, every
*                                  component id is registered (checked before
*                                  its metadata is read), every listed
*                                  policy component exists in the hierarchy
*                                  and can be switched off (enabledReplicated
*                                  still requires the root), and the replicated
*                                  serializers' maxBytes together fit one
*                                  spawn record (kMaxRecordBytes, 1024 since
*                                  protocol 9; at most 16 entries). A session
*                                  refuses to start while this fails.
*                HashInto feeds the canonical content hash: every
*                prefab in id order with its asset identity and lists, by
*                component NAME, so a teammate inserting a ComponentKind
*                does not change it on its own.
*
* \history
*    -v1.0       network prefab table with explicit ids; review
*                fixes: unregistered component ids reported, not thrown;
*                a prefab's replicated state must fit one record
*    -v1.1       sendPriority and critical: host-side snapshot scheduling,
*                outside the content hash
*    -v1.2       prefabGuid (found by asset GUID: renames and moves are
*                harmless); Remove, FindByGuid, FindByName, HighestId for
*                discovered network prefabs (Networking Phase 14.2)
*    -v1.3       a Collider or CharacterController may be both serverOnly and
*                enabledReplicated (Networking C# Phase 8: query bodies)
*    -v1.4       protocol 9: up to 16 replicated entries, records up to kMaxRecordBytes (1024)
*    -v1.5       NetPrefabDesc::syncedChildren: checked by Register (well-formed, at most 16, none twice,
*                used by a serializer) and Validate (each resolves to one entry of the prefab); hashed
*    -v1.6       NetPrefabDesc::childEnabledReplicated: checked, validated, hashed
*
**************************************************************************/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "EngineExport.h"
#include "ECS/Components/ComponentTypeId.h"
#include "Network/NetTypes.h"
#include "Network/ReplicationRegistry.h"

namespace Crucible
{
	class AssetManager;
	class ResourceManager;
	class ComponentRegistry;
	class Prefab;

	/// The largest number of network prefabs (ids 0..255).
	inline constexpr std::size_t kMaxNetPrefabs = 256;

	/// One network prefab.
	struct NetPrefabDesc
	{
		std::string                 name;                // "Grunt": logs and the hash
		std::string                 prefabPath;          // asset path, e.g. "Prefabs/Grunt.cprefab"
		std::uint64_t               prefabGuid = 0;      // the asset's GUID if known (found by it, not the path)
		std::uint64_t               contentHash = 0;     // when set, hashed instead of the file's content hash
		                                                 // (authored prefabs: their scheduling fields left out)
		std::vector<NetComponentId> replicated;          // record order: spawns and snapshots carry these
		std::vector<ComponentId>    serverOnly;          // switched off on client proxies (physics, AI)
		std::vector<ComponentId>    ownerLocalOnly;      // switched off unless owned by this machine (camera)
		std::vector<ComponentId>    enabledReplicated;   // whose on/off state EnabledMask carries, bit i = entry i
		std::vector<std::string>    syncedChildren;      // child name paths below the root, slot i = entry i
		                                                 // (Children/..., Renderers and Children/OnOff use them)
		std::vector<ComponentId>    childEnabledReplicated;   // on/off synced on the synced children, bit j =
		                                                 // entry j (Children/OnOff, at most 8)

		// Snapshot scheduling (host only: never on the wire, not in the content hash, so builds
		// that differ only here still play together). See NetReplicationSendSystem.h.
		float sendPriority = 1.f;   // weight while it waits for a snapshot slot; > 0, at most kMaxSendPriority
		bool  critical = false;     // every snapshot, ahead of the budget (players, a boss, a ball...)
	};

	/// The largest NetPrefabDesc::sendPriority.
	inline constexpr float kMaxSendPriority = 1000.f;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members of an exported class: used only inside Engine.dll
#endif

	class ENGINE_API NetPrefabTable
	{
	public:
		/// Adds `desc` as prefab `id`. Refuses: an id >= kMaxNetPrefabs, an id or name already
		/// registered (the message names both), an empty name or path, more than 8 replicated or
		/// enabledReplicated entries, a duplicate or invalid entry in any list, enabledReplicated
		/// overlapping serverOnly (except a Collider or CharacterController: clients' query bodies
		/// follow its on/off) or ownerLocalOnly, serverOnly overlapping ownerLocalOnly, and
		/// EnabledMask without enabledReplicated (or the other way round).
		bool Register(Net::NetPrefabId id, NetPrefabDesc desc, std::string& error);

		/// Reads every registered prefab's file fresh through the asset system. False (with the
		/// prefab named) if a path is not a registered prefab asset or does not load.
		bool LoadPrefabs(const AssetManager& assets, ResourceManager& resources, std::string& error);

		/// Uses an already loaded prefab for `id` instead (LoadPrefabs does this for each id).
		bool Attach(Net::NetPrefabId id, std::shared_ptr<const Prefab> prefab, std::string& error);

		/// Drops prefab `id` (discovered prefabs are found afresh before every session). False if
		/// there was none.
		bool Remove(Net::NetPrefabId id);

		/// The id registered for an asset GUID or a name, or kInvalidNetPrefab.
		Net::NetPrefabId FindByGuid(std::uint64_t prefabGuid) const noexcept;
		Net::NetPrefabId FindByName(std::string_view name) const noexcept;

		/// The highest registered id, 0 if none.
		Net::NetPrefabId HighestId() const noexcept;

		/// Everything a session needs before it may start (see the file notes). Never throws for a
		/// configuration mistake: the message names the prefab, its id and the component or
		/// serializer at fault.
		bool Validate(const ReplicationRegistry& serializers, const ComponentRegistry& components, std::string& error) const;

		const NetPrefabDesc* Desc(Net::NetPrefabId id) const noexcept;
		const Prefab*        PrefabOf(Net::NetPrefabId id) const noexcept;
		std::size_t          Count() const noexcept { return m_count; }

		/// Feeds every prefab in id order (see the file notes). Call after Validate.
		void HashInto(CanonicalHash& hash, const ComponentRegistry& components) const;

	private:
		struct Entry
		{
			NetPrefabDesc                 desc;
			std::shared_ptr<const Prefab> prefab;   // set by LoadPrefabs / Attach
		};
		std::array<std::optional<Entry>, kMaxNetPrefabs> m_entries;
		std::size_t                                       m_count = 0;
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
