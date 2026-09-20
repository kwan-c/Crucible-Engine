/*************************************************************************
* \file:         IECSSystems.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Declares IECSSystem: the base interface every ECS
*                system (physics, rendering, audio, ...) must implement
*                so it can be driven generically by
*                ECSSystemManager/ECSSystemRegistry.
*
* \details
*                A "system" here is anything that runs logic over the
*                World once per frame (see World.h). Concrete systems
*                derive from IECSSystem and override Update() (and
*                optionally Init()/Shutdown()) - see ECSPhysicsSystem.h/
*                .cpp for the current (placeholder) example. Systems are
*                registered and looked up by id in
*                IECSSystemRegistry.h/.cpp, and are driven in a fixed
*                order by ECSSystemManager.h/.cpp.
*
* \history
*    -v1.0       initial system interface (Init/Update/Shutdown)
*
*
**************************************************************************/
#pragma once
#include "Commandbuffer.h"


namespace Crucible
{
	class World;

	// base system class for ecs

	/*************************************************************************
	 \brief
		Base interface every ECS system (physics, rendering, audio, ...)
		must implement so it can be driven generically by
		ECSSystemManager/ECSSystemRegistry.

	 \details
		A "system" is anything that runs logic over the World once per
		frame. Concrete systems derive from IECSSystem and override Update()
		(optionally Init()/Shutdown() too) - see ECSPhysicsSystem for an
		example. Systems are registered/looked up by id in
		ECSSystemRegistry, and driven in a fixed order by ECSSystemManager.
	 *************************************************************************/
	class IECSSystem
	{
	public:
		
		/*********************************************************************
		 \brief  Virtual destructor so systems can be destroyed via
				 IECSSystem*.
		 *********************************************************************/
		virtual ~IECSSystem() = default;

		/*********************************************************************
		 \brief  One-time setup, called once before the first Update().
		 \details Default implementation does nothing; override only if the
				  system needs setup.
		 *********************************************************************/
		virtual void Init() {}

		/*********************************************************************
		 \brief  Runs this system's per-frame logic over a World.
		 \param[in,out] world The world to read/modify.
		 \note  Pure virtual - every concrete system must implement this.
		 *********************************************************************/
		virtual void Update(World& world, CommandBuffer& commands) = 0;
		/*********************************************************************
		 \brief  One-time teardown, called once when the engine shuts down.
		 \details Default implementation does nothing; override only if the
				  system owns resources needing explicit release.
		 *********************************************************************/
		virtual void Shutdown()noexcept {}


	};
}