#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "Archetype.h"

namespace Crucible
{
	class ArchetypeRegistry
	{
	public:
		explicit ArchetypeRegistry(const ComponentRegistry& components);

		Archetype* Find(const ComponentSignature& signature);
		Archetype* FindOrCreate(const ComponentSignature& signature);
		Archetype* EmptyArchetype()const { return m_empty; }

		Archetype* GetAddDestination(Archetype& source, ComponentId id);
		Archetype* GetRemoveDestination(Archetype& source, ComponentId id);

		const std::vector<Archetype*>& All()const { return m_all; }
		//const std::vector<Archetype*>& ArchetypesWith(ComponentId id)const { return m_componentToArchetype[id]; }

	private:
		void OnArchetypeCreated(Archetype& archetype);

		const ComponentRegistry& m_components;

		std::unordered_map < ComponentSignature, std::unique_ptr <Archetype>, SignatureHash> m_archetypes;

		std::vector<Archetype*> m_all;
		Archetype* m_empty = nullptr;

		std::vector<std::vector<Archetype*>> m_componentToArchetypes;
	};
}