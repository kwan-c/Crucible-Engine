/*************************************************************************
* \file:         NetContent.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                NetContent (see the header).
*
* \history
*    -v1.0       Networking Guide 18.3 issue 7 (PR 7)
*
**************************************************************************/
#include "Network/NetContent.h"

#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Network/CanonicalHash.h"
#include "Network/NetworkSession.h"

namespace Crucible
{
	NetContent::NetContent()
	{
		std::string error;
		if (!m_serializers.RegisterBuiltIns(error)) CRU_ERROR("[Net] built-in serializers: {}", error);   // a programming error
	}

	bool NetContent::Prepare(World& world, const AssetManager& assets, ResourceManager& resources, std::string& error)
	{
		m_hash = 0;
		if (!m_prefabs.LoadPrefabs(assets, resources, error)) return false;
		return Recompute(world.Components(), error);
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
