/*************************************************************************
* \file:         NetPrefabTable.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.4
* \brief
*                NetPrefabTable (see the header).
*
* \history
*    -v1.0       network prefab table with explicit ids; review
*                fixes: IsRegistered before metadata, record-size limit
*    -v1.1       sendPriority checked
*    -v1.2       a known prefabGuid is used instead of the path; Remove,
*                FindByGuid, FindByName, HighestId (Networking Phase 14.2)
*    -v1.3       a serializer whose component is added at runtime needs none in the file
*    -v1.4       a Collider or CharacterController may be server-only AND enabledReplicated:
*                a client's query body follows its on/off (Networking C# Phase 8)
*
**************************************************************************/
#include "Network/NetPrefabTable.h"

#include <algorithm>
#include <format>
#include <string_view>

#include "AssetPipe/Assetmanager.h"
#include "ECS/Components/ComponentRegistry.h"
#include "ECS/Components/Components.h"   // Collider, CharacterController
#include "Json/JsonWrapper.h"
#include "Network/CanonicalHash.h"
#include "Network/Protocol.h"
#include "Resources/ResourceManager.h"
#include "Scene/Prefab.h"

namespace Crucible
{
	namespace
	{
		constexpr std::size_t kMaxListEntries = 8;   // records and EnabledMask carry one bit per entry in a u8

		template <typename T>
		bool HasDuplicates(const std::vector<T>& list)
		{
			for (std::size_t i = 0; i < list.size(); ++i)
				for (std::size_t j = i + 1; j < list.size(); ++j)
					if (list[i] == list[j]) return true;
			return false;
		}

		bool Overlap(const std::vector<ComponentId>& a, const std::vector<ComponentId>& b)
		{
			return std::ranges::any_of(a, [&](ComponentId id) { return std::ranges::find(b, id) != b.end(); });
		}

		bool Contains(const std::vector<NetComponentId>& list, NetComponentId id)
		{
			return std::ranges::find(list, id) != list.end();
		}

		std::string Where(Net::NetPrefabId id, const NetPrefabDesc& desc)
		{
			return std::format("network prefab '{}' (id {})", desc.name, id);
		}
	}

