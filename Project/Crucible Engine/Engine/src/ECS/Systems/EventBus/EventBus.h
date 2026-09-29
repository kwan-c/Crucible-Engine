/*************************************************************************
* \file:         EventBus.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.1
* \brief
*                Declares EventBus (type-based publish/subscribe for one
*                domain), EventBusConfig (its rules and limits) and
*                EventOutbox (where a system records events during
*                Update()).
*
* \details
*                The event system in one paragraph: events are plain
*                structs. Each event type has its own channel (see
*                EventChannel.h), so Send<EnemyKilled> only reaches
*                EnemyKilled's listeners. Systems never call the bus while
*                they run - they record events through the EventWriter in
*                their SystemContext (see EventWriter.h), which writes into
*                that system's own EventOutbox. At the end
*                of each stage the system manager merges every outbox into
*                its bus (in the scheduler's serial order, so the event order
*                is deterministic) and the bus delivers everything on the
*                main thread.
*
*                Delivery happens in ROUNDS: round 1 delivers what was queued;
*                anything listeners send during round 1 is delivered in round
*                2, and so on, up to maxRoundsPerFlush rounds. Within a round
*                events are delivered in the exact order they were sent,
*                across ALL event types, so an EnemyKilled can never overtake
*                the DamageDealt that caused it.
*
*                Channels are keyed by std::type_index, not a counter: the
*                Engine is a DLL and the Game a separate executable, and a
*                per-module counter would give the same type different ids
*                on each side.
*
*                Header-only on purpose: the Game module instantiates the
*                templates for its own event types.
*
* \history
*    -v1.0       initial bus: deferred rounds in global send order, delayed
*                events, outboxes, immediate dispatch (engine bus only),
*                bounded queue/rounds/recursion, trace ring buffer
*    -v1.1       delayed events and outboxes are bounded too
*                (maxDelayedEvents, EventOutbox::SetMaxRecords; drops are
*                counted), geometric growth instead of exact-size reserves
*                (delayed inserts were quadratic)
*
**************************************************************************/
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "EventChannel.h"
#include "Subscription.h"

namespace Crucible
{
	class EventBus;

	/*************************************************************************
	 \brief
		Events one system recorded for one bus during its Update().

	 \details
		Lives in the system's SystemSlot (one outbox per bus), so a system
		running on a worker thread only ever touches its own outbox - no
		locks needed. The system manager merges it into the bus at the end
		of the stage. Clear() keeps capacity, so a steady frame reuses the
		same storage.

	 \note
		Each record stores a small std::function that knows how to enqueue
		the event on a bus; the outbox itself never touches a bus.
		An outbox holds at most MaxRecords() events per stage; further sends
		are dropped and counted, and the count is added to the bus's
		DroppedEvents() when the outbox is merged - so one runaway system
		can't eat memory.
	 *************************************************************************/
	class EventOutbox
	{
	public:
		static constexpr std::size_t kDefaultMaxRecords = std::size_t{ 1 } << 16;

		/*********************************************************************
		 \brief  Records an event, delivered at the end of this stage.
		 \return false if the outbox is full (the event is dropped + counted).
		 *********************************************************************/
		template <typename T> bool Send(T event);

		/*********************************************************************
		 \brief  Records an event delivered once `seconds` of game time pass.
		 \return false if the outbox is full (the event is dropped + counted).
		 *********************************************************************/
		template <typename T> bool SendAfterSeconds(T event, float seconds);

		/*********************************************************************
		 \brief  Records an event delivered after `frames` frames.
		 \return false if the outbox is full (the event is dropped + counted).
		 *********************************************************************/
		template <typename T> bool SendAfterFrames(T event, std::uint32_t frames);

		bool Empty() const noexcept { return m_records.empty(); }
		std::size_t Size() const noexcept { return m_records.size(); }

		/*********************************************************************
		 \brief  Discards every recorded event (keeps capacity). The drop
				 count is kept until the next merge reports it.
		 *********************************************************************/
		void Clear() noexcept { m_records.clear(); }

