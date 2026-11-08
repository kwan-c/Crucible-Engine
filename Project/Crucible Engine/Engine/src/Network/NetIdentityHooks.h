/*************************************************************************
* \file:         NetIdentityHooks.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Connects a World's NetworkIdentity components to a
*                NetworkSession's id map, and checks that they still agree.
*
* \details
*                World fires OnComponentAdded / OnComponentRemoved for
*                NetworkIdentity on every path - AddComponent, spawns,
*                RemoveComponent, DestroyEntity, a destroyed parent's
*                children, CommandBuffer, DestroyAllEntities - so a map
*                written only from these hooks cannot outlive or miss an
*                entity, whatever code changes the World.
*
*                The one thing hooks cannot see is an identity OVERWRITTEN
*                in place (AddComponent on an entity that already has one,
*                or Write<NetworkIdentity>). The rule is "written once, at
*                spawn"; CheckIdentityMap reports any break of it.
*
* \history
*    -v1.0       NetworkIdentity hooks and the identity map check
*
**************************************************************************/
#pragma once

#include <cstddef>

#include "EngineExport.h"

namespace Crucible
{
	class World;
	class NetworkSession;

	/// Registers the NetworkIdentity add/remove hooks that keep session.Entities() in step with
	/// `world`. Call once per World (World hooks cannot be unregistered): Core::Init does. The
	/// session must outlive every entity of the World (Core declares it first).
	ENGINE_API void RegisterIdentityHooks(World& world, NetworkSession& session);

	/// Every mapping (id -> entity) must name a live entity whose NetworkIdentity still has that
	/// id. Logs each one that does not (an identity overwritten after spawn) and returns how many.
	/// NetReceiveSystem runs it every frame in Debug builds while a session is active.
	ENGINE_API std::size_t CheckIdentityMap(const World& world, const NetworkSession& session);
}