	bool NetPrefabTable::Register(Net::NetPrefabId id, NetPrefabDesc desc, std::string& error)
	{
		if (id >= kMaxNetPrefabs) { error = std::format("network prefab '{}': id {} is not below {}", desc.name, id, kMaxNetPrefabs); return false; }
		if (m_entries[id]) { error = std::format("network prefab id {} is already '{}'; cannot also register '{}'", id, m_entries[id]->desc.name, desc.name); return false; }
		if (desc.name.empty()) { error = std::format("network prefab id {} has no name", id); return false; }
		for (std::size_t other = 0; other < kMaxNetPrefabs; ++other)
		{
			if (m_entries[other] && m_entries[other]->desc.name == desc.name)
			{
				error = std::format("network prefab name '{}' is already id {}; cannot also use it for id {}", desc.name, other, id);
				return false;
			}
		}
		const std::string where = Where(id, desc);
		if (desc.prefabPath.empty()) { error = where + ": no prefab path"; return false; }
		if (!(desc.sendPriority > 0.f && desc.sendPriority <= kMaxSendPriority))   // also refuses NaN
		{
			error = std::format("{}: sendPriority {} is not in (0, {}]", where, desc.sendPriority, kMaxSendPriority);
			return false;
		}

		if (desc.replicated.size() > kMaxListEntries || desc.enabledReplicated.size() > kMaxListEntries)
		{
			error = std::format("{}: at most {} replicated and {} enabledReplicated entries", where, kMaxListEntries, kMaxListEntries);
			return false;
		}
		if (Contains(desc.replicated, NetComponentId::None)) { error = where + ": 'None' in its replicated list"; return false; }
		const auto invalid = [](ComponentId c) { return c == kInvalidComponentId || c >= kMaxComponents; };
		if (std::ranges::any_of(desc.serverOnly, invalid) || std::ranges::any_of(desc.ownerLocalOnly, invalid)
			|| std::ranges::any_of(desc.enabledReplicated, invalid))
		{
			error = where + ": an invalid component id in serverOnly, ownerLocalOnly or enabledReplicated";
			return false;
		}
		if (HasDuplicates(desc.replicated) || HasDuplicates(desc.serverOnly) || HasDuplicates(desc.ownerLocalOnly) || HasDuplicates(desc.enabledReplicated))
		{
			error = where + ": a list names the same entry twice";
			return false;
		}
		// Replicating the on/off state of a component that each machine switches itself would
		// fight: the server's "physics on" would switch client physics back on. Except the
		// physics a client has query bodies for (Phase 8): a server-only Collider or
		// CharacterController with its on/off replicated stays off on clients, and their query
		// body follows the host's on/off instead (ReplicationRegistry, EnabledMask).
		const auto queryBodySource = [](ComponentId id) {
			return id == ComponentTypeId<Collider>() || id == ComponentTypeId<CharacterController>();
		};
		const bool serverOnlyClash = std::ranges::any_of(desc.enabledReplicated, [&](ComponentId id) {
			return !queryBodySource(id) && std::ranges::find(desc.serverOnly, id) != desc.serverOnly.end();
		});
		if (serverOnlyClash || Overlap(desc.enabledReplicated, desc.ownerLocalOnly))
		{
			error = where + ": a component in enabledReplicated is also server-only or owner-local"
				" (only a Collider or CharacterController may be both server-only and synced)";
			return false;
		}
		if (Overlap(desc.serverOnly, desc.ownerLocalOnly)) { error = where + ": a component is both server-only and owner-local"; return false; }
		if (Contains(desc.replicated, NetComponentId::EnabledMask) != !desc.enabledReplicated.empty())
		{
			error = where + ": EnabledMask must be replicated exactly when enabledReplicated lists components";
			return false;
		}

		m_entries[id] = Entry{ .desc = std::move(desc), .prefab = nullptr };
		++m_count;
		return true;
	}

	bool NetPrefabTable::Attach(Net::NetPrefabId id, std::shared_ptr<const Prefab> prefab, std::string& error)
	{
		if (id >= kMaxNetPrefabs || !m_entries[id]) { error = std::format("no network prefab with id {}", id); return false; }
		m_entries[id]->prefab = std::move(prefab);
		return true;
	}

	bool NetPrefabTable::LoadPrefabs(const AssetManager& assets, ResourceManager& resources, std::string& error)
	{
		for (std::size_t id = 0; id < kMaxNetPrefabs; ++id)
		{
			if (!m_entries[id]) continue;
			Entry& entry = *m_entries[id];
			entry.prefab = nullptr;
			const std::string where = Where(static_cast<Net::NetPrefabId>(id), entry.desc);
			const std::uint64_t guid = entry.desc.prefabGuid != 0 ? entry.desc.prefabGuid : assets.GetGuid(entry.desc.prefabPath).m_Value;
			if (guid == 0) { error = std::format("{}: '{}' is not a prefab asset in the project", where, entry.desc.prefabPath); return false; }
			std::shared_ptr<Prefab> prefab = resources.ReadPrefab(guid);   // read fresh: the file's current content hash
			if (!prefab) { error = std::format("{}: '{}' could not be loaded", where, entry.desc.prefabPath); return false; }
			entry.prefab = std::move(prefab);
		}
		return true;
	}