		/*********************************************************************
		 \brief  Sets how many events one stage may record (default 65536).
		 *********************************************************************/
		void SetMaxRecords(std::size_t maxRecords) noexcept { m_maxRecords = maxRecords; }
		std::size_t MaxRecords() const noexcept { return m_maxRecords; }

		/*********************************************************************
		 \brief  Sends dropped because the outbox was full, not yet reported
				 to a bus.
		 *********************************************************************/
		std::uint64_t DroppedCount() const noexcept { return m_dropped; }

	private:
		friend class EventBus;

		struct Record
		{
			float seconds = 0.0f;                       // > 0: delayed by time
			std::uint32_t frames = 0;                   // > 0: delayed by frames
			std::function<void(EventBus&)> enqueue;     // puts the event on a bus
		};

		// Common body of the three Send functions.
		template <typename T>
		bool Add(float seconds, std::uint32_t frames, T&& event);

		std::vector<Record> m_records;
		std::size_t m_maxRecords = kDefaultMaxRecords;
		std::uint64_t m_dropped = 0;                    // reported (and reset) by EventBus::Merge
	};

	/*************************************************************************
	 \brief  Per-bus rules and limits.
	 *************************************************************************/
	struct EventBusConfig
	{
		const char* name = "EventBus";           // used in errors, logs and the trace
		bool allowImmediate = false;             // Dispatch() allowed (engine bus only)
		std::uint32_t maxRoundsPerFlush = 8;     // cap on "listener sends an event" chains
		std::size_t maxPendingEvents = 1u << 16; // queue bound; further sends are dropped + counted
		std::uint32_t maxImmediateDepth = 16;    // Dispatch() called from inside Dispatch()
		std::size_t maxDelayedEvents = 1u << 16; // delayed-event bound; further ones are dropped + counted
	};

	/*************************************************************************
	 \brief
		Type-based publish/subscribe for ONE domain (engine, gameplay or
		network). Each event type has its own channel, so Send<T> only ever
		reaches T's listeners.

	 \details
		Typical use:
		\code
		Subscription s = bus.Subscribe<WindowResized>([&](const WindowResized& e) { ... });
		bus.Enqueue(WindowResized{ 1280, 720 });   // delivered at the next DeliverPending()
		\endcode

		Threading: every member function is owner-thread only and throws
		std::logic_error otherwise. Systems never call the bus during
		Update(); they record through their EventWriter (see
		EventWriter.h). Subscription::Reset() is the one call allowed
		elsewhere (see Subscription).

		The bus itself knows nothing about the ECS: it can be used on its
		own (Enqueue / DeliverPending), which is also how the ECS lifecycle
		bridge and listeners send.

		Exception safety (listener exceptions never escape - they are caught
		per listener and counted):
		  - Enqueue / Subscribe: strong guarantee.
		  - Merge: events already committed stay queued; on a throw the rest
			of the outbox is dropped and counted. The outbox is always empty
			afterwards (never half-consumed).
		  - DeliverPending: if an internal operation throws (bad_alloc), the
			rest of the CURRENT round is dropped and counted, later-round
			events stay queued, and every channel is left consistent.
		  - Tick: due events that could not be queued are dropped and counted.

		Allocation: all queues and scratch buffers keep their capacity, so
		steady-state frames reuse storage. The remaining per-event cost is
		one std::function per outbox/delayed record.

	 \note
		Non-copyable and non-movable: channels hold a pointer back to the bus
		for failure reporting.
	 *************************************************************************/
	class EventBus
	{
	public:
		/*********************************************************************
		 \brief  Creates a bus owned by the calling thread.
		 \param[in] config The bus's name, rules and limits.
		 *********************************************************************/
		explicit EventBus(EventBusConfig config = {})
			: m_config(config), m_owner(std::this_thread::get_id()) {}

		EventBus(const EventBus&) = delete;
		EventBus& operator=(const EventBus&) = delete;

		// -- Subscribing --------------------------------------------------------------

