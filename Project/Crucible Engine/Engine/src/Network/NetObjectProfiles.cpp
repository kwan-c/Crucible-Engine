/*************************************************************************
* \file:         NetObjectProfiles.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          6 November 2026
* \version       1.9
* \brief
*                NetworkObjectAuthoring -> NetPrefabDesc, and network prefab
*                discovery (see the header).
*
* \history
*    -v1.0       Networking Phase 14.2
*    -v1.1       AuthoringOptions, CheckAuthoring (Networking Phase 14.3)
*    -v1.2       NetVariables offered although the object has none in the editor
*                (Networking C# Phase 4)
*    -v1.3       NetAnimator offered the same way (Networking C# Phase 7)
*    -v1.4       synced children: BuildProfile copies and checks the list,
*                CheckAuthoring resolves each path; runtime-added serializers grouped one row per component
*    -v1.5       CheckAuthoring: a renderer beyond Renderers' first 7 synced children is reported
*    -v1.6       CheckAuthoring reports a record that cannot fit (WorstCaseRecordBytes), as a session start would
*    -v1.7       childEnabledReplicated: BuildProfile, CheckAuthoring, belowRoot
*    -v1.8       NetData is a network runtime component
*    -v1.9       several spawnPerPlayer prefabs: no longer an error; the first is the default player
*
**************************************************************************/
#include "Network/NetObjectProfiles.h"

#include <algorithm>
#include <format>
#include <memory>

#include "ECS/Components/ComponentRegistry.h"
#include "ECS/World.h"
#include "Json/JsonWrapper.h"
#include "Network/CanonicalHash.h"
#include "Network/NetComponents.h"
#include "Network/NetSyncedChildren.h"
#include "Network/ReplicationRegistry.h"
#include "Scene/EntityIO.h"
#include "Scene/Prefab.h"

namespace Crucible
{
	namespace
	{
		constexpr const char* kAuthoringName = "NetworkObjectAuthoring";

		bool ComponentByName(const ComponentRegistry& components, const std::string& name, ComponentId& out)
		{
			for (std::size_t i = 0; i < kMaxComponents; ++i)
			{
				const auto id = static_cast<ComponentId>(i);
				if (components.IsRegistered(id) && components.Get(id).name == name) { out = id; return true; }
			}
			return false;
		}

		bool Components(const ComponentRegistry& components, const std::vector<std::string>& names, const char* field,
			std::vector<ComponentId>& out, std::string& error)
		{
			out.clear();
			for (const std::string& name : names)
			{
				ComponentId id = kInvalidComponentId;
				if (!ComponentByName(components, name, id))
				{
					error = std::format("{}: '{}' is not a component", field, name);
					return false;
				}
				out.push_back(id);
			}
			return true;
		}
	}

	// The prefab's content for the content hash: its whole document, but without the root's
	// sendPriority and critical - host-side scheduling, which two machines may differ in (Phase 13).
	// Everything else in the file counts: components, values, hierarchy, the other authored settings.
	std::uint64_t SchemaHash(const Prefab& prefab)
	{
		JsonWrapper copy;
		if (!copy.ReadFromString(prefab.Document().ToString(false))) return prefab.ContentHash();
		const std::string authoring = prefab.ComponentsPath(0) + "/" + kAuthoringName;
		copy.Remove(authoring + "/sendPriority");
		copy.Remove(authoring + "/critical");
		CanonicalHash hash;
		hash.Str("Crucible.NetPrefab");
		hash.U64(prefab.AssetGuid());
		hash.Str(copy.ToString(false));
		const std::uint64_t value = hash.Value();
		return value != 0 ? value : 1;
	}

	bool BuildProfile(const NetworkObjectAuthoring& authoring, const ComponentRegistry& components,
		const ReplicationRegistry& serializers, NetPrefabDesc& out, std::string& error)
	{
		out.replicated.clear();
		for (const std::string& name : authoring.replicated)
		{
			const ReplicatedComponent* serializer = serializers.FindByName(name);
			if (serializer == nullptr)
			{
				error = std::format("replicated: '{}' is not a registered serializer", name);
				return false;
			}
			out.replicated.push_back(serializer->id);
		}
		if (!Components(components, authoring.serverOnly, "serverOnly", out.serverOnly, error)) return false;
		if (!Components(components, authoring.ownerLocalOnly, "ownerLocalOnly", out.ownerLocalOnly, error)) return false;
		if (!Components(components, authoring.enabledReplicated, "enabledReplicated", out.enabledReplicated, error)) return false;
		if (!Components(components, authoring.childEnabledReplicated, "childEnabledReplicated", out.childEnabledReplicated, error)) return false;
		if (!CheckSyncedChildList(authoring.syncedChildren, error)) return false;
		out.syncedChildren = authoring.syncedChildren;
		out.sendPriority = authoring.sendPriority;
		out.critical = authoring.critical;
		return true;
	}

