/*************************************************************************
* \file:         NetContent.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                NetContent: everything two builds must agree on before
*                they may play together - the serializers, the network
*                prefabs - and the content hash that proves they do.
*
* \details
*                Core owns one. The game registers its prefabs at startup
*                (Prefabs(), see GamePrefabs.h). Each time a Host or Client
*                session is about to start, Prepare re-reads the prefab
*                files, validates the table and recomputes the hash; Core
*                hands the hash (or the reason Prepare failed) to the
*                NetworkSession, which sends the hash in JoinRequest and
*                refuses to start with invalid content.
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
*    -v1.0       Networking Guide 18.3 issue 7 (PR 7)
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <string>

#include "EngineExport.h"
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

		/// Before a Host or Client session starts: reads every prefab file fresh, validates the
		/// table, and recomputes Hash(). False with the reason if anything is wrong; Hash() is
		/// then 0.
		bool Prepare(World& world, const AssetManager& assets, ResourceManager& resources, std::string& error);

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
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