		/*********************************************************************
		 \brief  Registers a listener for event type T.
		 \param[in] handler  Called with each delivered T.
		 \param[in] priority Lower runs first; equal keeps subscription order.
		 \return The handle that keeps the listener registered - store it.
		 \note   Subscribe in a system's Init() (main thread), never in
				 Update().
		 *********************************************************************/
		template <typename T>
		Subscription Subscribe(std::function<void(const T&)> handler, int priority = 0)
		{
			CheckThread("Subscribe");
			auto channel = GetOrCreate<T>();
			const ListenerId id = m_nextListenerId + 1;
			channel->Subscribe(std::move(handler), priority, id);   // strong guarantee
			m_nextListenerId = id;                                   // only committed on success
			return Subscription(std::weak_ptr<IEventChannel>(channel), id);
		}

		/*********************************************************************
		 \brief  True if at least one listener is registered for T.
		 *********************************************************************/
		template <typename T>
		bool HasListeners() const
		{
			CheckThread("HasListeners");
			auto it = m_channels.find(std::type_index(typeid(T)));
			return it != m_channels.end() && it->second->ListenerCount() > 0;
		}

		// -- Sending ---------------------------------------------------------------------

		/*********************************************************************
		 \brief  Queues an event, delivered at the next DeliverPending().
		 \return false if the event was dropped: nobody listens to T (free,
				 not counted), or the queue is at maxPendingEvents (counted
				 in DroppedEvents()).
		 *********************************************************************/
		template <typename T>
		bool Enqueue(T event)
		{
			CheckThread("Enqueue");
			auto channel = GetOrCreate<T>();
			if (channel->ListenerCount() == 0) return false;   // nobody cares: costs nothing
			if (m_order.size() >= m_config.maxPendingEvents)
			{
				++m_dropped;
				return false;
			}

			// Strong guarantee: record the send order first and undo it if the channel
			// push throws, so the order list and the channel queues can never disagree
			// (a mismatch would deliver the wrong payload).
			m_order.push_back(channel.get());
			try { channel->Enqueue(std::move(event)); }
			catch (...) { m_order.pop_back(); throw; }
			return true;
		}

		/*********************************************************************
		 \brief  Delivers an event to T's listeners right now.
		 \note   Only on buses with allowImmediate (the engine bus). Throws
				 std::logic_error otherwise, or if Dispatch() nests deeper
				 than maxImmediateDepth.
		 *********************************************************************/
		template <typename T>
		void Dispatch(const T& event)
		{
			CheckThread("Dispatch");
			if (!m_config.allowImmediate)
				throw std::logic_error(std::string(m_config.name) + ": immediate Dispatch is disabled on this bus");
			if (m_immediateDepth >= m_config.maxImmediateDepth)
				throw std::logic_error(std::string(m_config.name) + ": immediate Dispatch recursion limit reached");

			auto channel = GetOrCreate<T>();
			++m_immediateDepth;
			struct Restore { std::uint32_t& depth; ~Restore() { --depth; } } restore{ m_immediateDepth };
			channel->Dispatch(event);
		}

		/*********************************************************************
		 \brief  Queues an event once `seconds` of game time have passed
				 (counted by Tick()).
		 \return false if the delayed list is at maxDelayedEvents (the event
				 is dropped and counted in DroppedEvents()).
		 *********************************************************************/
		template <typename T>
		bool EnqueueAfterSeconds(T event, float seconds)
		{
			CheckThread("EnqueueAfterSeconds");
			return AddDelayed(Delayed{ m_time + seconds, 0, MakeEnqueue(std::move(event)) });
		}

		/*********************************************************************
		 \brief  Queues an event after `frames` calls to Tick() (min 1).
		 \return false if the delayed list is at maxDelayedEvents (the event
				 is dropped and counted in DroppedEvents()).
		 *********************************************************************/
		template <typename T>
		bool EnqueueAfterFrames(T event, std::uint32_t frames)
		{
			CheckThread("EnqueueAfterFrames");
			return AddDelayed(Delayed{ 0.0, m_frame + (std::max)(frames, std::uint32_t{ 1 }), MakeEnqueue(std::move(event)) });
		}

