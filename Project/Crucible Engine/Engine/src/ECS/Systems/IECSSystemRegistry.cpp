#include "IECSSystemRegistry.h"
#include <ECS/ECSPhysicsSystem.h>
#include <stdexcept>

namespace Crucible
{
	ECSSystemRegistry::ECSSystemRegistry()
	{
		m_systems[static_cast<std::size_t>(ECSSystemId::Physics)] =
			std::make_unique<ECSPhysicsSystem>();

		// Detect missing construction when a new enum entry is added.
		for (const auto& system : m_systems)
		{
			if (!system)
				throw std::logic_error("Declared ECS system was not constructed");
		}
	}
}