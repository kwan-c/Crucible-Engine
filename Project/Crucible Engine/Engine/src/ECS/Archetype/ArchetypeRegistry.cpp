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
		return archetype;
	}

	Archetype* ArchetypeRegistry::GetAddDestination(Archetype& source, ComponentId id)
	{
		ComponentSignature signature = source.Signature();
		signature.set(id);
		return FindOrCreate(signature);
	}

	Archetype* ArchetypeRegistry::GetRemoveDestination(Archetype& source, ComponentId id)
	{
		ComponentSignature signature = source.Signature();
		signature.reset(id);
		return FindOrCreate(signature);
	}

	void ArchetypeRegistry::OnArchetypeCreated(Archetype& archetype)
	{
		for (ComponentId id : archetype.ComponentIds())
		{
			m_componentToArchetypes[id].push_back(&archetype);
		}
	}

}