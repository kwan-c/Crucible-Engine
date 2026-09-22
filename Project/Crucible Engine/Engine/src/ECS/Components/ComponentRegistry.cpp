/*************************************************************************
* \file:         ComponentRegistry.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Implementation of ComponentRegistry: bulk registration
*                of every known component type and lookup/validation of
*                individual ComponentInfo entries. See
*                ComponentRegistry.h for the public interface.
*
* \history
*    -v1.0       initial RegisterAll/Get/IsRegistered/RegisterInfo
*
*
**************************************************************************/
#include <cassert>
#include <utility>

#include "ComponentRegistry.h"
#include "Components.h"

namespace Crucible
{
	ComponentRegistry::ComponentRegistry()
	{
		m_components.resize(kMaxComponents);
	}

	void ComponentRegistry::RegisterAll() 
	{
#define CRUCIBLE_REGISTER(Type) Register<Type>();
		CRUCIBLE_COMPONENT_LIST(CRUCIBLE_REGISTER)
#undef CRUCIBLE_REGISTER
			for (ComponentId id = 0; id < kMaxComponents; ++id)
			{
				assert(m_components[id].IsValid() &&
					"ComponentKind entry has no registered type - check "
					"CRUCIBLE_COMPONENT_LIST and the CRUCIBLE_COMPONENT bindings");
			}

	}

	const ComponentInfo& ComponentRegistry::Get(ComponentId id) const
	{
		assert(IsValidComponentId(id));
		return m_components[id];
	}
	bool ComponentRegistry::IsRegistered(ComponentId id) const
	{
		return IsValidComponentId(id) && m_components[id].IsValid();
	}

	ComponentId ComponentRegistry::RegisterInfo(ComponentInfo info)
	{
		const ComponentId id = info.id;
		assert(IsValidComponentId(id));
		if (!m_components[id].IsValid())
			m_components[id] = std::move(info);
		return id;
	}

}