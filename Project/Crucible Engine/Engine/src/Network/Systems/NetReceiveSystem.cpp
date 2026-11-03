/*************************************************************************
* \file:         NetReceiveSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.1
* \brief
*                NetReceiveSystem: drives NetworkSession::Update each frame.
*
* \history
*    -v1.0       connection handling (Networking Guide PR 4)
*    -v1.1       destroys rejected identities; Debug id-map check (PR 6)
*
**************************************************************************/
#include "Network/Systems/NetReceiveSystem.h"

#include "ECS/World.h"
#include "Network/NetComponents.h"
#include "Network/NetIdentityHooks.h"
#include "Network/NetworkSession.h"

namespace Crucible
{
	void NetReceiveSystem::Describe(SystemDesc& desc) const
	{
		desc.Reads<NetworkIdentity>();   // rejected entities are destroyed through commands; Phase 8 needs Exclusive()
	}

	void NetReceiveSystem::Update(World& world, SystemContext& ctx)
	{
		m_session.Update();   // Offline: returns at once

		// Entities whose NetworkIdentity the session rejected (no id, or an id already in use).
		// Skipped if the entity is gone, or is now mapped after all (its identity was replaced).
		m_session.TakeRejected(m_rejected);
		for (const Entity e : m_rejected)
		{
			if (!world.IsAlive(e)) continue;
			const NetworkIdentity* identity = world.Read<NetworkIdentity>(e);
			if (identity != nullptr && m_session.Entities().Find(identity->id) == e) continue;
			ctx.commands.DestroyEntity(e);
		}

#ifndef NDEBUG
		if (m_session.IsActive()) CheckIdentityMap(world, m_session);   // reports identities overwritten after spawn
#endif
	}
}
