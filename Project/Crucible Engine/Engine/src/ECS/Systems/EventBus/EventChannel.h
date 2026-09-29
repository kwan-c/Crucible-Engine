/*************************************************************************
* \file:         EventChannel.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.1
* \brief
*                Declares EventChannel<T>: every listener and every queued
*                event for ONE event type.
*
* \details
*                Each event type gets its own channel, so sending an
*                EnemyKilled only ever touches EnemyKilled's listeners -
*                nothing is broadcast to everyone.
*
*                How the pieces fit together:
*                  - Listeners are kept sorted by priority (lower first);
*                    equal priorities keep subscription order.
*                  - Listeners subscribed while the channel is dispatching go
*                    to a side list (m_added) and are merged afterwards, so
*                    the list being looped over is never resized.
*                  - Queued events use a double buffer: m_pending collects
*                    new events, m_inFlight holds the ones being delivered
*                    this round. Events sent BY a listener land in
*                    m_pending and wait for the next round.
*                  - A listener that throws is logged and counted; the other
*                    listeners still run (same policy as World hooks).
*                  - The listener list is guarded by a (recursive) mutex that
*                    the owner holds while dispatching. An Unsubscribe() from
*                    another thread takes the same lock, so it waits for any
*                    in-progress delivery and, once it returns, the listener
*                    is never called again - the object it captured can be
*                    destroyed right away.
*
* \history
*    -v1.0       initial per-type channel (priority listeners, deferred
*                double-buffered queue, off-thread unsubscribe queue)
*    -v1.1       off-thread Unsubscribe is synchronous (waits for in-progress
*                delivery) instead of queued, so captured objects can be
*                destroyed safely after Reset(); geometric growth for the
*                listener list
*
**************************************************************************/
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "Subscription.h"

namespace Crucible
{
	namespace Detail
	{
		/*********************************************************************
		 \brief  Makes sure `vec` can hold `needed` elements, growing
				 geometrically.
		 \details reserve(size + 1) allocates EXACTLY that on MSVC, so
				  calling it before every insert reallocates (and moves every
				  element) each time - quadratic. Doubling keeps inserts
				  amortised O(1) while still reserving before anything moves.
		 *********************************************************************/
		template <typename Vec>
		void ReserveForGrowth(Vec& vec, std::size_t needed)
		{
			if (needed <= vec.capacity()) return;
			vec.reserve((std::max)({ needed, vec.capacity() * 2, std::size_t{ 16 } }));
		}
	}

	/*************************************************************************
	 \brief  Called when a listener throws: (event type name, exception
			 text). Must not throw itself.
	 *************************************************************************/
	using ListenerFailureFn = std::function<void(const char*, const char*)>;

	/*************************************************************************
	 \brief
		Every listener and every queued event of ONE event type.

	 \tparam T
		The event type: a plain, copyable struct with a non-throwing move.

	 \details
		Listeners run in (priority, subscription order).

		Exception safety:
		  - Subscribe() / Enqueue(): strong guarantee (nothing changes on
			throw).
		  - Dispatch(): listener exceptions are caught per listener. The only
			other possible throw is bad_alloc while merging listeners added
			mid-dispatch; they then stay in m_added and are merged later, so
			the channel is always consistent.
		  - Everything else is noexcept.

		Threading: owner thread only, except Unsubscribe(), which may be
		called from any thread. Off-thread it blocks until any delivery in
		progress on this channel has finished; afterwards the listener is
		never called again.

	 \warning
		Because of that wait, a listener must never block on a thread that
		might be releasing a Subscription of the same channel (deadlock).

	 \note
		Normally used through EventBus, not directly.
	 *************************************************************************/
	template <typename T>
	class EventChannel final : public IEventChannel
	{
		static_assert(std::is_nothrow_move_constructible_v<T>,
			"events must be nothrow-movable plain data (queues rely on it)");

	public:
		using Handler = std::function<void(const T&)>;

