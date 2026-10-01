/*************************************************************************
* \file:         EventWriter.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 September 2026
* \version       1.1
* \brief
*                Declares EventWriter: the single send-only abstraction every
*                event producer uses to record events for the engine,
*                gameplay and network buses.
*
* \details
*                EventWriter is NOT another bus. It only records events into
*                outboxes; the outboxes are merged into their buses later and
*                delivered on the bus owner (main) thread. Every producer
*                follows the same model:
*                    producer -> EventWriter -> outbox -> merge -> bus -> listeners
*
*                Routing is always explicit (there is no Emit(event) that
*                silently picks a bus):
*                  - EmitEngine(e)   -> engine bus   (window, asset, scene,
*                                       native facts such as CollisionStarted)
*                  - EmitGameplay(e) -> gameplay bus (gameplay decisions)
*                  - EmitNetwork(e)  -> network bus  (connection-level events)
*
*                Which routes a producer may use is an OWNERSHIP decision,
*                made when the writer is created (EventRoute flags):
*                  - Native ECS systems get Engine + Network. Gameplay only if
*                    the system declares SystemDesc::EmitsGameplayEvents() in
*                    Describe() - gameplay logic belongs to scripting, so a
*                    native system should almost never do this.
*                  - The future script bridge is the main gameplay producer.
*                    As a system it declares EmitsGameplayEvents(); outside
*                    the scheduler it owns an EventOutbox and builds its own
*                    gameplay-capable writer.
*                Using a route the writer wasn't given throws
*                std::logic_error at the call - for a system that is a normal
*                system failure (logged, its commands/events discarded,
*                disabled after 3 failing frames). This is a guardrail, not a
*                security boundary: main-thread code can still call a bus's
*                Enqueue() directly.
*
*                Nothing is forwarded between buses automatically. The
*                planned flow is: native CollisionStarted -> engine bus ->
*                interested scripts -> script decides -> EmitGameplay
*                (e.g. DamageDealt) -> gameplay bus.
*
*                When system events are delivered (per stage, main thread):
*                  1. every system in the stage runs Update();
*                  2. the stage's command buffers are replayed (scheduler
*                     order); ECS lifecycle hooks fire here and queue their
*                     events on the gameplay bus;
*                  3. every system's outboxes are merged into their buses in
*                     scheduler execution order, each outbox keeping its own
*                     send order;
*                  4. the buses deliver in a fixed order: engine, network,
*                     gameplay. Order is guaranteed only WITHIN a bus. On the
*                     gameplay bus, the lifecycle events from step 2 come
*                     before the systems' gameplay events from step 3.
*
*                Failure semantics (a system's Update() or command replay
*                throws): that system's pending commands and pending events
*                (all routes) are discarded. Not rolled back: component data
*                it already wrote directly, commands already replayed, and
*                events already merged or delivered.
*
*                Events describe something that already happened; by the
*                time a listener runs the entity may have changed again -
*                check World::IsAlive() and re-read components.
*
* \history
*    -v1.0       initial send-only writer (engine/network routing, delayed
*                variants)
*    -v1.1       gameplay route added behind per-writer route capabilities
*                (EventRoute); undeclared routes throw
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "EventBus.h"

namespace Crucible
{
	/*************************************************************************
	 \brief  The event routes a writer may use (combine with |).
	 *************************************************************************/
	enum class EventRoute : std::uint8_t
	{
		None     = 0,
		Engine   = 1 << 0,
		Gameplay = 1 << 1,
		Network  = 1 << 2,
		All      = Engine | Gameplay | Network
	};

	constexpr EventRoute operator|(EventRoute a, EventRoute b) noexcept
	{
		return static_cast<EventRoute>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
	}

	constexpr bool HasRoute(EventRoute set, EventRoute route) noexcept
	{
		return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(route)) != 0;
	}

	/*************************************************************************
	 \brief
		Send-only, non-owning handle to one producer's engine, gameplay and
		network outboxes, limited to the routes the producer owns.

	 \details
		For a system, ECSSystemManager creates one right before calling
		Update(World&, SystemContext&) and destroys it right after. Every
		Emit* call only records the event; listeners run later (see the
		file header).

		\code
		void CollisionSystem::Update(World& world, SystemContext& ctx)
		{
			ctx.events.EmitEngine(CollisionStarted{ a, b });   // native fact
		}
		\endcode

		The interface intentionally has no Clear, Merge, Flush, subscription
		or queue access - a producer can only add events.

	 \warning
		Do not store the writer (or a pointer/reference to it) and do not use
		it from work that outlives the call it was given for, e.g. a job
		still running after Update() returned. It is non-copyable to make
		retaining it harder.

	 \note
		Each Emit* returns false if the event was dropped because that
		outbox is full (see EventOutbox::SetMaxRecords); drops are counted
		in the bus's DroppedEvents() when the outbox is merged. Using a
		route the writer wasn't given throws std::logic_error.
	 *************************************************************************/
	class EventWriter
	{
	public:
		/*********************************************************************
		 \brief  Wraps one producer's outboxes.
		 \param[in] engineOutbox   Where EmitEngine* records events.
		 \param[in] gameplayOutbox Where EmitGameplay* records events.
		 \param[in] networkOutbox  Where EmitNetwork* records events.
		 \param[in] allowedRoutes  Routes this producer owns; the others throw.
		 \param[in] producerName   Used in the error message (must outlive
					the writer; a string literal or a system name).
		 *********************************************************************/
		EventWriter(EventOutbox& engineOutbox, EventOutbox& gameplayOutbox, EventOutbox& networkOutbox,
			EventRoute allowedRoutes, const char* producerName) noexcept
			: m_engine(engineOutbox), m_gameplay(gameplayOutbox), m_network(networkOutbox),
			m_allowed(allowedRoutes), m_producer(producerName ? producerName : "") {}

		// Non-copyable and non-movable: it only lives for one call.
		EventWriter(const EventWriter&) = delete;
		EventWriter& operator=(const EventWriter&) = delete;
		EventWriter(EventWriter&&) = delete;
		EventWriter& operator=(EventWriter&&) = delete;

		/*********************************************************************
		 \brief  Whether this writer may use a route (e.g. to skip work).
		 *********************************************************************/
		bool CanEmit(EventRoute route) const noexcept { return HasRoute(m_allowed, route); }

		// -- Engine bus ---------------------------------------------------------------

		/*********************************************************************
		 \brief  Records an ENGINE-bus event, delivered at the end of the
				 stage. \return false if dropped (outbox full).
		 *********************************************************************/
		template <typename T>
		bool EmitEngine(T event) { Require(EventRoute::Engine, "EmitEngine"); return m_engine.Send(std::move(event)); }

		template <typename T>
		bool EmitEngineAfterSeconds(T event, float seconds) { Require(EventRoute::Engine, "EmitEngineAfterSeconds"); return m_engine.SendAfterSeconds(std::move(event), seconds); }

		template <typename T>
		bool EmitEngineAfterFrames(T event, std::uint32_t frames) { Require(EventRoute::Engine, "EmitEngineAfterFrames"); return m_engine.SendAfterFrames(std::move(event), frames); }

		// -- Gameplay bus (ownership-restricted) ---------------------------------------

		/*********************************************************************
		 \brief  Records a GAMEPLAY-bus event, delivered at the end of the
				 stage. \return false if dropped (outbox full).
		 \note   Only for producers that own gameplay decisions (the script
				 bridge, or a system that declared EmitsGameplayEvents());
				 throws std::logic_error otherwise.
		 *********************************************************************/
		template <typename T>
		bool EmitGameplay(T event) { Require(EventRoute::Gameplay, "EmitGameplay"); return m_gameplay.Send(std::move(event)); }

		template <typename T>
		bool EmitGameplayAfterSeconds(T event, float seconds) { Require(EventRoute::Gameplay, "EmitGameplayAfterSeconds"); return m_gameplay.SendAfterSeconds(std::move(event), seconds); }

		template <typename T>
		bool EmitGameplayAfterFrames(T event, std::uint32_t frames) { Require(EventRoute::Gameplay, "EmitGameplayAfterFrames"); return m_gameplay.SendAfterFrames(std::move(event), frames); }

		// -- Network bus ---------------------------------------------------------------

		/*********************************************************************
		 \brief  Records a NETWORK-bus event, delivered at the end of the
				 stage. \return false if dropped (outbox full).
		 *********************************************************************/
		template <typename T>
		bool EmitNetwork(T event) { Require(EventRoute::Network, "EmitNetwork"); return m_network.Send(std::move(event)); }

		template <typename T>
		bool EmitNetworkAfterSeconds(T event, float seconds) { Require(EventRoute::Network, "EmitNetworkAfterSeconds"); return m_network.SendAfterSeconds(std::move(event), seconds); }

		template <typename T>
		bool EmitNetworkAfterFrames(T event, std::uint32_t frames) { Require(EventRoute::Network, "EmitNetworkAfterFrames"); return m_network.SendAfterFrames(std::move(event), frames); }

	private:
		// Ownership check: a route the producer wasn't given is a programming error.
		void Require(EventRoute route, const char* call) const
		{
			if (HasRoute(m_allowed, route)) return;
			std::string message = std::string("EventWriter: '") + m_producer + "' called " + call +
				" but does not own that event route";
			if (HasRoute(route, EventRoute::Gameplay))
				message += " - gameplay events come from the scripting layer; a system that really owns gameplay "
					"decisions must declare SystemDesc::EmitsGameplayEvents() in Describe()";
			throw std::logic_error(message);
		}

		EventOutbox& m_engine;      // outboxes are owned by the producer (a system slot, or the script bridge)
		EventOutbox& m_gameplay;
		EventOutbox& m_network;
		EventRoute m_allowed;
		const char* m_producer;
	};
}
