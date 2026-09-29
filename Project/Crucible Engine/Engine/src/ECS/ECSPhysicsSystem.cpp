/*************************************************************************
* \file:         ECSPhysicsSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.2
* \brief
*                Implementation of ECSPhysicsSystem. See
*                ECSPhysicsSystem.h for the interface this satisfies.
*
* \details
*                Update() visits every entity with both a Transform and a
*                RigidBody (see Components.h) and integrates velocity into
*                position using the frame delta time from
*                World::DeltaTime(). RigidBody is read-only here, so only
*                Transform gets its change tick stamped.
*
* \history
*    -v1.0       stub implementation, no physics logic yet
*    -v1.1       explicit Euler integration of velocity into position
*    -v1.2       Describe() for the system scheduler
*
**************************************************************************/
#include "ECSPhysicsSystem.h"
#include <ECS/World.h>

namespace Crucible
{
	void ECSPhysicsSystem::Init()
	{
	}
	void ECSPhysicsSystem::Describe(SystemDesc& desc) const
	{
		// Pure component math: safe on a worker once a job system exists.
		desc.Uses<BodyQuery>().Affinity(SystemAffinity::Worker);
	}
	void ECSPhysicsSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands)
	{
		if (!m_bodies) m_bodies.emplace(world.CreateQuery<Transform, const RigidBody>());

		const float dt = world.DeltaTime();

		m_bodies->ForEach([dt](Entity, Transform& transform, const RigidBody& body)
			{
				transform.positions.x += body.velocity.x * dt;
				transform.positions.y += body.velocity.y * dt;
				transform.positions.z += body.velocity.z * dt;
			});
	}
	void ECSPhysicsSystem::Shutdown() noexcept
	{
		m_bodies.reset();
	}
}