		/*********************************************************************
		 \brief  Creates an empty channel.
		 \param[in] owner     The only thread allowed to use the channel
							  (except for Unsubscribe()).
		 \param[in] onFailure Called when a listener throws.
		 *********************************************************************/
		EventChannel(std::thread::id owner, ListenerFailureFn onFailure)
			: m_owner(owner), m_onFailure(std::move(onFailure)) {}

		/*********************************************************************
		 \brief  Adds a listener.
		 \param[in] handler  Function called with each delivered event.
		 \param[in] priority Lower runs first; equal keeps subscription order.
		 \param[in] id       The listener's id (assigned by the bus).
		 \note   Strong guarantee. Called during dispatch, the listener is
				 parked in m_added and first runs for the NEXT event.
		 *********************************************************************/
		void Subscribe(Handler handler, int priority, ListenerId id)
		{
			std::lock_guard lock(m_listenersMutex);
			Listener listener{ id, priority, std::move(handler), true };

			if (m_dispatchDepth > 0)
			{
				// Can't touch m_listeners while it is being looped over.
				m_added.push_back(std::move(listener));
				return;
			}

			MergeAdded();                                                  // leftovers from an earlier failed merge
			Detail::ReserveForGrowth(m_listeners, m_listeners.size() + 1);
			Insert(std::move(listener));                                   // capacity reserved: cannot throw now
		}

		/*********************************************************************
		 \brief  Removes a listener (see IEventChannel::Unsubscribe).
		 \details Off the owner thread this waits for any delivery in progress
				  on this channel (it needs the same lock), so when it returns
				  the listener is neither running nor ever called again.
		 *********************************************************************/
		void Unsubscribe(ListenerId id) noexcept override
		{
			const bool offThread = std::this_thread::get_id() != m_owner;
			if (offThread) m_offThreadCount.fetch_add(1, std::memory_order_relaxed);

			std::lock_guard lock(m_listenersMutex);   // off-thread: blocks until the owner's dispatch finishes
			MarkDead(id);

			// Physically remove only when nobody is looping over the list.
			if (m_dispatchDepth == 0) Compact();
		}

		/*********************************************************************
		 \brief  Immediately delivers one event to every live listener.
		 \param[in] event The event to deliver.
		 \note   Listener exceptions are caught, reported and counted; they
				 never stop the remaining listeners.
		 *********************************************************************/
		void Dispatch(const T& event)
		{
			// Held for the whole loop: an off-thread Unsubscribe waits until we are done.
			// Recursive, so listeners may Subscribe/Unsubscribe/Dispatch on this channel.
			std::lock_guard lock(m_listenersMutex);
			++m_dispatchDepth;

			// Index loop over a size snapshot: m_listeners is never resized during
			// dispatch (new listeners go to m_added), so every index stays valid.
			const std::size_t count = m_listeners.size();
			for (std::size_t i = 0; i < count; ++i)
			{
				if (!m_listeners[i].alive) continue;   // unsubscribed earlier (possibly this dispatch)
				try { m_listeners[i].handler(event); }
				catch (const std::exception& e) { Report(e.what()); }
				catch (...) { Report("non-std::exception"); }
			}

			// Nothing above can throw, so the depth is always restored here.
			if (--m_dispatchDepth == 0)
			{
				Compact();
				MergeAdded();   // may throw bad_alloc; m_added then simply waits for the next merge
			}
		}

		// -- Deferred queue (owner thread only; other threads never touch these) --

		/*********************************************************************
		 \brief  Queues an event for the next delivery round.
		 \note   Strong guarantee. Normally called only by EventBus, which
				 also records the event's position in the global send order.
		 *********************************************************************/
		void Enqueue(T&& event) { m_pending.push_back(std::move(event)); }

		/*********************************************************************
		 \brief  Starts a delivery round (see IEventChannel::BeginRound).
		 *********************************************************************/
		bool BeginRound(std::uint64_t roundId) noexcept override
		{
			if (m_roundId == roundId) return false;   // already started this round
			m_roundId = roundId;

			// Swap buffers: everything queued so far is delivered this round;
			// events queued during delivery go into the (now empty) pending list.
			m_inFlight.clear();
			m_inFlight.swap(m_pending);
			m_cursor = 0;
			return true;
		}

