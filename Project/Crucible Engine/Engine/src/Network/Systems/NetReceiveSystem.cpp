/*************************************************************************
* \file:         NetReceiveSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                NetReceiveSystem: drives NetworkSession::Update each frame.
*
* \history
*    -v1.0       connection handling (Networking Guide PR 4)
*
**************************************************************************/
#include "Network/Systems/NetReceiveSystem.h"

#include "Network/NetworkSession.h"

namespace Crucible
{
	void NetReceiveSystem::Describe(SystemDesc& desc) const
	{
		desc.NoComponentAccess();   // Phase 4: connections only; Phase 8 needs Exclusive()
	}

	void NetReceiveSystem::Update([[maybe_unused]] World& world, [[maybe_unused]] SystemContext& ctx)
	{
		m_session.Update();   // Offline: returns at once
	}
}
