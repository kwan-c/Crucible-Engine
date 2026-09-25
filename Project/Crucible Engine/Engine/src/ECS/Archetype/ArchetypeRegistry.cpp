/*************************************************************************
* \file:         ArchetypeRegistry.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Implementation of ArchetypeRegistry: signature->archetype
*                lookup/creation and the add/remove destination helpers
*                used when an entity's component set changes.
*                See ArchetypeRegistry.h for the public interface.
*
* \history
*    -v1.0       initial map-based lookup, no edge caching yet
*    -v1.1       GetAdd/RemoveDestination now read/write Archetype's edge
*                cache instead of unconditionally hashing+looking up;
*                FindOrCreate() bumps a Version() counter on genuine
*                creation so Query can skip redundant rebuilds
*
**************************************************************************/
#include "ArchetypeRegistry.h"

namespace Crucible
{
	ArchetypeRegistry::ArchetypeRegistry(const ComponentRegistry& components) :m_components(components)
	{
		m_componentToArchetypes.resize(kMaxComponents);
		m_empty = FindOrCreate(ComponentSignature());
	}

	Archetype* ArchetypeRegistry::Find(const ComponentSignature& signature)
	{
		const auto it = m_archetypes.find(signature);
		return it == m_archetypes.end()? nullptr : it->second.get();
	}

	Archetype* ArchetypeRegistry::FindOrCreate(const ComponentSignature& signature)
	{
		if (Archetype* existing = Find(signature)) return existing;

		auto owned = std::make_unique<Archetype>(signature, m_components);
		Archetype* archetype = owned.get();

		m_archetypes.emplace(signature, std::move(owned));
		m_all.push_back(archetype);
		
		OnArchetypeCreated(*archetype);
		++m_version;
		return archetype;
	}

	Archetype* ArchetypeRegistry::GetAddDestination(Archetype& source, ComponentId id)
	{
		ArchetypeEdge& edge = source.Edge(id);
		if (edge.add) return edge.add;

		ComponentSignature signature = source.Signature();
		signature.set(id);
		Archetype* dst = FindOrCreate(signature);

		edge.add = dst;
		dst->Edge(id).remove = &source;

		return dst;
	}

	Archetype* ArchetypeRegistry::GetRemoveDestination(Archetype& source, ComponentId id)
	{
		ArchetypeEdge& edge = source.Edge(id);
		if (edge.remove) return edge.remove;

		ComponentSignature signature = source.Signature();
		signature.reset(id);
		Archetype* dst = FindOrCreate(signature);

		edge.remove = dst;
		dst->Edge(id).add = &source;

		return dst;
	}

	void ArchetypeRegistry::OnArchetypeCreated(Archetype& archetype)
	{
		for (ComponentId id : archetype.ComponentIds())
		{
			m_componentToArchetypes[id].push_back(&archetype);
		}
	}

}