	AuthoringRead ReadPrefabAuthoring(const Prefab& prefab, NetworkObjectAuthoring& out, std::string& error)
	{
		if (!prefab.IsValid() || prefab.EntityCount() == 0) return AuthoringRead::None;
		const JsonWrapper& doc = prefab.Document();
		for (std::size_t i = 1; i < prefab.EntityCount(); ++i)
		{
			if (doc.Has(prefab.ComponentsPath(i) + "/" + kAuthoringName))
			{
				error = std::format("entry {} below the root has NetworkObjectAuthoring too: a network object cannot contain another "
					"(keep it on the root only, or unpack the nested one)", i);
				return AuthoringRead::Invalid;
			}
		}
		if (!doc.Has(prefab.ComponentsPath(0) + "/" + kAuthoringName)) return AuthoringRead::None;

		// Read through EntityIO into a scratch entity: exactly how a scene or prefab load reads it.
		World scratch;
		const Entity e = scratch.CreateEntity();
		EntityIO::ReadComponents(scratch, e, doc, prefab.ComponentsPath(0));
		const NetworkObjectAuthoring* authoring = scratch.Read<NetworkObjectAuthoring>(e);
		if (authoring == nullptr)
		{
			error = "its NetworkObjectAuthoring could not be read";
			return AuthoringRead::Invalid;
		}
		out = *authoring;
		return AuthoringRead::Found;
	}

	namespace
	{
		bool Has(const World& world, Entity e, ComponentId id)
		{
			return world.IsAlive(e) && id < kMaxComponents && world.GetSignature(e).test(id);
		}

		// root and every descendant, breadth first
		void Subtree(const World& world, Entity root, std::vector<Entity>& out)
		{
			out.push_back(root);
			for (std::size_t i = out.size() - 1; i < out.size(); ++i)
				world.ForEachChild(out[i], [&](Entity child) { out.push_back(child); });
		}

		bool InSubtree(const World& world, const std::vector<Entity>& subtree, ComponentId id)
		{
			return std::ranges::any_of(subtree, [&](Entity e) { return Has(world, e, id); });
		}

		bool IsNetworkRuntime(ComponentId id)
		{
			return id == ComponentTypeId<NetworkIdentity>() || id == ComponentTypeId<NetInterpolation>()
				|| id == ComponentTypeId<PlayerInput>() || id == ComponentTypeId<NetworkObjectAuthoring>()
				|| id == ComponentTypeId<NetVariables>() || id == ComponentTypeId<NetAnimator>()
				|| id == ComponentTypeId<NetChildTransforms>() || id == ComponentTypeId<NetAttachment>()
				|| id == ComponentTypeId<NetRenderers>() || id == ComponentTypeId<NetChildEnabled>()
				|| id == ComponentTypeId<NetData>();
		}
	}

	std::vector<AuthoringComponentOption> AuthoringOptions(const World& world, Entity root,
		const ComponentRegistry& components, const ReplicationRegistry& serializers)
	{
		std::vector<AuthoringComponentOption> out;
		if (!world.IsAlive(root)) return out;
		std::vector<Entity> subtree;
		Subtree(world, root, subtree);
		for (std::size_t i = 0; i < kMaxComponents; ++i)
		{
			const auto id = static_cast<ComponentId>(i);
			if (!components.IsRegistered(id) || IsNetworkRuntime(id) || !InSubtree(world, subtree, id)) continue;
			AuthoringComponentOption option;
			option.name = std::string(components.Get(id).name);
			option.onRoot = Has(world, root, id);
			option.belowRoot = std::ranges::any_of(subtree, [&](Entity e) { return e != root && Has(world, e, id); });
			option.toggleable = components.Get(id).toggleable;
			serializers.ForEach([&](const ReplicatedComponent& s) { if (s.local == id) option.serializers.push_back(s.name); });
			if (option.serializers.empty()) option.noReplication = "no registered serializer for this component";
			else if (!option.onRoot) option.noReplication = "only the object's root is replicated; this component is below it";
			if (!option.toggleable) option.noPolicy = "this component cannot be switched off";
			if (!option.toggleable) option.noEnabledSync = "this component cannot be switched off";
			else if (!option.onRoot) option.noEnabledSync = "only the root's on/off states are replicated";
			// Nothing to choose (no serializer, cannot be switched off - EntityName, the hierarchy
			// links, WorldTransform): always Local, so not offered.
			if (option.serializers.empty() && !option.toggleable) continue;
			out.push_back(std::move(option));
		}
		// Components the network layer adds itself while a session runs (NetVariables): the object
		// never has one in the editor, but whether it replicates them is still its choice. One row
		// per component, with every serializer of it (Children/Rotation and Children/Full).
		serializers.ForEach([&](const ReplicatedComponent& s) {
			if (!s.addedAtRuntime || !components.IsRegistered(s.local)) return;
			if (s.id == NetComponentId::ChildOnOff) return;   // set by the Sync on/off column, like EnabledMask
			const std::string name(components.Get(s.local).name);
			const auto existing = std::ranges::find_if(out, [&](const AuthoringComponentOption& o) { return o.name == name; });
			if (existing != out.end()) { existing->serializers.push_back(s.name); return; }
			AuthoringComponentOption option;
			option.name = name;
			option.onRoot = true;
			option.toggleable = false;
			option.serializers.push_back(s.name);
			option.noPolicy = "the network adds this one itself on every machine";
			option.noEnabledSync = option.noPolicy;
			out.push_back(std::move(option));
		});
		return out;
	}