	bool NetPrefabTable::Validate(const ReplicationRegistry& serializers, const ComponentRegistry& components, std::string& error) const
	{
		for (std::size_t id = 0; id < kMaxNetPrefabs; ++id)
		{
			if (!m_entries[id]) continue;
			const Entry& entry = *m_entries[id];
			const NetPrefabDesc& desc = entry.desc;
			const std::string where = Where(static_cast<Net::NetPrefabId>(id), desc);

			// Loaded from a registered asset: its identity is part of the content hash.
			const Prefab* prefab = entry.prefab.get();
			if (prefab == nullptr || !prefab->IsValid()) { error = std::format("{}: '{}' is not loaded", where, desc.prefabPath); return false; }
			if (prefab->AssetGuid() == 0 || prefab->ContentHash() == 0)
			{
				error = std::format("{}: '{}' was not loaded from a registered asset (no asset guid or content hash)", where, desc.prefabPath);
				return false;
			}
			std::size_t root = 0;
			while (root < prefab->EntityCount() && prefab->ParentAt(root) != -1) ++root;
			if (root == prefab->EntityCount()) { error = where + ": the prefab has no root entity"; return false; }
			const std::string rootComponents = prefab->ComponentsPath(root);
			const auto onRoot = [&](ComponentId c) { return components.IsRegistered(c) && prefab->Document().Has(rootComponents + "/" + std::string(components.Get(c).name)); };

			// The whole replicated list is one record (a spawn carries every entry; a snapshot a
			// subset): its state must fit the record's one-byte length. `total` never exceeds
			// kMaxRecordBytes, so `limit - total` cannot wrap and the check cannot overflow.
			std::size_t total = 0;
			for (const NetComponentId netId : desc.replicated)
			{
				const ReplicatedComponent* serializer = serializers.Find(netId);
				if (serializer == nullptr) { error = std::format("{}: replicates serializer id {}, which is not registered", where, static_cast<int>(netId)); return false; }
				if (serializer->local != kInvalidComponentId)
				{
					if (!components.IsRegistered(serializer->local))
					{
						error = std::format("{}: serializer '{}' (id {}) uses component id {}, which is not registered",
							where, serializer->name, static_cast<int>(netId), serializer->local);
						return false;
					}
					if (!serializer->addedAtRuntime && !onRoot(serializer->local))   // a runtime one is never in the file
					{
						error = std::format("{}: its root has no '{}' (needed by serializer '{}')", where, components.Get(serializer->local).name, serializer->name);
						return false;
					}
				}
				if (serializer->maxBytes > kMaxRecordBytes - total)
				{
					// The full figure for the message: at most 8 x 255 bytes, so no overflow.
					std::size_t needed = 0;
					std::string parts;
					for (const NetComponentId listed : desc.replicated)
					{
						const ReplicatedComponent* s = serializers.Find(listed);
						if (s == nullptr) continue;
						needed += s->maxBytes;
						parts += std::format("{}{} {}", parts.empty() ? "" : " + ", s->name, s->maxBytes);
					}
					error = std::format("{}: its replicated state can need {} bytes ({}), but one spawn record holds at most {}",
						where, needed, parts, kMaxRecordBytes);
					return false;
				}
				total += serializer->maxBytes;
			}

			struct List { const std::vector<ComponentId>* ids; const char* name; bool rootOnly; };
			for (const List& list : { List{ &desc.serverOnly, "serverOnly", false }, List{ &desc.ownerLocalOnly, "ownerLocalOnly", false }, List{ &desc.enabledReplicated, "enabledReplicated", true } })
			{
				for (const ComponentId c : *list.ids)
				{
					if (!components.IsRegistered(c)) { error = std::format("{}: {} names component id {}, which is not registered", where, list.name, c); return false; }
					const std::string_view name = components.Get(c).name;
					if (!components.Get(c).toggleable) { error = std::format("{}: '{}' (in {}) cannot be switched off", where, name, list.name); return false; }
					bool present = onRoot(c);
					// Proxy policies apply to the whole subtree; EnabledMask addresses only the root.
					if (!list.rootOnly)
						for (std::size_t i = 0; !present && i < prefab->EntityCount(); ++i)
							present = prefab->Document().Has(prefab->ComponentsPath(i) + "/" + std::string(name));
					if (!present) { error = std::format("{}: its {} has no '{}' (listed in {})", where, list.rootOnly ? "root" : "hierarchy", name, list.name); return false; }
				}
			}
		}
		return true;
	}

