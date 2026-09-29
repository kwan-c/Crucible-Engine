/*************************************************************************
* \file:         Subscription.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.1
* \brief
*                Declares IEventChannel (the type-erased face of one event
*                channel) and Subscription (the RAII handle that keeps one
*                listener registered for exactly as long as it lives).
*
* \details
*                Why a handle: a listener is usually a lambda that captures
*                `this`. If the object that registered it is destroyed while
*                the listener is still registered, the next event would call
*                into freed memory. Subscription removes the listener in its
*                destructor, so a listener can never outlive its owner.
*
*                The handle holds a weak_ptr to the channel, so it is also
*                safe when the bus is destroyed first - releasing it then is
*                simply a no-op.
*
*                Threading: Reset() is the ONE event-system call allowed off
*                the bus's owner (main) thread, because handles can be
*                destroyed anywhere. Off the owner thread it waits for any
*                delivery in progress on that channel (see EventChannel.h);
*                once it returns, the listener is not running and will never
*                be called again, so the captured object can be destroyed.
*
*                Header-only: the Game module instantiates everything for its
*                own event types without anything being exported from the
*                Engine DLL.
*
* \history
*    -v1.0       initial RAII subscription handle + channel interface
*    -v1.1       off-thread Reset() is synchronous: after it returns the
*                listener never runs again (was: queued, could run once more)
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace Crucible
{
	/*************************************************************************
	 \brief  Identifies one listener within a bus. 0 means "no listener".
	 *************************************************************************/
	using ListenerId = std::uint64_t;

	/*************************************************************************
	 \brief
		Type-erased base of every EventChannel<T>: everything the bus and
		Subscription need to do with a channel without knowing its event
		type T.

	 \details
		A delivery round works like this (driven by EventBus::DeliverPending):
		  1. BeginRound()  - the channel moves its queued events into an
							 "in flight" list (once per round).
		  2. DeliverNext() - called once per queued event, in global send
							 order across all channels.
		  3. EndRound()    - drops anything left in flight (after a failure)
							 and gets ready for the next round.

	 \note
		Every function except Unsubscribe() is owner-thread only; the bus
		checks the thread before calling them.
	 *************************************************************************/
	class IEventChannel
	{
	public:
		virtual ~IEventChannel() = default;

		/*********************************************************************
		 \brief  Removes a listener.
		 \param[in] id The listener to remove.
		 \note   Any thread. From another thread it waits until any delivery
				 in progress on the channel has finished. Either way, once it
				 returns the listener is never called again.
		 *********************************************************************/
		virtual void Unsubscribe(ListenerId id) noexcept = 0;

		/*********************************************************************
		 \brief  Moves queued events into the in-flight list for a round.
		 \param[in] roundId Unique id of the delivery round.
		 \return true the first time it is called for a given round id
				 (later calls in the same round do nothing).
		 *********************************************************************/
		virtual bool BeginRound(std::uint64_t roundId) noexcept = 0;

		/*********************************************************************
		 \brief  Delivers the next in-flight event to every live listener.
		 *********************************************************************/
		virtual void DeliverNext() = 0;

		/*********************************************************************
		 \brief  Ends the round, dropping any undelivered in-flight events.
		 *********************************************************************/
		virtual void EndRound() noexcept = 0;

		/*********************************************************************
		 \brief  Discards every queued (not yet in-flight) event.
		 *********************************************************************/
		virtual void ClearPending() noexcept = 0;

		/*********************************************************************
		 \brief  Number of live listeners (including ones added mid-dispatch).
		 *********************************************************************/
		virtual std::size_t ListenerCount() const noexcept = 0;

		/*********************************************************************
		 \brief  How many Unsubscribe() calls arrived from another thread
				 (a diagnostic: this should normally stay 0).
		 *********************************************************************/
		virtual std::uint64_t OffThreadUnsubscribes() const noexcept = 0;

		/*********************************************************************
		 \brief  Readable name of the channel's event type (for logs/trace).
		 *********************************************************************/
		virtual const char* TypeName() const noexcept = 0;
	};

	/*************************************************************************
	 \brief
		RAII handle to one listener. Destroying or Reset()-ing it
		unsubscribes the listener.

	 \details
		Keep it as a member of whatever owns the listener (usually a
		system), and Reset() it in that owner's Shutdown(). Move-only: one
		listener has exactly one owner.

	 \note
		Releasing on the bus's owner (main) thread is the normal case.
		Releasing on another thread is also safe: Reset() then waits for any
		delivery in progress on that channel, and after it returns the
		listener never runs again - so the object the listener captured can
		be destroyed immediately afterwards.

	 \warning
		Don't release a handle from a thread that the running listener is
		itself waiting on - the two would wait for each other (deadlock).
	 *************************************************************************/
	class [[nodiscard]] Subscription
	{
	public:
		/*********************************************************************
		 \brief  An empty handle (not subscribed to anything).
		 *********************************************************************/
		Subscription() = default;

		/*********************************************************************
		 \brief  Wraps a listener that was just registered on a channel.
		 \param[in] channel The channel the listener lives in.
		 \param[in] id      The listener's id within that channel's bus.
		 *********************************************************************/
		Subscription(std::weak_ptr<IEventChannel> channel, ListenerId id) noexcept
			: m_channel(std::move(channel)), m_id(id) {}

		/*********************************************************************
		 \brief  Unsubscribes the listener (if still subscribed).
		 *********************************************************************/
		~Subscription() { Reset(); }

		Subscription(const Subscription&) = delete;
		Subscription& operator=(const Subscription&) = delete;

		/*********************************************************************
		 \brief  Takes over other's listener; other becomes empty.
		 *********************************************************************/
		Subscription(Subscription&& other) noexcept
			: m_channel(std::move(other.m_channel)), m_id(std::exchange(other.m_id, 0)) {}

		/*********************************************************************
		 \brief  Releases this handle's listener, then takes over other's.
		 *********************************************************************/
		Subscription& operator=(Subscription&& other) noexcept
		{
			if (this != &other)
			{
				Reset();
				m_channel = std::move(other.m_channel);
				m_id = std::exchange(other.m_id, 0);
			}
			return *this;
		}

		/*********************************************************************
		 \brief  Unsubscribes now. Safe to call repeatedly, and safe after
				 the bus has been destroyed (then it does nothing).
		 \note   When it returns, the listener is not running and will never
				 be called again (from another thread it may briefly wait).
		 *********************************************************************/
		void Reset() noexcept
		{
			if (m_id == 0) return;

			// lock() fails if the bus (and so the channel) is already gone.
			if (auto channel = m_channel.lock()) channel->Unsubscribe(m_id);
			m_channel.reset();
			m_id = 0;
		}

		/*********************************************************************
		 \brief  True while the listener is registered and its bus exists.
		 *********************************************************************/
		bool IsActive() const noexcept { return m_id != 0 && !m_channel.expired(); }

	private:
		std::weak_ptr<IEventChannel> m_channel;   // weak: the bus owns channels, not handles
		ListenerId m_id = 0;
	};
}
