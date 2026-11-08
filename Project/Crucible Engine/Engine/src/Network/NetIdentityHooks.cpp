/*************************************************************************
* \file:         NetIdentityHooks.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                RegisterIdentityHooks and CheckIdentityMap (see the header).
*
* \history
*    -v1.0       NetworkIdentity hooks and the identity map check
*
**************************************************************************/
#include "Network/NetIdentityHooks.h"

#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Network/NetComponents.h"
#include "Network/NetworkSession.h"

namespace Crucible
{
	void RegisterIdentityHooks(World& world, NetworkSession& session)
	{
		world.OnComponentAdded<NetworkIdentity>([&session](World& w, Entity e, ComponentRef<NetworkIdentity> identity) {
			session.OnIdentityAdded(e, identity, [&w](Entity other) { return w.IsAlive(other); });
		});
		world.OnComponentRemoved<NetworkIdentity>([&session](World&, Entity e, ComponentRef<NetworkIdentity> identity) {
			session.OnIdentityRemoved(e, identity);
		});
	}

	std::size_t CheckIdentityMap(const World& world, const NetworkSession& session)
	{
		std::size_t broken = 0;
		for (const auto& [id, entity] : session.Entities())
		{
			const NetworkIdentity* identity = world.IsAlive(entity) ? world.Read<NetworkIdentity>(entity) : nullptr;
			if (identity != nullptr && identity->id == id) continue;
			++broken;
			if (identity == nullptr)
				CRU_ERROR("[Net] id map: NetworkId {} names entity {}:{}, which no longer has a NetworkIdentity",
					id, entity.index, entity.generation);
			else
				CRU_ERROR("[Net] id map: NetworkId {} names entity {}:{}, whose NetworkIdentity now says {} - overwritten after spawn",
					id, entity.index, entity.generation, identity->id);
		}
		return broken;
	}
}