		// -- Frame integration (called by ECSSystemManager / Core) --------------------

		/*********************************************************************
		 \brief  Moves a system's outbox into this bus, keeping the order the
				 system sent its events in.
		 \details Every limit applies: immediate records go through Enqueue()
				  (maxPendingEvents), delayed records beyond maxDelayedEvents
				  are dropped, and sends the outbox itself refused (full
				  outbox) are added to DroppedEvents().
		 \note   The outbox is always empty afterwards, even on a throw (the
				 unmerged rest is then dropped and counted).
		 *********************************************************************/
		void Merge(EventOutbox& outbox)
		{
			CheckThread("Merge");
			struct ClearOnExit { EventOutbox& o; ~ClearOnExit() { o.Clear(); } } clear{ outbox };

			// Report sends the outbox had to refuse because it was full.
			m_dropped += std::exchange(outbox.m_dropped, 0);

			// The only allocation for delayed records happens here, before anything is
			// committed - and only for as many as the delayed limit still allows.
			const auto delayedCount = static_cast<std::size_t>(std::count_if(
				outbox.m_records.begin(), outbox.m_records.end(),
				[](const EventOutbox::Record& r) { return r.seconds > 0.0f || r.frames > 0; }));
			const std::size_t delayedRoom = m_config.maxDelayedEvents > m_delayed.size()
				? m_config.maxDelayedEvents - m_delayed.size() : 0;
			Detail::ReserveForGrowth(m_delayed, m_delayed.size() + (std::min)(delayedCount, delayedRoom));

			std::size_t processed = 0;
			try
			{
				for (EventOutbox::Record& r : outbox.m_records)
				{
					const bool delayed = r.seconds > 0.0f || r.frames > 0;
					if (!delayed)
					{
						r.enqueue(*this);   // immediate record: Enqueue() applies maxPendingEvents
					}
					else if (m_delayed.size() >= m_config.maxDelayedEvents)
					{
						++m_dropped;        // over the delayed limit: dropped + counted
					}
					else if (r.seconds > 0.0f)
					{
						AddDelayedReserved(Delayed{ m_time + r.seconds, 0, std::move(r.enqueue) });
					}
					else
					{
						AddDelayedReserved(Delayed{ 0.0, m_frame + r.frames, std::move(r.enqueue) });
					}
					++processed;
				}
			}
			catch (...)
			{
				m_dropped += outbox.m_records.size() - processed;
				throw;
			}
		}

		/*********************************************************************
		 \brief  Delivers everything queued, in send order across ALL event
				 types. Events sent by listeners are delivered in the next
				 round, up to maxRoundsPerFlush rounds.
		 \return Number of events delivered.
		 \note   A re-entrant call from inside a listener returns 0 - the
				 outer call delivers those events in its next round.
		 *********************************************************************/
		std::size_t DeliverPending()
		{
			CheckThread("DeliverPending");
			if (m_delivering) return 0;

			m_delivering = true;
			// Whatever happens (even a throw), close every channel round we opened.
			struct Finish
			{
				EventBus& bus;
				~Finish() { bus.EndRoundOnTouched(); bus.m_delivering = false; }
			} finish{ *this };

			std::size_t delivered = 0;
			std::uint32_t round = 0;
			while (!m_order.empty())
			{
				// A listener chain (A sends B sends A ...) stops here; the rest waits a frame.
				if (round == m_config.maxRoundsPerFlush)
				{
					Log(std::string(m_config.name) + ": event chain hit maxRoundsPerFlush; rest deferred to the next flush");
					break;
				}
				++round;

				// The round's only allocation, done before anything moves: on throw, nothing changed.
				Detail::ReserveForGrowth(m_touched, m_order.size());

				// This round delivers what is queued now; anything sent during it goes to m_order again.
				m_inFlightOrder.clear();
				m_inFlightOrder.swap(m_order);   // m_order reuses the old scratch capacity
				const std::uint64_t roundId = ++m_roundCounter;

				// Open each channel's round once (moves its pending events to in-flight).
				for (IEventChannel* c : m_inFlightOrder)
					if (c->BeginRound(roundId)) m_touched.push_back(c);   // nothrow: reserved

				// Deliver one event per order entry, which replays the exact send order.
				m_roundDelivered = 0;
				try
				{
					for (IEventChannel* c : m_inFlightOrder)
					{
						Trace(round, c);
						++m_roundDelivered;
						c->DeliverNext();
					}
				}
				catch (...)
				{
					// Defined policy: the rest of THIS round is dropped and counted.
					m_dropped += m_inFlightOrder.size() - m_roundDelivered;
					m_inFlightOrder.clear();
					throw;   // Finish ends every touched channel's round
				}

				delivered += m_inFlightOrder.size();
				m_inFlightOrder.clear();
				EndRoundOnTouched();
			}

			m_deliveredTotal += delivered;
			return delivered;
		}

