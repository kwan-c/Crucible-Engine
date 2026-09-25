/*************************************************************************
* \file:         ECSPhysicsSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.1
* \brief
*                Declares the ECSPhysicsSystem, which applies
*                RigidBody-driven motion to Transform components each
*                frame.
*
* \details
*                ECSPhysicsSystem implements the IECSSystem interface
*                (see Systems/IECSSystems.h) so it can be registered
*                with and driven by ECSSystemManager. It runs in the
*                PostUpdate stage and keeps one persistent query, built
*                lazily on its first Update(). See ECSPhysicsSystem.cpp.
*
* \history
*    -v1.0       skeleton / placeholder system, no physics logic yet
*    -v1.1       integrates velocity into position using
*                World::DeltaTime(); PostUpdate stage; persistent query
*
**************************************************************************/
#pragma once

#include <optional>

#include "Systems/IECSSystems.h"
#include "Systems/Query.h"
#include "Components/Components.h"

namespace Crucible
{

	class ECSPhysicsSystem final :public IECSSystem
	{
	public:
		void Init() override;
		void Update(World& world, CommandBuffer& commands)override;
		void Shutdown() noexcept override;

		int Priority()const override { return 1; }

		/*********************************************************************
		 \brief  Physics runs after the main per-frame logic.
		 *********************************************************************/
		SystemStage Stage() const override { return SystemStage::PostUpdate; }

	private:
		// Built on the first Update(), then reused every frame.
		std::optional<Query<Transform, const RigidBody>> m_bodies;
	};
}