		/*********************************************************************
		 \brief  Delivers the next in-flight event.
		 *********************************************************************/
		void DeliverNext() override
		{
			const std::size_t index = m_cursor++;   // advance first: a throw never re-delivers
			Dispatch(m_inFlight[index]);
		}

		/*********************************************************************
		 \brief  Ends the round (see IEventChannel::EndRound).
		 *********************************************************************/
		void EndRound() noexcept override
		{
			m_inFlight.clear();   // keeps capacity for the next round
			m_cursor = 0;
		}

		/*********************************************************************
		 \brief  Discards every queued event.
		 *********************************************************************/
		void ClearPending() noexcept override { m_pending.clear(); }

		/*********************************************************************
		 \brief  Live listeners, including ones parked in m_added.
		 *********************************************************************/
		std::size_t ListenerCount() const noexcept override
		{
			std::lock_guard lock(m_listenersMutex);
			std::size_t n = 0;
			for (const Listener& l : m_listeners) n += l.alive ? 1 : 0;
			for (const Listener& l : m_added)     n += l.alive ? 1 : 0;
			return n;
		}

		std::uint64_t OffThreadUnsubscribes() const noexcept override { return m_offThreadCount.load(std::memory_order_relaxed); }

		const char* TypeName() const noexcept override { return typeid(T).name(); }

	private:
		struct Listener
		{
			ListenerId id;
			int priority;
			Handler handler;
			bool alive;   // false = unsubscribed, waiting for Compact()
		};
		static_assert(std::is_nothrow_move_constructible_v<Listener> && std::is_nothrow_move_assignable_v<Listener>,
			"Listener moves must not throw (Insert/Compact rely on it)");

		// Inserts in priority order. Requires capacity for one more element: then nothrow.
		void Insert(Listener&& listener) noexcept
		{
			// upper_bound puts the new listener AFTER existing ones of equal priority.
			auto at = std::upper_bound(m_listeners.begin(), m_listeners.end(), listener.priority,
				[](int p, const Listener& l) { return p < l.priority; });
			m_listeners.insert(at, std::move(listener));
		}

		// Strong guarantee: the reserve (the only allocation) happens before anything moves.
		void MergeAdded()
		{
			if (m_added.empty()) return;
			Detail::ReserveForGrowth(m_listeners, m_listeners.size() + m_added.size());
			for (Listener& l : m_added)
				if (l.alive) Insert(std::move(l));
			m_added.clear();
		}

		void MarkDead(ListenerId id) noexcept
		{
			for (Listener& l : m_listeners) if (l.id == id) { l.alive = false; m_needsCompact = true; }
			for (Listener& l : m_added)     if (l.id == id) { l.alive = false; }
		}

		// Physically removes dead listeners (never while dispatching).
		void Compact() noexcept
		{
			if (!m_needsCompact) return;
			std::erase_if(m_listeners, [](const Listener& l) { return !l.alive; });
			m_needsCompact = false;
		}

		void Report(const char* what) noexcept
		{
			try { if (m_onFailure) m_onFailure(TypeName(), what); }
			catch (...) {}   // reporting must never turn into a new failure
		}

		std::thread::id m_owner;
		ListenerFailureFn m_onFailure;

		// Guards m_listeners, m_added, m_dispatchDepth and m_needsCompact.
		// Recursive: listeners may call back into this channel on the owner thread.
		mutable std::recursive_mutex m_listenersMutex;
		std::vector<Listener> m_listeners;   // sorted by priority
		std::vector<Listener> m_added;       // subscribed during dispatch, merged afterwards
		int m_dispatchDepth = 0;             // > 0 while Dispatch() is looping (can nest)
		bool m_needsCompact = false;

		std::vector<T> m_pending;            // queued for the next delivery round
		std::vector<T> m_inFlight;           // being delivered this round
		std::size_t m_cursor = 0;            // next in-flight event to deliver
		std::uint64_t m_roundId = 0;

		std::atomic<std::uint64_t> m_offThreadCount{ 0 };
	};
}