		/*********************************************************************
		 \brief  Advances game time/frames and queues delayed events whose
				 time or frame has come.
		 \param[in] dt Seconds since the last Tick().
		 *********************************************************************/
		void Tick(float dt)
		{
			CheckThread("Tick");
			m_time += dt;
			++m_frame;

			// Cheap early-out: nothing delayed, or nothing due yet.
			if (m_delayed.empty() || (m_time < m_nextDueTime && m_frame < m_nextDueFrame)) return;

			m_due.clear();
			Detail::ReserveForGrowth(m_due, m_delayed.size());   // only allocation; nothing moved yet

			// In-place partition keeping insertion order on both sides (nothrow moves):
			// due events go to m_due, the rest are compacted to the front of m_delayed.
			std::size_t keep = 0;
			m_nextDueTime = (std::numeric_limits<double>::infinity)();
			m_nextDueFrame = (std::numeric_limits<std::uint64_t>::max)();
			for (std::size_t i = 0; i < m_delayed.size(); ++i)
			{
				Delayed& d = m_delayed[i];
				if (IsDue(d)) { m_due.push_back(std::move(d)); continue; }
				TrackNextDue(d);
				if (keep != i) m_delayed[keep] = std::move(d);
				++keep;
			}
			m_delayed.erase(m_delayed.begin() + static_cast<std::ptrdiff_t>(keep), m_delayed.end());

			// Queue the due events (delivered at the next DeliverPending()).
			std::size_t queued = 0;
			try
			{
				for (Delayed& d : m_due) { d.enqueue(*this); ++queued; }
			}
			catch (...)
			{
				m_dropped += m_due.size() - queued;
				m_due.clear();
				throw;
			}
			m_due.clear();
		}

		/*********************************************************************
		 \brief  Discards every queued and delayed event (listeners stay).
		 *********************************************************************/
		void Clear()
		{
			CheckThread("Clear");
			for (auto& [type, c] : m_channels) c->ClearPending();
			m_order.clear();
			m_delayed.clear();
			m_nextDueTime = (std::numeric_limits<double>::infinity)();
			m_nextDueFrame = (std::numeric_limits<std::uint64_t>::max)();
		}

		// -- Diagnostics -----------------------------------------------------------------

		/*********************************************************************
		 \brief  One delivered event in the trace ring buffer.
		 *********************************************************************/
		struct TraceEntry
		{
			std::uint64_t frame;
			std::uint32_t round;
			const char* type;
		};
		static constexpr std::size_t kTraceCapacity = 256;

		const char* Name() const noexcept { return m_config.name; }
		std::uint64_t ListenerFailures() const noexcept { return m_listenerFailures; }
		std::uint64_t DeliveredTotal() const noexcept { return m_deliveredTotal; }

		/*********************************************************************
		 \brief  Events dropped by a full queue, a full delayed list, a full
				 outbox (reported at merge) or a failed merge/round.
				 Non-zero is a bug signal.
		 *********************************************************************/
		std::uint64_t DroppedEvents() const noexcept { return m_dropped; }

