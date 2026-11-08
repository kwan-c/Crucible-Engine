/*************************************************************************
* \file:         NetContent.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.2
* \brief
*                NetContent: everything two builds must agree on before
*                they may play together - the serializers, the network
*                prefabs - and the content hash that proves they do.
*
* \details
*                Core owns one. Prefabs come from two places:
*                  - authored: every .cprefab whose root carries
*                    NetworkObjectAuthoring, found afresh before every
*                    session (NetObjectProfiles.h), ids derived after the
*                    code-registered ones;
*                  - code (optional): GamePrefabs.h, registered once at
*                    startup with ids written in the source.
*                Each time a Host or Client session is about to start,
*                Prepare drops the previously discovered prefabs, re-reads
*                the code ones, discovers the authored ones, validates the
*                table and recomputes the hash; Core hands the hash (or the
*                reason Prepare failed) to the NetworkSession, which sends
*                the hash in JoinRequest and refuses to start with invalid
*                content.
*
*                The hash (CanonicalHash, FNV-1a 64 over a fixed-order byte
*                string) covers, in this order:
*                  1. NetworkSession::DefaultContentHash(): protocol version,
*                     tick rate, snapshot interval;
*                  2. every serializer in NetComponentId order;
*                  3. every prefab in NetPrefabId order (NetPrefabTable).
*                Registration order never matters; content always does.
*
* \history
*    -v1.0       network content: registry, prefab table, content hash
*    -v1.1       authored network prefabs discovered before every session;
*                PrepareWith, PlayerPrefab, Discovered (Networking Phase 14.2)
*    -v1.2       scene profiles (Networking Phase 14.7)
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "EngineExport.h"
#include "Network/NetObjectProfiles.h"
#include "Network/NetPrefabTable.h"
#include "Network/ReplicationRegistry.h"

namespace Crucible
{
	class World;
	class AssetManager;
	class ResourceManager;
	class ComponentRegistry;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // members of an exported class: used only inside Engine.dll
#endif

	class ENGINE_API NetContent
	{
	public:
		/// Registers the built-in serializers.
		NetContent();

		ReplicationRegistry&       Serializers() noexcept       { return m_serializers; }
		const ReplicationRegistry& Serializers() const noexcept { return m_serializers; }
		NetPrefabTable&            Prefabs() noexcept           { return m_prefabs; }
		const NetPrefabTable&      Prefabs() const noexcept     { return m_prefabs; }

		/// Before a Host or Client session starts: re-reads the code-registered prefabs, discovers
		/// every authored network prefab among the project's .cprefab files (read fresh), validates
		/// the table, and recomputes Hash(). False with the reason if anything is wrong; Hash() is
		/// then 0.
		bool Prepare(World& world, const AssetManager& assets, ResourceManager& resources, std::string& error);

		/// Prepare without the asset system: discovers among `sources` (tests; Prepare builds them
		/// from the project), validates and hashes.
		bool PrepareWith(std::span<const NetPrefabSource> sources, const ComponentRegistry& components, std::string& error);

		/// The discovered prefab with spawnPerPlayer (NetInputSystem spawns one per peer), or
		/// kInvalidNetPrefab.
		Net::NetPrefabId PlayerPrefab() const noexcept { return m_playerPrefab; }

		/// The ids the last Prepare discovered.
		std::span<const Net::NetPrefabId> Discovered() const noexcept { return m_discovered; }

		/// Scene profiles (Networking Phase 14.7): the replication profiles of scene objects that
		/// use their own NetworkObjectAuthoring (scene-only objects, and prefab instances whose
		/// settings differ from their prefab's). NetReceiveSystem registers them when a session
		/// starts, above every prefab id; they never enter Hash() (the scene hash covers them) and
		/// are forgotten at the next session start or Prepare. They have no prefab: never spawned.
		bool AddSceneProfile(Net::NetPrefabId id, NetPrefabDesc desc, std::string& error);
		void ForgetSceneProfiles();
		std::span<const Net::NetPrefabId> SceneProfiles() const noexcept { return m_sceneProfiles; }

		/// The content hash from the last successful Prepare (or Recompute); 0 before.
		std::uint64_t Hash() const noexcept { return m_hash; }

		/// Validates and hashes the prefabs as they are now attached, without reading files
		/// (Prepare's last two steps; tests attach their own prefabs).
		bool Recompute(const ComponentRegistry& components, std::string& error);

		/// The hash for this content (no validation).
		std::uint64_t ComputeHash(const ComponentRegistry& components) const;

	private:
		ReplicationRegistry m_serializers;
		NetPrefabTable      m_prefabs;
		std::uint64_t       m_hash = 0;
		std::vector<Net::NetPrefabId> m_discovered;   // removed again before the next discovery
		std::vector<Net::NetPrefabId> m_sceneProfiles;   // this session's (Phase 14.7)
		Net::NetPrefabId    m_playerPrefab = Net::kInvalidNetPrefab;

		void ForgetDiscovered();
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
