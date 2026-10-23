/*************************************************************************
* \file:         IECSSystems.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.8
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
*    -v1.4       added the Update(World&, SystemContext&) overload for event
*                outboxes; Update(World&, CommandBuffer&) keeps its
*                signature and now has an empty default
*    -v1.5       SystemContext carries an EventWriter (explicit
*                EmitEngine/EmitNetwork) instead of per-bus outboxes
*    -v1.6       SystemDesc::EmitsGameplayEvents() - the declaration that
*                authorises ctx.events.EmitGameplay for a system
*    -v1.7       SystemDesc::SimulationOnly() - runs only while the
*                simulation is active (play mode)
*    -v1.8       component on/off handling, set at registration:
*                IECSSystem::ReactiveTo<T...>() / ReactiveComponents(),
*                OnComponentActivated / OnComponentDeactivated;
*                SystemDesc::reactiveComponents (for the schedule log).
*                Filtered by default.
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
#include "SystemContext.h"


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
		PostUpdate,   // physics
		PreRender,    // transform propagation (first), then render extraction
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

		// true = this system owns gameplay decisions and may use ctx.events.EmitGameplay(...).
		// Native engine systems leave it false: gameplay events come from scripting.
		bool emitsGameplayEvents = false;

		// true = runs only while the simulation is active (Playing); see
		// ECSSystemManager::SetSimulationActive. Physics and gameplay set it.
		bool simulationOnly = false;
		// Components whose on/off changes this system is told about (IECSSystem::ReactiveTo, set
		// at registration). Copied here by the manager for the schedule log; empty = filtered only.
		ComponentSignature reactiveComponents;

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

		/*********************************************************************
		 \brief  Allows this system to use ctx.events.EmitGameplay(...).
		 \details Gameplay decisions belong to the scripting layer, so this
				  is meant for the script bridge (or another system that
				  genuinely owns gameplay decisions) - not for native engine
				  systems. Without it, EmitGameplay throws std::logic_error.
		 *********************************************************************/
		SystemDesc& EmitsGameplayEvents()
		{
			emitsGameplayEvents = true;
			return *this;
		}

		/*********************************************************************
		 \brief  This system simulates: it runs only while the simulation is
				 active (Playing), not in Edit or Paused.
		 \details The system stays registered and alive throughout; the
				  manager just skips its Update (and its command replay)
				  while ECSSystemManager::SetSimulationActive(false).
		 *********************************************************************/
		SystemDesc& SimulationOnly()
		{
			simulationOnly = true;
			return *this;
		}
	};

	class IECSSystem;
	namespace Detail
	{
		// Whether System overrides OnComponentActivated or OnComponentDeactivated (defined below).
		template <typename System>
		consteval bool OverridesActivationCallbacks();
	}

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
		 \brief  Runs this system's per-frame logic over a World - the
				 original entry point, used by systems that don't send
				 events.
		 \param[in,out] world The world to read/modify.
		 \param[in,out] commands This system's own CommandBuffer; anything
				queued here is applied at the end of the system's stage.
		 \note  Override EITHER this OR Update(World&, SystemContext&).
				Frame delta time is available through World::DeltaTime().
				(It used to be pure virtual; it now has an empty default so
				systems that only override the context version still
				compile. Existing overrides are unaffected.)
		 *********************************************************************/
		virtual void Update([[maybe_unused]] World& world, [[maybe_unused]] CommandBuffer& commands) {}

		/*********************************************************************
		 \brief  Runs this system's per-frame logic with its full context:
				 CommandBuffer plus EventWriter.
		 \param[in,out] world The world to read/modify.
		 \param[in,out] ctx   This system's commands and event writer;
				everything written here is applied/delivered at the end of
				the system's stage (commands first, then events).
		 \details The system manager always calls this one. The default
				 forwards to Update(World&, CommandBuffer&), so systems
				 written before events existed keep working unchanged.
				 Override this version to send events:
				 \code
				 void MySystem::Update(World& world, SystemContext& ctx)
				 {
					 ctx.commands.DestroyEntity(e);
					 ctx.events.EmitEngine(SomethingHappened{ e });
				 }
				 \endcode
				 Engine and network routes are always available;
				 ctx.events.EmitGameplay needs SystemDesc::EmitsGameplayEvents()
				 (gameplay decisions belong to scripting). Listeners never run
				 during Update(). Don't keep ctx (or its members) beyond this
				 call.
		 *********************************************************************/
		virtual void Update(World& world, SystemContext& ctx) { Update(world, ctx.commands); }

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

		/*********************************************************************
		 \brief
			Makes this system reactive to Components: besides the usual
			query filtering, ECSSystemManager calls its OnComponentActivated
			/ OnComponentDeactivated whenever one of them starts or stops
			being active on an entity - switched on/off itself, or through a
			component it requires (RigidBody when its Collider is switched
			off). For systems that keep runtime state (physics bodies,
			voices, gameplay state).
		 \details
			Meant to be chained onto registration; it returns the system
			itself (its own type), so references keep working:
			\code
			m_systems.RegisterSystem<ECSPhysicsSystem>()
				.ReactiveTo<Collider, RigidBody, CharacterController>();
			auto& animator = m_systems.RegisterSystem<AnimatedModelSystem>(...)
				.ReactiveTo<MeshRenderer>();
			\endcode
			Calling it again adds components; nothing removes them. A system
			never made reactive is filtered only (the default). The manager
			starts routing at its next Update (or AttachWorld), so configure
			it at registration.
		 \note   Fails to compile for a component that can't be switched off,
				 for an empty list, and for a system that overrides neither
				 callback.
		 *********************************************************************/
		template <ToggleableComponent... Components, typename Self>
		Self& ReactiveTo(this Self& self);

		/*********************************************************************
		 \brief  The components this system is reactive to (ReactiveTo);
				 empty for a filtered-only system.
		 *********************************************************************/
		const ComponentSignature& ReactiveComponents() const noexcept { return m_reactiveComponents; }

		/*********************************************************************
		 \brief  Reactive systems only (ReactiveTo): `component` - one the
				 system is reactive to - started / stopped being active on
				 entity: it, or a component it requires, was switched on/off.
		 \details Delivered just before this system's next Update, inside
				  its declared access scope, one call per change in the
				  exact order the changes happened, across all entities
				  and components. A component switched off and on again before
				  delivery arrives as Deactivated then Activated. Not called
				  for adding, removing or destroying components (changes
				  queued for a component that is removed before delivery
				  are dropped, even if it is added back), nor while
				  the system isn't running (kept until it runs again; cleared
				  when a play session starts or stops for SimulationOnly
				  systems). An exception counts as a failure of the system's
				  frame. Override either or both, public or private.
		 *********************************************************************/
		virtual void OnComponentActivated([[maybe_unused]] World& world, [[maybe_unused]] Entity entity,
			[[maybe_unused]] ComponentId component) {}
		virtual void OnComponentDeactivated([[maybe_unused]] World& world, [[maybe_unused]] Entity entity,
			[[maybe_unused]] ComponentId component) {}

	private:
		ComponentSignature m_reactiveComponents;   // ReactiveTo
	};

	namespace Detail
	{
		template <typename System>
		consteval bool OverridesActivationCallbacks()
		{
			using Callback = void (IECSSystem::*)(World&, Entity, ComponentId);
			// The base versions are public, so a callback System can't reach from here is one
			// System declared itself (private/protected override); a reachable one is System's
			// own when its member pointer names System rather than IECSSystem.
			bool overrides = false;
			if constexpr (requires { &System::OnComponentActivated; })
				overrides |= !std::is_same_v<decltype(&System::OnComponentActivated), Callback>;
			else
				overrides = true;
			if constexpr (requires { &System::OnComponentDeactivated; })
				overrides |= !std::is_same_v<decltype(&System::OnComponentDeactivated), Callback>;
			else
				overrides = true;
			return overrides;
		}
	}

	template <ToggleableComponent... Components, typename Self>
	Self& IECSSystem::ReactiveTo(this Self& self)
	{
		static_assert(std::is_base_of_v<IECSSystem, Self>, "ReactiveTo: call it on a system");
		static_assert(sizeof...(Components) > 0, "ReactiveTo<Components...>: name at least one component");
		static_assert(Detail::OverridesActivationCallbacks<Self>(),
			"ReactiveTo: this system overrides neither OnComponentActivated nor OnComponentDeactivated");
		static_cast<IECSSystem&>(self).m_reactiveComponents |= MakeSignature<Components...>();
		return self;
	}
}
