/*************************************************************************
* \file:         ECSPhysicsSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Implementation of ECSPhysicsSystem. Currently empty -
*                see ECSPhysicsSystem.h for the interface this
*                satisfies.
*
* \details
*                All three overrides (Init/Update/Shutdown) are stubbed
*                out. Update() will eventually query the World (see
*                World.h / World.cpp) for entities that have both a
*                RigidBody and a Transform component (see Components.h)
*                and integrate velocity into position.
*
* \history
*    -v1.0       stub implementation, no physics logic yet
*
*
**************************************************************************/
#include "ECSPhysicsSystem.h"
#include <iostream>
#include <ECS/World.h>
#include <ECS/Components/Components.h>

namespace Crucible
{
	void ECSPhysicsSystem::Init()
	{
	}
	void ECSPhysicsSystem::Update([[maybe_unused]] World& world, [[maybe_unused]] CommandBuffer& commands)
	{

	}
	void ECSPhysicsSystem::Shutdown() noexcept
	{
	}
}