		std::size_t PendingCount() const noexcept { return m_order.size(); }
		std::size_t DelayedCount() const noexcept { return m_delayed.size(); }

		/*********************************************************************
		 \brief  Where failure messages go (listener threw, round cap hit).
		 *********************************************************************/
		void SetFailureLogger(std::function<void(const std::string&)> log) { CheckThread("SetFailureLogger"); m_log = std::move(log); }

		/*********************************************************************
		 \brief  Off-thread Subscription releases across all channels.
				 Non-zero is a bug signal (release handles on the main thread).
		 *********************************************************************/
		std::uint64_t OffThreadUnsubscribes() const
		{
			CheckThread("OffThreadUnsubscribes");
			std::uint64_t total = 0;
			for (const auto& [type, c] : m_channels) total += c->OffThreadUnsubscribes();
			return total;
		}

		/*********************************************************************
		 \brief  Calls fn(typeName, listenerCount) for every channel.
		 *********************************************************************/
		template <typename Fn>
		void ForEachChannel(Fn&& fn) const
		{
			CheckThread("ForEachChannel");
			for (const auto& [type, channel] : m_channels) fn(channel->TypeName(), channel->ListenerCount());
		}

		/*********************************************************************
		 \brief  Turns the delivery trace on or off (clears it either way).
		 *********************************************************************/
		void SetTraceEnabled(bool on) { CheckThread("SetTraceEnabled"); m_traceEnabled = on; m_traceCount = 0; m_traceHead = 0; }

		/*********************************************************************
		 \brief  Calls fn(const TraceEntry&) for each traced delivery, oldest
				 first. Fixed ring buffer: never allocates.
		 *********************************************************************/
		template <typename Fn>
		void ForEachTrace(Fn&& fn) const
		{
			CheckThread("ForEachTrace");
			const std::size_t start = (m_traceHead + kTraceCapacity - m_traceCount) % kTraceCapacity;
			for (std::size_t i = 0; i < m_traceCount; ++i) fn(m_trace[(start + i) % kTraceCapacity]);
		}

	private:
		struct Delayed
		{
			double fireTime;           // used when fireFrame == 0
			std::uint64_t fireFrame;   // 0 = time-based
			std::function<void(EventBus&)> enqueue;
		};
		static_assert(std::is_nothrow_move_constructible_v<Delayed> && std::is_nothrow_move_assignable_v<Delayed>,
			"Tick() partitions in place and relies on nothrow moves");

		template <typename T>
		static std::function<void(EventBus&)> MakeEnqueue(T event)
		{
			return [e = std::move(event)](EventBus& bus) mutable { bus.Enqueue<T>(std::move(e)); };
		}

		bool IsDue(const Delayed& d) const noexcept
		{
			return d.fireFrame > 0 ? m_frame >= d.fireFrame : m_time >= d.fireTime;
		}

		// Remembers the earliest due time/frame so Tick() can skip scanning.
		void TrackNextDue(const Delayed& d) noexcept
		{
			if (d.fireFrame > 0) m_nextDueFrame = (std::min)(m_nextDueFrame, d.fireFrame);
			else                 m_nextDueTime = (std::min)(m_nextDueTime, d.fireTime);
		}

		// Bounded and amortised O(1): returns false (dropped + counted) at the limit.
		bool AddDelayed(Delayed&& d)
		{
			if (m_delayed.size() >= m_config.maxDelayedEvents)
			{
				++m_dropped;
				return false;
			}
			Detail::ReserveForGrowth(m_delayed, m_delayed.size() + 1);   // may throw: nothing changed yet
			AddDelayedReserved(std::move(d));
			return true;
		}

		void AddDelayedReserved(Delayed&& d) noexcept
		{
			TrackNextDue(d);
			m_delayed.push_back(std::move(d));   // capacity reserved + nothrow move
		}

		void EndRoundOnTouched() noexcept
		{
			for (IEventChannel* c : m_touched) c->EndRound();
			m_touched.clear();
		}

