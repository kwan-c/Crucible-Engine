/*************************************************************************
* \file:         SystemAccessScope.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.0
* \brief
*                Engine-internal RAII scope that marks a system as running
*                on the current thread (see CurrentSystemAccess()).
*
* \details
*                Used by ECSSystemManager around each system's Update().
*                Not exported: game code never sets the context itself.
*
* \history
*    -v1.0       initial scope
*
**************************************************************************/
#pragma once

#include "SystemAccess.h"

namespace Crucible
{
	/*************************************************************************
	 \brief  Makes `active` the current system on this thread for the
			 scope's lifetime, restoring the previous one afterwards.
	 \note   `active` must outlive the scope.
	 *************************************************************************/
	class SystemAccessScope
	{
	public:
		explicit SystemAccessScope(const ActiveSystemAccess& active) noexcept;
		~SystemAccessScope();

		SystemAccessScope(const SystemAccessScope&) = delete;
		SystemAccessScope& operator=(const SystemAccessScope&) = delete;

	private:
		const ActiveSystemAccess* m_previous;
	};
}