	std::vector<std::string> CheckAuthoring(const World& world, Entity root, const NetworkObjectAuthoring& authoring,
		const ComponentRegistry& components, const ReplicationRegistry& serializers)
	{
		std::vector<std::string> problems;
		if (!world.IsAlive(root)) { problems.push_back("the entity is gone"); return problems; }

		// The names, then the table's own rules (exactly what a session start applies).
		NetPrefabDesc desc;
		std::string error;
		if (!BuildProfile(authoring, components, serializers, desc, error)) { problems.push_back(error); return problems; }
		desc.name = "this object";
		desc.prefabPath = "(editor)";
		const auto table = std::make_unique<NetPrefabTable>();   // two fixed 256-entry tables: never on the stack
		if (!table->Register(0, desc, error)) problems.push_back(error);

		// What the object itself must have.
		std::vector<Entity> subtree;
		Subtree(world, root, subtree);
		for (const NetComponentId sid : desc.replicated)
		{
			const ReplicatedComponent* s = serializers.Find(sid);
			if (s != nullptr && s->local != kInvalidComponentId && !s->addedAtRuntime && !Has(world, root, s->local))
				problems.push_back(std::format("replicated: '{}' needs a {} on this entity", s->name, components.Get(s->local).name));
		}
		const auto check = [&](const std::vector<ComponentId>& ids, const char* field, bool rootOnly) {
			for (const ComponentId id : ids)
			{
				const std::string_view name = components.Get(id).name;
				if (rootOnly ? !Has(world, root, id) : !InSubtree(world, subtree, id))
					problems.push_back(std::format("{}: this object has no {}{}", field, name, rootOnly ? " on its root" : ""));
				else if (!components.Get(id).toggleable)
					problems.push_back(std::format("{}: {} cannot be switched off", field, name));
			}
		};
		// The whole record must fit one (the same check a session start makes).
		if (const std::size_t worst = WorstCaseRecordBytes(serializers, desc); worst > kMaxRecordBytes)
		{
			std::string parts;
			for (const NetComponentId id : desc.replicated)
				if (const ReplicatedComponent* s = serializers.Find(id)) parts += std::format("{}{} {}", parts.empty() ? "" : " + ", s->name, WorstCaseBytes(*s, desc));
			problems.push_back(std::format("replicated: can need {} bytes ({}), but one record holds at most {}", worst, parts, kMaxRecordBytes));
		}

		// On/off on the synced children: on at least one of them, and switchable.
		for (const ComponentId id : desc.childEnabledReplicated)
		{
			const std::string_view name = components.Get(id).name;
			bool onAChild = false;
			for (const std::string& path : desc.syncedChildren)
			{
				Entity child;
				if (ResolveChildPath(world, root, path, child) == ChildPathResult::Found && Has(world, child, id)) onAChild = true;
			}
			if (!onAChild) problems.push_back(std::format("sync on/off (children): none of the synced children has a {}", name));
			else if (!components.Get(id).toggleable) problems.push_back(std::format("sync on/off (children): {} cannot be switched off", name));
		}

		// Synced children: each must name exactly one entity of this object.
		for (const std::string& path : desc.syncedChildren)
		{
			Entity child;
			const ChildPathResult found = ResolveChildPath(world, root, path, child);
			if (found == ChildPathResult::Missing) problems.push_back(std::format("synced child '{}' is not below this object", path));
			else if (found == ChildPathResult::Ambiguous) problems.push_back(std::format("synced child '{}' names more than one entity (give them different names)", path));
		}
		if (std::ranges::find(desc.replicated, NetComponentId::Renderers) != desc.replicated.end())
		{
			for (std::size_t i = NetRenderers::kSlots - 1; i < desc.syncedChildren.size(); ++i)
			{
				Entity child;
				if (ResolveChildPath(world, root, desc.syncedChildren[i], child) == ChildPathResult::Found && Has(world, child, ComponentTypeId<MeshRenderer>()))
					problems.push_back(std::format("Renderers: synced child '{}' has a MeshRenderer, but Renderers covers only the first {} synced children (move it up the list)",
						desc.syncedChildren[i], NetRenderers::kSlots - 1));
			}
		}
		check(desc.serverOnly, "serverOnly", false);
		check(desc.ownerLocalOnly, "ownerLocalOnly", false);
		check(desc.enabledReplicated, "enabledReplicated", true);

		// One identity per network object: none inside another.
		const ComponentId authoringId = ComponentTypeId<NetworkObjectAuthoring>();
		for (std::size_t i = 1; i < subtree.size(); ++i)
			if (Has(world, subtree[i], authoringId))
				problems.push_back("an entity below this one has NetworkObjectAuthoring too: a network object cannot contain another");
		for (Entity up = world.GetParent(root); world.IsAlive(up); up = world.GetParent(up))
			if (Has(world, up, authoringId))
				problems.push_back("an entity above this one has NetworkObjectAuthoring: a network object cannot be inside another");
		return problems;
	}