		void Trace(std::uint32_t round, const IEventChannel* c) noexcept
		{
			if (!m_traceEnabled) return;
			m_trace[m_traceHead] = TraceEntry{ m_frame, round, c->TypeName() };
			m_traceHead = (m_traceHead + 1) % kTraceCapacity;
			m_traceCount = (std::min)(m_traceCount + 1, kTraceCapacity);
		}

		// Finds T's channel, creating it on first use.
		template <typename T>
		std::shared_ptr<EventChannel<T>> GetOrCreate()
		{
			static_assert(std::is_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>,
				"events must be copyable, nothrow-movable plain data");
			const std::type_index key(typeid(T));
			auto it = m_channels.find(key);
			if (it == m_channels.end())
			{
				auto channel = std::make_shared<EventChannel<T>>(m_owner,
					[this](const char* type, const char* what) { OnListenerFailure(type, what); });
				it = m_channels.emplace(key, std::move(channel)).first;
			}
			return std::static_pointer_cast<EventChannel<T>>(it->second);
		}

		// Turns a silent data race into an immediate, obvious error.
		void CheckThread(const char* what) const
		{
			if (std::this_thread::get_id() != m_owner)
				throw std::logic_error(std::string(m_config.name) + "::" + what +
					" called off the owner thread - systems must use their EventOutbox");
		}

		void OnListenerFailure(const char* type, const char* what) noexcept
		{
			++m_listenerFailures;
			try { Log(std::string(m_config.name) + ": listener for " + type + " threw - " + what); }
			catch (...) {}
		}

		void Log(const std::string& message) noexcept
		{
			try { if (m_log) m_log(message); }
			catch (...) {}
		}

		EventBusConfig m_config;
		std::thread::id m_owner;
		std::unordered_map<std::type_index, std::shared_ptr<IEventChannel>> m_channels;

		std::vector<IEventChannel*> m_order;           // global send order of queued events
		std::vector<IEventChannel*> m_inFlightOrder;   // order being delivered this round (reused)
		std::vector<IEventChannel*> m_touched;         // channels with an open round (reused)
		std::vector<Delayed> m_delayed;
		std::vector<Delayed> m_due;                    // Tick scratch (reused)

		std::function<void(const std::string&)> m_log;
		ListenerId m_nextListenerId = 0;
		std::uint64_t m_roundCounter = 0;
		std::size_t m_roundDelivered = 0;
		std::uint64_t m_frame = 0;
		double m_time = 0.0;
		double m_nextDueTime = (std::numeric_limits<double>::infinity)();
		std::uint64_t m_nextDueFrame = (std::numeric_limits<std::uint64_t>::max)();

		std::uint64_t m_listenerFailures = 0;
		std::uint64_t m_deliveredTotal = 0;
		std::uint64_t m_dropped = 0;
		std::uint32_t m_immediateDepth = 0;
		bool m_delivering = false;

		bool m_traceEnabled = false;
		std::array<TraceEntry, kTraceCapacity> m_trace{};
		std::size_t m_traceHead = 0;
		std::size_t m_traceCount = 0;
	};

	// EventOutbox template bodies need the complete EventBus.
	template <typename T>
	bool EventOutbox::Add(float seconds, std::uint32_t frames, T&& event)
	{
		// Bounded: a runaway system drops (and counts) instead of growing without limit.
		if (m_records.size() >= m_maxRecords)
		{
			++m_dropped;
			return false;
		}
		// push_back grows geometrically, so recording stays amortised O(1).
		m_records.push_back({ seconds, frames, [e = std::move(event)](EventBus& bus) mutable { bus.Enqueue<T>(std::move(e)); } });
		return true;
	}

	template <typename T>
	bool EventOutbox::Send(T event)
	{
		return Add<T>(0.0f, 0u, std::move(event));
	}

	template <typename T>
	bool EventOutbox::SendAfterSeconds(T event, float seconds)
	{
		return Add<T>(seconds, 0u, std::move(event));
	}

	template <typename T>
	bool EventOutbox::SendAfterFrames(T event, std::uint32_t frames)
	{
		return Add<T>(0.0f, frames, std::move(event));
	}
}
