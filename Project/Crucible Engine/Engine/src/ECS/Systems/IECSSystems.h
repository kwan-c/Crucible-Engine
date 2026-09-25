/*************************************************************************
* \file:         IECSSystems.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.3
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
*    -v1.2       added SystemDesc and the optional Describe() override
*                (access, affinity, RunAfter/RunBefore) for the scheduler
*    -v1.3       RunAfter/RunBefore are required dependencies; optional
*                ones use RunAfterIfPresent/RunBeforeIfPresent
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>

#include "CommandBuffer.h"
#include "SystemAccess.h"


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

	class IECSSystem;

	/*************************************************************************
	 \brief  One RunAfter/RunBefore constraint on another system type.
	 *************************************************************************/
	struct SystemDependency
	{
		std::type_index type;
		bool required = true;   // required: the schedule fails if the other system isn't registered
	};

	/*************************************************************************
	 \brief
		Everything the scheduler needs to know about one system: stage,
		priority, component access, thread affinity and explicit ordering
		constraints.

	 \details
		ECSSystemManager fills stage/priority from Stage()/Priority() and
		then calls IECSSystem::Describe(), which may override or extend any
		field. Systems that never override Describe() keep today's
		behaviour: Stage()/Priority() ordering, undeclared (exclusive)
		access and MainThread affinity.

	 \note
		RunAfter<T>()/RunBefore<T>() need T to be a complete type, so a
		Describe() that names another system belongs in the .cpp file.
	 *************************************************************************/
	struct SystemDesc
	{
		SystemStage    stage = SystemStage::Update;
		int            priority = 0;                           // lower runs first among ready systems
		SystemAccess   access;
		SystemAffinity affinity = SystemAffinity::MainThread;

		std::vector<SystemDependency> runAfter;    // systems that must run before this one
		std::vector<SystemDependency> runBefore;   // systems that must run after this one

		/*********************************************************************
		 \brief  This system runs after system T, which MUST be registered
				 (Init()/registration fails otherwise). If T is in another
				 stage, stage order must already agree.
		 *********************************************************************/
		template <typename T>
		SystemDesc& RunAfter()
		{
			static_assert(std::is_base_of_v<IECSSystem, T>, "RunAfter<T>: T must derive from IECSSystem");
			runAfter.push_back(SystemDependency{ typeid(T), true });
			return *this;
		}

		/*********************************************************************
		 \brief  This system runs before system T, which MUST be registered
				 (same rules as RunAfter()).
		 *********************************************************************/
		template <typename T>
		SystemDesc& RunBefore()
		{
			static_assert(std::is_base_of_v<IECSSystem, T>, "RunBefore<T>: T must derive from IECSSystem");
			runBefore.push_back(SystemDependency{ typeid(T), true });
			return *this;
		}

		/*********************************************************************
		 \brief  Like RunAfter(), but ignored (with an info log) when T is
				 not registered - for optional features.
		 *********************************************************************/
		template <typename T>
		SystemDesc& RunAfterIfPresent()
		{
			static_assert(std::is_base_of_v<IECSSystem, T>, "RunAfterIfPresent<T>: T must derive from IECSSystem");
			runAfter.push_back(SystemDependency{ typeid(T), false });
			return *this;
		}

		/*********************************************************************
		 \brief  Like RunBefore(), but ignored (with an info log) when T is
				 not registered - for optional features.
		 *********************************************************************/
		template <typename T>
		SystemDesc& RunBeforeIfPresent()
		{
			static_assert(std::is_base_of_v<IECSSystem, T>, "RunBeforeIfPresent<T>: T must derive from IECSSystem");
			runBefore.push_back(SystemDependency{ typeid(T), false });
			return *this;
		}

		/*********************************************************************
		 \brief  Declares read-only access to the given engine components.
		 *********************************************************************/
		template <typename... Components>
		SystemDesc& Reads()
		{
			access.reads |= MakeSignature<Components...>();
			access.declared = true;
			return *this;
		}

		/*********************************************************************
		 \brief  Declares write access to the given engine components.
		 *********************************************************************/
		template <typename... Components>
		SystemDesc& Writes()
		{
			access.writes |= MakeSignature<Components...>();
			access.declared = true;
			return *this;
		}

		/*********************************************************************
		 \brief  Declares the access of a query type the system iterates,
				 e.g. Uses<Query<Transform, const RigidBody>>().
		 \tparam QueryT A Query<...> type (see Query::AppendAccess()).
		 *********************************************************************/
		template <typename QueryT>
		SystemDesc& Uses()
		{
			QueryT::AppendAccess(access);
			return *this;
		}

		/*********************************************************************
		 \brief  Marks the system as conflicting with every other system.
		 *********************************************************************/
		SystemDesc& Exclusive()
		{
			access.exclusive = true;
			access.declared = true;
			return *this;
		}

		/*********************************************************************
		 \brief  Declares that the system touches no engine components
				 (so it conflicts with nothing).
		 *********************************************************************/
		SystemDesc& NoComponentAccess()
		{
			access.declared = true;
			return *this;
		}

		/*********************************************************************
		 \brief  Sets the thread the system asks to run on (metadata only
				 until a job system exists).
		 *********************************************************************/
		SystemDesc& Affinity(SystemAffinity value)
		{
			affinity = value;
			return *this;
		}
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

		/*********************************************************************
		 \brief  Optional scheduling description: access, affinity and
				 RunAfter/RunBefore dependencies.
		 \param[in,out] desc Already filled from Stage()/Priority(); add to
				 or override it here. No need to call the base version.
		 \note  Called when the schedule is built (Init() and registration
				 after Init()), not every frame.
		 *********************************************************************/
		virtual void Describe([[maybe_unused]] SystemDesc& desc) const {}
	};
}