	bool DiscoverNetworkPrefabs(std::span<const NetPrefabSource> sources, const ComponentRegistry& components,
		const ReplicationRegistry& serializers, NetPrefabTable& table, Net::NetPrefabId firstId,
		NetPrefabDiscovery& out, std::string& error)
	{
		out = NetPrefabDiscovery{};
		out.offered = sources.size();

		// Asset-GUID order: the same set of prefabs always gets the same ids, whatever the scan order.
		std::vector<const NetPrefabSource*> order;
		order.reserve(sources.size());
		for (const NetPrefabSource& s : sources) order.push_back(&s);
		std::ranges::sort(order, {}, &NetPrefabSource::guid);
		for (std::size_t i = 1; i < order.size(); ++i)
		{
			if (order[i]->guid == order[i - 1]->guid)
			{
				error = std::format("prefab GUID {:016X} offered twice ('{}' and '{}')", order[i]->guid, order[i - 1]->path, order[i]->path);
				return false;
			}
		}

		std::uint32_t next = firstId;
		std::string playerPath;
		for (const NetPrefabSource* source : order)
		{
			if (source->prefab == nullptr) continue;
			NetworkObjectAuthoring authoring;
			std::string why;
			const AuthoringRead read = ReadPrefabAuthoring(*source->prefab, authoring, why);
			if (read == AuthoringRead::None) continue;
			const std::string where = std::format("network prefab '{}'", source->path);
			if (read == AuthoringRead::Invalid) { error = std::format("{}: {}", where, why); return false; }
			if (source->guid == 0) { error = where + ": it has no asset GUID"; return false; }
			if (next >= kMaxNetPrefabs) { error = std::format("{}: more than {} network prefabs", where, kMaxNetPrefabs - firstId); return false; }

			NetPrefabDesc desc;
			desc.name = source->path;
			desc.prefabPath = source->path;
			desc.prefabGuid = source->guid;
			desc.contentHash = SchemaHash(*source->prefab);
			if (!BuildProfile(authoring, components, serializers, desc, why)) { error = std::format("{}: {}", where, why); return false; }

			const auto id = static_cast<Net::NetPrefabId>(next++);
			if (!table.Register(id, std::move(desc), error)) return false;
			out.ids.push_back(id);   // registered: the caller removes it again if anything fails
			if (!table.Attach(id, source->prefab, error)) return false;

			if (authoring.spawnPerPlayer)
			{
				// Several are allowed: a scene's player is the prefab its placed players are instances
				// of (NetSceneObjects); the first found is the player of a scene that places none.
				out.playerPrefabs.push_back(id);
				if (out.playerPrefab == Net::kInvalidNetPrefab)
				{
					out.playerPrefab = id;
					playerPath = source->path;
				}
			}
		}
		return true;
	}
}
