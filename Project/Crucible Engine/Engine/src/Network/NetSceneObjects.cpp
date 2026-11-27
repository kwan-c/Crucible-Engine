/*************************************************************************
* \file:         NetSceneObjects.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          7 November 2026
* \version       1.1
* \brief
*                ScanSceneObjects (see the header).
*
* \history
*    -v1.0       Networking Phase 14.5
*                placed players listed apart (Networking Phase 14.6)
*                scene-only objects, per-instance profiles (Phase 14.7)
*    -v1.1       placed players of any spawnPerPlayer prefab; the scene's player prefab
*
**************************************************************************/
#include "Network/NetSceneObjects.h"

#include <algorithm>
#include <format>

#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Json/JsonWrapper.h"
#include "Network/CanonicalHash.h"
#include "Network/NetComponents.h"
#include "Network/NetContent.h"
#include "Network/NetObjectProfiles.h"
#include "Scene/EntityIO.h"
#include "Scene/Prefab.h"
#include "Scene/Scene.h"

namespace Crucible
{
	namespace
	{
		bool SameProfile(const NetPrefabDesc& a, const NetPrefabDesc& b)
		{
			return a.replicated == b.replicated && a.serverOnly == b.serverOnly && a.ownerLocalOnly == b.ownerLocalOnly
				&& a.enabledReplicated == b.enabledReplicated && a.sendPriority == b.sendPriority && a.critical == b.critical;
		}
	}

	SceneObjectScan ScanSceneObjects(World& world, const Scene& scene, const NetContent& content)
	{
		SceneObjectScan out;

		std::vector<Entity> authored;
		world.CreateQuery<const NetworkObjectAuthoring>().ForEach([&](Entity e, const NetworkObjectAuthoring&) { authored.push_back(e); });

		struct Own { SceneObject object; NetPrefabDesc desc; std::string label; };
		std::vector<Own> own;   // objects with their own settings: profiles assigned below
		for (const Entity e : authored)
		{
			const NetworkObjectAuthoring& authoring = *world.Read<NetworkObjectAuthoring>(e);
			const EntityName* name = world.Read<EntityName>(e);
			const std::string label = name != nullptr ? std::string(name->name) : std::format("entity {}:{}", e.index, e.generation);
			if (world.HasComponent<NetworkIdentity>(e)) continue;   // already networked (a proxy, or adopted)
			const PrefabInstance* instance = world.Read<PrefabInstance>(e);
			const Net::NetPrefabId prefabProfile = instance != nullptr && instance->isRoot ? content.Prefabs().FindByGuid(instance->prefabAsset) : Net::kInvalidNetPrefab;
			if (content.IsPlayerPrefab(prefabProfile))
			{
				out.players.push_back(SceneObject{ .entity = e, .sceneId = scene.IdOf(e).m_Value, .profile = prefabProfile });   // a spawn point
				continue;
			}
			if (!authoring.allowScenePlacement) { out.skipped.push_back(label + ": scene placement is off"); continue; }
			const std::uint64_t sceneId = scene.IdOf(e).m_Value;
			if (sceneId == 0) { out.skipped.push_back(label + ": the scene has no id for it (save the scene)"); continue; }

			// An instance with its prefab's settings uses the prefab's profile.
			if (prefabProfile != Net::kInvalidNetPrefab)
			{
				NetworkObjectAuthoring prefabAuthoring;
				std::string ignored;
				const Prefab* prefab = content.Prefabs().PrefabOf(prefabProfile);
				if (prefab != nullptr && ReadPrefabAuthoring(*prefab, prefabAuthoring, ignored) == AuthoringRead::Found && prefabAuthoring == authoring)
				{
					out.objects.push_back(SceneObject{ .entity = e, .sceneId = sceneId, .profile = prefabProfile });
					continue;
				}
			}

			// Its own settings (Phase 14.7): a scene-only object, or an instance that overrides them.
			if (authoring.spawnPerPlayer) { out.skipped.push_back(label + ": spawnPerPlayer belongs on a prefab, not on a scene entity"); continue; }
			const std::vector<std::string> problems = CheckAuthoring(world, e, authoring, world.Components(), content.Serializers());
			if (!problems.empty())
			{
				out.skipped.push_back(std::format("{}: {}{}", label, problems.front(), problems.size() > 1 ? std::format(" (and {} more)", problems.size() - 1) : ""));
				continue;
			}
			NetPrefabDesc desc;
			std::string error;
			if (!BuildProfile(authoring, world.Components(), content.Serializers(), desc, error)) { out.skipped.push_back(label + ": " + error); continue; }
			own.push_back(Own{ .object = SceneObject{ .entity = e, .sceneId = sceneId }, .desc = std::move(desc), .label = label });
		}

		// Scene profiles: identical settings share one id; new ids above the content's, in scene-id order.
		std::ranges::sort(own, {}, [](const Own& o) { return o.object.sceneId; });
		std::size_t next = static_cast<std::size_t>(content.Prefabs().HighestId()) + 1;
		for (Own& o : own)
		{
			const auto same = std::ranges::find_if(out.profiles, [&](const SceneProfile& p) { return SameProfile(p.desc, o.desc); });
			if (same != out.profiles.end()) o.object.profile = same->id;
			else if (next >= kMaxNetPrefabs) { out.skipped.push_back(o.label + ": no network prefab id is left for its own settings"); continue; }
			else
			{
				o.object.profile = static_cast<Net::NetPrefabId>(next++);
				o.desc.name = std::format("scene profile {}", o.object.profile);
				o.desc.prefabPath = "(scene)";
				out.profiles.push_back(SceneProfile{ .id = o.object.profile, .desc = o.desc });
			}
			out.objects.push_back(o.object);
		}
		std::ranges::sort(out.objects, {}, &SceneObject::sceneId);
		std::ranges::sort(out.players, {}, &SceneObject::sceneId);
		// The scene's player: the prefab of its first placed player. Placed players of another player
		// prefab still mark spawn points, but are reported (one scene, one kind of player).
		if (!out.players.empty())
		{
			out.playerPrefab = out.players.front().profile;
			for (const SceneObject& p : out.players)
				if (p.profile != out.playerPrefab)
					out.skipped.push_back(std::format("a placed player of another player prefab ({}): the scene's player is {}", p.profile, out.playerPrefab));
		}

		// The authored data each machine keeps locally: every component of every entity of each
		// object's subtree, as EntityIO writes it (runtime components never appear).
		CanonicalHash hash;
		hash.Str("Crucible.SceneObjects");
		hash.U32(static_cast<std::uint32_t>(out.objects.size()));
		std::vector<Entity> subtree;
		for (const SceneObject& object : out.objects)
		{
			hash.U64(object.sceneId);
			hash.U32(object.profile);
			subtree.clear();
			world.CollectSubtree(object.entity, subtree);   // parents before children, in sibling order
			hash.U32(static_cast<std::uint32_t>(subtree.size()));
			for (const Entity e : subtree)
			{
				JsonWrapper doc;
				EntityIO::WriteComponents(world, e, doc, "c");
				hash.Str(doc.ToString(false));
			}
		}
		out.hash = hash.Value() != 0 ? hash.Value() : 1;
		return out;
	}
}
