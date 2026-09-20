/*************************************************************************
* \file:         ECSPhysicsSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Declares the ECSPhysicsSystem, a placeholder physics
*                system that will apply RigidBody-driven motion to
*                Transform components each frame.
*
* \details
*                ECSPhysicsSystem implements the IECSSystem interface
*                (see Systems/IECSSystems.h) so it can be registered
*                with and driven by the engine's SystemRegistry/manager.
*                The actual physics update logic is not yet implemented -
*                see ECSPhysicsSystem.cpp for the (currently empty)
*                Init/Update/Shutdown bodies.
*
* \history
*    -v1.0       skeleton / placeholder system, no physics logic yet
*
*
**************************************************************************/
#pragma once

#include "Systems/IECSSystems.h"
//this is a sample place holder file 

namespace Crucible
{

	class ECSPhysicsSystem final :public IECSSystem
	{
	public:
		void Init() override;
		void Update(World& world, CommandBuffer& commands)override;
		void Shutdown() noexcept override;

		int Priority()const override { return 1; }
	};
}