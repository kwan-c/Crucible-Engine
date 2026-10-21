/*************************************************************************
* \file:         EcsEvents.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.1
* \brief
*                Declares the ECS lifecycle event types (EntityDestroyed,
*                ComponentAdded<T>, ComponentRemoved<T>) and EcsEventBridge,
*                which turns World hooks into queued events on a bus.
*
* \details
*                Hooks vs events:
*                  - World hooks are the immediate, internal mechanism: they
*                    run INSIDE a structural change, under the iteration
*                    guard, and are used by engine code that must keep data
*                    consistent (GPU handles, hierarchy links).
*                  - Events are the deferred, public mechanism: "something
*                    happened that others may care about". They are
*                    delivered at the stage flush, on the main thread, where
*                    listeners may change the World directly.
*
*                The bridge is opt-in per component type: it installs a World
*                hook only when someone subscribes to that type, and the
*                hook only queues an event. Types nobody listens to cost
*                nothing. Building on hooks (instead of new calls inside
*                World) means every structural path is covered:
*                AddComponent, EmplaceComponent, CreateEntity(signature),
*                CreateEntities, CommandBuffer spawns, RemoveComponent and
*                DestroyEntity.
*
* \history
*    -v1.0       initial opt-in hook-to-event bridge
*    -v1.1       hooks take const T& (World passes EntityName only as const)
*
**************************************************************************/
#pragma once

#include <bitset>
#include <functional>
#include <type_traits>
#include <utility>

#include "../../World.h"
#include "EventBus.h"

namespace Crucible
{
	/*************************************************************************
	 \brief
		Deferred ECS lifecycle events.

	 \details
		These are generated ONLY by World hooks through EcsEventBridge -
		systems never emit them by hand. A hook queues the event on the
		gameplay bus the moment the change happens (a removal hook runs
		BEFORE the component is destroyed, so ComponentRemoved carries a
		copy of the old value). Delivery happens later: at the end of the
		stage, after all of that stage's command buffers have been replayed
		(or, for changes made outside systems, at the next stage flush).

	 \warning
		Events describe something that already happened. By the time a
		listener runs, a later command may have changed the entity again
		or destroyed it. Check world.IsAlive(e.entity) and re-read
		components instead of assuming the state in the event is current.
	 *************************************************************************/
	namespace EcsEvents
	{
		/*********************************************************************
		 \brief  An entity was destroyed.
		 *********************************************************************/
		struct EntityDestroyed { Entity entity; };

		/*********************************************************************
		 \brief  An entity gained a T (read the value from the World).
		 *********************************************************************/
		template <typename T>
		struct ComponentAdded { Entity entity; };

		/*********************************************************************
		 \brief  An entity lost a T. Carries a copy of the component, since
				 the entity is usually gone by the time this is delivered.
		 *********************************************************************/
		template <typename T>
		struct ComponentRemoved { Entity entity; T lastValue; };
	}

	/*************************************************************************
	 \brief
		Turns World hooks into queued events on one bus - opt-in per
		component type.

	 \details
		Usually used through the On*() helpers, which enable the hook and
		subscribe in one call:
		\code
		m_onMeshRemoved = core.EcsBridge().OnRemoved<MeshRenderer>(
			[this](const EcsEvents::ComponentRemoved<MeshRenderer>& e) { ... });
		\endcode

		World hooks can't be unregistered, so each type's hook is installed
		once and stays. When nobody listens any more, the bus's
		no-listener early-out keeps it (almost) free.

	 \note
		The bus must outlive the World's hooks. Core declares its buses
		before the World, and destroys every entity during CleanUp(), so the
		hooks never call into a destroyed bus.
	 *************************************************************************/
	class EcsEventBridge
	{
	public:
		/*********************************************************************
		 \brief  Creates a bridge that queues onto `bus` from `world`'s hooks.
		 *********************************************************************/
		EcsEventBridge(World& world, EventBus& bus) : m_world(world), m_bus(bus) {}

		EcsEventBridge(const EcsEventBridge&) = delete;
		EcsEventBridge& operator=(const EcsEventBridge&) = delete;

		// -- Subscribe + enable in one call (the usual way to use it) ---------------

		/*********************************************************************
		 \brief  Listens for destroyed entities.
		 \return The subscription - keep it; releasing it stops listening.
		 *********************************************************************/
		Subscription OnEntityDestroyed(std::function<void(const EcsEvents::EntityDestroyed&)> fn, int priority = 0)
		{
			EnableEntityDestroyed();
			return m_bus.Subscribe<EcsEvents::EntityDestroyed>(std::move(fn), priority);
		}

		/*********************************************************************
		 \brief  Listens for entities gaining a T.
		 *********************************************************************/
		template <RegisteredComponent T>
		Subscription OnAdded(std::function<void(const EcsEvents::ComponentAdded<T>&)> fn, int priority = 0)
		{
			EnableComponentAdded<T>();
			return m_bus.Subscribe<EcsEvents::ComponentAdded<T>>(std::move(fn), priority);
		}

		/*********************************************************************
		 \brief  Listens for entities losing a T (including by destruction).
		 *********************************************************************/
		template <RegisteredComponent T>
		Subscription OnRemoved(std::function<void(const EcsEvents::ComponentRemoved<T>&)> fn, int priority = 0)
		{
			EnableComponentRemoved<T>();
			return m_bus.Subscribe<EcsEvents::ComponentRemoved<T>>(std::move(fn), priority);
		}

		// -- Enabling (idempotent: each hook is installed at most once) -------------

		/*********************************************************************
		 \brief  Installs the OnEntityDestroyed hook (once).
		 *********************************************************************/
		void EnableEntityDestroyed()
		{
			if (m_destroyedEnabled) return;
			EventBus* bus = &m_bus;
			// The hook only queues; the event is delivered at the next flush.
			m_world.OnEntityDestroyed([bus](World&, Entity e)
				{
					bus->Enqueue(EcsEvents::EntityDestroyed{ e });
				});
			m_destroyedEnabled = true;
		}

		/*********************************************************************
		 \brief  Installs the OnComponentAdded<T> hook (once).
		 *********************************************************************/
		template <RegisteredComponent T>
		void EnableComponentAdded()
		{
			const ComponentId id = ComponentTypeId<T>();
			if (m_addedEnabled.test(id)) return;
			EventBus* bus = &m_bus;
			m_world.OnComponentAdded<T>([bus](World&, Entity e, const T&)
				{
					bus->Enqueue(EcsEvents::ComponentAdded<T>{ e });
				});
			m_addedEnabled.set(id);
		}

		/*********************************************************************
		 \brief  Installs the OnComponentRemoved<T> hook (once).
		 *********************************************************************/
		template <RegisteredComponent T>
		void EnableComponentRemoved()
		{
			static_assert(std::is_copy_constructible_v<T>,
				"ComponentRemoved<T> carries a copy of T; T must be copyable");
			const ComponentId id = ComponentTypeId<T>();
			if (m_removedEnabled.test(id)) return;
			EventBus* bus = &m_bus;
			// Remove hooks run BEFORE the component is torn down, so the copy is still valid here.
			m_world.OnComponentRemoved<T>([bus](World&, Entity e, const T& value)
				{
					bus->Enqueue(EcsEvents::ComponentRemoved<T>{ e, value });
				});
			m_removedEnabled.set(id);
		}

	private:
		World& m_world;
		EventBus& m_bus;
		std::bitset<kMaxComponents> m_addedEnabled;     // per component id: hook installed?
		std::bitset<kMaxComponents> m_removedEnabled;
		bool m_destroyedEnabled = false;
	};
}