	bool NetPrefabTable::Remove(Net::NetPrefabId id)
	{
		if (id >= kMaxNetPrefabs || !m_entries[id]) return false;
		m_entries[id].reset();
		--m_count;
		return true;
	}

	Net::NetPrefabId NetPrefabTable::FindByGuid(std::uint64_t prefabGuid) const noexcept
	{
		if (prefabGuid == 0) return Net::kInvalidNetPrefab;
		for (std::size_t id = 0; id < kMaxNetPrefabs; ++id)
		{
			if (!m_entries[id]) continue;
			const Entry& e = *m_entries[id];
			if (e.desc.prefabGuid == prefabGuid || (e.prefab && e.prefab->AssetGuid() == prefabGuid)) return static_cast<Net::NetPrefabId>(id);
		}
		return Net::kInvalidNetPrefab;
	}

	Net::NetPrefabId NetPrefabTable::FindByName(std::string_view name) const noexcept
	{
		for (std::size_t id = 0; id < kMaxNetPrefabs; ++id)
			if (m_entries[id] && m_entries[id]->desc.name == name) return static_cast<Net::NetPrefabId>(id);
		return Net::kInvalidNetPrefab;
	}

	Net::NetPrefabId NetPrefabTable::HighestId() const noexcept
	{
		for (std::size_t id = kMaxNetPrefabs; id-- > 0;)
			if (m_entries[id]) return static_cast<Net::NetPrefabId>(id);
		return 0;
	}

	const NetPrefabDesc* NetPrefabTable::Desc(Net::NetPrefabId id) const noexcept
	{
		return id < kMaxNetPrefabs && m_entries[id] ? &m_entries[id]->desc : nullptr;
	}

	const Prefab* NetPrefabTable::PrefabOf(Net::NetPrefabId id) const noexcept
	{
		return id < kMaxNetPrefabs && m_entries[id] ? m_entries[id]->prefab.get() : nullptr;
	}

	void NetPrefabTable::HashInto(CanonicalHash& hash, const ComponentRegistry& components) const
	{
		// Validate has refused unregistered ids already; without it, they hash as a fixed marker
		// instead of throwing.
		const auto names = [&](const std::vector<ComponentId>& ids, bool sorted) {
			std::vector<std::string_view> out;
			for (const ComponentId c : ids) out.push_back(components.IsRegistered(c) ? components.Get(c).name : std::string_view("<unregistered>"));
			if (sorted) std::ranges::sort(out);
			hash.U32(static_cast<std::uint32_t>(out.size()));
			for (const std::string_view n : out) hash.Str(n);
		};

		hash.Str("prefabs");
		for (std::size_t id = 0; id < kMaxNetPrefabs; ++id)
		{
			if (!m_entries[id]) continue;
			const Entry& entry = *m_entries[id];
			hash.U32(static_cast<std::uint32_t>(id));
			hash.Str(entry.desc.name);
			hash.U64(entry.prefab ? entry.prefab->AssetGuid() : 0);
			hash.U64(entry.desc.contentHash != 0 ? entry.desc.contentHash : (entry.prefab ? entry.prefab->ContentHash() : 0));
			hash.U32(static_cast<std::uint32_t>(entry.desc.replicated.size()));   // record order matters
			for (const NetComponentId c : entry.desc.replicated) hash.U8(static_cast<std::uint8_t>(c));
			names(entry.desc.serverOnly, true);          // sets: order does not matter
			names(entry.desc.ownerLocalOnly, true);
			names(entry.desc.enabledReplicated, false);  // bit i = entry i: order matters
		}
	}
}
