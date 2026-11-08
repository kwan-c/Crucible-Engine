/*************************************************************************
* \file:         NetContent.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.2
* \brief
*                NetContent (see the header).
*
* \history
*    -v1.0       network content: registry, prefab table, content hash
*    -v1.1       discovery of authored network prefabs (Networking Phase
*                14.2)
*    -v1.2       scene profiles (Networking Phase 14.7)
*
**************************************************************************/
#include "Network/NetContent.h"

#include <format>

#include "AssetPipe/Assetmanager.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Network/CanonicalHash.h"
#include "Network/NetworkSession.h"
#include "Resources/ResourceManager.h"
#include "Scene/Prefab.h"

namespace Crucible
{
	NetContent::NetContent()
	{
		std::string error;
		if (!m_serializers.RegisterBuiltIns(error)) CRU_ERROR("[Net] built-in serializers: {}", error);   // a programming error
	}

	bool NetContent::AddSceneProfile(Net::NetPrefabId id, NetPrefabDesc desc, std::string& error)
	{
		if (!m_prefabs.Register(id, std::move(desc), error)) return false;
		m_sceneProfiles.push_back(id);
		return true;
	}

	void NetContent::ForgetSceneProfiles()
	{
		for (const Net::NetPrefabId id : m_sceneProfiles) m_prefabs.Remove(id);
		m_sceneProfiles.clear();
	}

	void NetContent::ForgetDiscovered()
	{
		ForgetSceneProfiles();   // above every discovered id
		for (const Net::NetPrefabId id : m_discovered) m_prefabs.Remove(id);
		m_discovered.clear();
		m_playerPrefab = Net::kInvalidNetPrefab;
	}

	bool NetContent::Prepare(World& world, const AssetManager& assets, ResourceManager& resources, std::string& error)
	{
		m_hash = 0;
		ForgetDiscovered();
		if (!m_prefabs.LoadPrefabs(assets, resources, error)) return false;   // the code-registered ones

		// Every prefab of the project, read fresh (its current content hash). One that cannot be read
		// is skipped with a warning: if it was a network prefab, the content hash differs from a peer
		// that could read it, and the join is refused.
		std::vector<NetPrefabSource> sources;
		for (const AssetEntry* entry : assets.GetByType(AssetType::Prefab))
		{
			std::shared_ptr<Prefab> prefab = resources.ReadPrefab(entry->guid.m_Value);
			if (!prefab) { CRU_WARN("[Net] prefab '{}' could not be read; it is not offered as a network prefab", entry->virtualPath); continue; }
			sources.push_back(NetPrefabSource{ .guid = entry->guid.m_Value, .path = entry->virtualPath, .prefab = std::move(prefab) });
		}
		return PrepareWith(sources, world.Components(), error);
	}

	bool NetContent::PrepareWith(std::span<const NetPrefabSource> sources, const ComponentRegistry& components, std::string& error)
	{
		m_hash = 0;
		ForgetDiscovered();
		NetPrefabDiscovery found;
		const bool ok = DiscoverNetworkPrefabs(sources, components, m_serializers, m_prefabs,
			static_cast<Net::NetPrefabId>(m_prefabs.HighestId() + 1), found, error);
		m_discovered = found.ids;   // removed again next time, also after a failure
		if (!ok)
		{
			ForgetDiscovered();
			return false;
		}
		m_playerPrefab = found.playerPrefab;
		if (!Recompute(components, error))
		{
			ForgetDiscovered();   // refused: nothing of it stays registered
			return false;
		}
		CRU_INFO("[Net] {} network prefab(s) found among {} prefabs{}", m_discovered.size(), found.offered,
			m_playerPrefab != Net::kInvalidNetPrefab ? std::format(" (player: '{}')", m_prefabs.Desc(m_playerPrefab)->name) : std::string());
		return true;
	}

	bool NetContent::Recompute(const ComponentRegistry& components, std::string& error)
	{
		m_hash = 0;
		if (!m_prefabs.Validate(m_serializers, components, error)) return false;
		m_hash = ComputeHash(components);
		return true;
	}

	std::uint64_t NetContent::ComputeHash(const ComponentRegistry& components) const
	{
		CanonicalHash hash;
		hash.U64(NetworkSession::DefaultContentHash());   // protocol version, tick rate, snapshot interval
		m_serializers.HashInto(hash);
		m_prefabs.HashInto(hash, components);
		return hash.Value();
	}
}
