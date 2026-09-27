/*************************************************************************
* \file:         SystemAccess.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.0
* \brief
*                Storage for the thread-local "system currently running"
*                context. See SystemAccess.h.
*
* \history
*    -v1.0       initial thread-local context
*
**************************************************************************/
#include "SystemAccess.h"
#include "SystemAccessScope.h"

namespace Crucible
{
	namespace
	{
		thread_local const ActiveSystemAccess* t_currentSystem = nullptr;
	}

	const ActiveSystemAccess* CurrentSystemAccess() noexcept
	{
		return t_currentSystem;
	}

	SystemAccessScope::SystemAccessScope(const ActiveSystemAccess& active) noexcept
		: m_previous(t_currentSystem)
	{
		t_currentSystem = &active;
	}

	SystemAccessScope::~SystemAccessScope()
	{
		t_currentSystem = m_previous;
	}
}
