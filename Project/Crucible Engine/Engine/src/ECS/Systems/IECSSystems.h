/*************************************************************************
* \file:         IECSSystems.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.1
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
*                optionally Init()/Shutdown()/Priority()/Stage()) - see
*                ECSPhysicsSystem.h/.cpp for an example. Systems register
*                through ECSSystemManager::RegisterSystem<T>() and run in
*                (Stage(), Priority()) order.
*
* \history
*    -v1.0       initial system interface (Init/Update/Shutdown)
*    -v1.1       added SystemStage and the optional Stage() override;
*                existing Init/Update/Shutdown/Priority unchanged
*
**************************************************************************/
#pragma once

#include <cstdint>

#include "CommandBuffer.h"


namespace Crucible
{
	class World;

	/*************************************************************************
	 \brief
		Coarse execution phases for systems.

	 \details
		Systems run stage by stage, ordered by Priority() within a stage.
		Each system's CommandBuffer is flushed at the end of its stage, so
		later stages see earlier stages' structural changes in the same
		frame.
	 *************************************************************************/
	enum class SystemStage : std::uint8_t
	{
		PreUpdate,    // input, network receive
		Update,       // main per-frame logic - default stage for every system
		PostUpdate,   // physics, transform propagation
		PreRender,    // render extraction
		Count
	};

	// base system class for ecs

	/*************************************************************************
	 \brief
		Base interface every ECS system (physics, rendering, audio, ...)
		must implement so it can be driven generically by
		ECSSystemManager/ECSSystemRegistry.

	 \details
		A "system" is anything that runs logic over the World once per
		frame. Concrete systems derive from IECSSystem and override Update()
		(optionally Init()/Shutdown()/Priority()/Stage() too) - see
		ECSPhysicsSystem for an example.
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
		 \param[in,out] commands This system's own CommandBuffer; anything
				queued here is applied at the end of the system's stage.
		 \note  Pure virtual - every concrete system must implement this.
				Frame delta time is available through World::DeltaTime().
		 *********************************************************************/
		virtual void Update(World& world, CommandBuffer& commands) = 0;

		/*********************************************************************
		 \brief  One-time teardown, called once when the engine shuts down.
		 \details Default implementation does nothing; override only if the
				  system owns resources needing explicit release.
		 *********************************************************************/
		virtual void Shutdown()noexcept {}

		/*********************************************************************
		 \brief  Order within the system's stage; lower runs first.
		 *********************************************************************/
		virtual int Priority() const { return 0; }

		/*********************************************************************
		 \brief  The stage this system runs in (optional to override).
		 \return SystemStage::Update unless overridden.
		 *********************************************************************/
		virtual SystemStage Stage() const { return SystemStage::Update; }
	};
}
