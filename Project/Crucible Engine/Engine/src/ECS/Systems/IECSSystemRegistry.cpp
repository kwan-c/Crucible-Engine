/*************************************************************************
* \file:         IECSSystemRegistry.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Implementation of ECSSystemRegistry's constructor:
*                constructs every system named in ECSSystemId and
*                sanity-checks that none were missed. See
*                IECSSystemRegistry.h for the public interface.
*
* \details
*                To register a new system: add its enum value in
*                IECSSystemRegistry.h's ECSSystemId (before MAX), then
*                construct it here at the matching index. The loop at
*                the end of the constructor throws if any slot in
*                m_systems was left null, so a forgotten construction
*                is caught immediately at startup instead of causing a
*                null-dereference later in ECSSystemManager (see
*                ECSSystemManager.cpp).
*
* \history
*    -v1.0       initial single-system (Physics) construction + validation
*
*
**************************************************************************/
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