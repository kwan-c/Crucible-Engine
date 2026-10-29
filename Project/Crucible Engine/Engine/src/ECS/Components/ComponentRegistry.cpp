/*************************************************************************
* \file:         ComponentRegistry.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.2
* \brief
*                Implementation of ComponentRegistry: bulk registration
*                of every known component type and lookup/validation of
*                individual ComponentInfo entries. See
*                ComponentRegistry.h for the public interface.
*
* \history
*    -v1.0       initial RegisterAll/Get/IsRegistered/RegisterInfo
*    -v1.1       validation throws in every build configuration;
*                RegisteredSignature/ValidateSignature
*    -v1.2       RegisterAll finishes with FinalizeRequirements
*
**************************************************************************/
#include <stdexcept>
#include <string>
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

		// A ComponentKind without a bound type would make every signature containing it unusable.
		for (ComponentId id = 0; id < kMaxComponents; ++id)
		{
			if (!m_components[id].IsValid())
			{
				throw std::logic_error("ComponentRegistry::RegisterAll: ComponentKind " + std::to_string(id) +
					" has no registered type - check CRUCIBLE_COMPONENT_LIST and the CRUCIBLE_COMPONENT bindings");
			}
		}

		FinalizeRequirements();
	}

	void ComponentRegistry::FinalizeRequirements()
	{
		std::vector<ComponentSignature> direct(kMaxComponents);
		for (ComponentId id = 0; id < kMaxComponents; ++id) direct[id] = m_components[id].requirements;

		std::vector<ComponentSignature> closure;
		const ComponentId cyclic = Detail::CloseRequirements(direct, closure);
		if (cyclic != kInvalidComponentId)
		{
			throw std::logic_error("ComponentRegistry: component '" + std::string(m_components[cyclic].name) +
				"' requires itself through RequiresComponents (a requirement cycle) - fix the RequiresComponents specialisations in Components.h");
		}

		// Reverse edges: who is switched off with this one.
		for (ComponentId id = 0; id < kMaxComponents; ++id)
		{
			m_components[id].requiredClosure = closure[id];
			m_components[id].dependents.reset();
		}
		for (ComponentId dependent = 0; dependent < kMaxComponents; ++dependent)
		{
			for (ComponentId required = 0; required < kMaxComponents; ++required)
			{
				if (closure[dependent].test(required)) m_components[required].dependents.set(dependent);
			}
		}
	}

	const ComponentInfo& ComponentRegistry::Get(ComponentId id) const
	{
		if (!IsRegistered(id))
		{
			throw std::out_of_range("ComponentRegistry::Get: component id " + std::to_string(id) + " is not registered");
		}
		return m_components[id];
	}

	bool ComponentRegistry::IsRegistered(ComponentId id) const
	{
		return IsValidComponentId(id) && m_components[id].IsValid();
	}

	ComponentSignature ComponentRegistry::RegisteredSignature() const noexcept
	{
		ComponentSignature registered;
		for (ComponentId id = 0; id < kMaxComponents; ++id)
		{
			if (m_components[id].IsValid()) registered.set(id);
		}
		return registered;
	}

	void ComponentRegistry::ValidateSignature(const ComponentSignature& signature, const char* operation) const
	{
		const ComponentSignature unknown = signature & ~RegisteredSignature();
		if (unknown.none()) return;

		// Name every offending id so the caller can find the bad bit quickly.
		std::string ids;
		for (ComponentId id = 0; id < kMaxComponents; ++id)
		{
			if (unknown.test(id)) ids += (ids.empty() ? "" : ", ") + std::to_string(id);
		}
		throw std::invalid_argument(std::string(operation) + ": signature contains unregistered component id(s) " + ids);
	}

	ComponentId ComponentRegistry::RegisterInfo(ComponentInfo info)
	{
		const ComponentId id = info.id;
		if (!IsValidComponentId(id))
		{
			throw std::out_of_range("ComponentRegistry::Register: invalid component id " + std::to_string(id));
		}
		if (!m_components[id].IsValid())
			m_components[id] = std::move(info);
		return id;
	}

}
