/*************************************************************************
* \file:         ServerTimeEstimate.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          6 November 2026
* \version       1.1
* \brief
*                Client: "which server tick is it now?", as a continuous
*                value that only moves forward, smoothly. Interpolation
*                renders at Estimate() - kInterpolationDelayTicks.
*
* \details
*                Samples are (snapshot tick, GNS round-trip time): a
*                snapshot left the host about RTT/2 ago, so the host's "now"
*                is its tick plus that transit time (the target). Every
*                frame both the estimate and the target advance by the
*                frame time, and the estimate is pulled towards the target
*                by at most 10% of that step, so time runs at 0.9x to 1.1x
*                speed and never visibly jumps or goes back. Only a first
*                sample, or falling more than kSnapTicks (~330 ms) behind
*                (a long stall on this side), re-syncs it in one go, and
*                only ever forwards (the newest tick is recorded on every
*                path).
*
*                Never ahead of what arrived: the estimate stops at most
*                kMaxAheadTicks past the newest snapshot (+ transit). The
*                host's tick only advances while it plays, so while it is
*                paused (or snapshots stop for any reason) the client's
*                time waits instead of running on. Without that, on resume
*                the estimate was seconds ahead and re-synced backwards,
*                and interpolation drew already-drawn positions again. The
*                ceiling equals the interpolation delay, so drawing is at
*                most the transit time past the newest sample: a hold.
*                Being ahead by less than that is corrected by the 0.9x
*                slew, so drift either way is still recovered.
*
*                Late or duplicate snapshots (tick not newer) are ignored,
*                so reordering never pulls time back. A frame longer than
*                250 ms counts as 250 ms. NetTick is 32-bit at 60 Hz (about
*                2.3 years): no wrap handling.
*
*                The client's NetworkSession owns one (ServerTime()), reset
*                by every Start and Stop; NetReceiveSystem feeds it,
*                NetInterpolationSystem advances and reads it.
*
* \history
*    -v1.0       smoothed server time estimate
*    -v1.1       review fix: never ahead of the newest snapshot by more
*                than kMaxAheadTicks; re-sync forwards only (host pause and
*                resume no longer replay positions)
*
**************************************************************************/
#pragma once

#include <algorithm>
#include <cmath>

#include "Network/NetClock.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	class ServerTimeEstimate
	{
	public:
		static constexpr double kSnapTicks = 10.0;     // ~330 ms behind: re-sync instead of catching up
		static constexpr double kMaxAheadTicks = 6.0;  // = the interpolation delay (100 ms)
		static constexpr float  kMaxFrameSeconds = 0.25f;

		void Reset() noexcept { m_valid = false; m_estimate = 0.0; m_target = 0.0; m_transit = 0.0; m_newest = 0; m_resyncs = 0; }

		/// A snapshot for `tick` was applied; `rttMs` is GNS's ping to the host (0 if unknown).
		void OnSnapshot(Net::NetTick tick, int rttMs) noexcept
		{
			if (m_valid && tick <= m_newest) return;   // late or duplicate: not newer
			m_newest = tick;
			m_transit = static_cast<double>(std::max(0, rttMs)) * 0.0005 * NetClock::kTickRate;   // RTT/2 in ticks
			m_target = static_cast<double>(tick) + m_transit;
			if (!m_valid || m_target - m_estimate > kSnapTicks)
			{
				if (m_valid) ++m_resyncs;
				m_estimate = m_target;   // first sample, or far behind: jump forwards
				m_valid = true;
			}
			// Ahead of the target (the host paused, or this clock ran fast): never jump back; the
			// ceiling in Advance and the 0.9x slew bring it in without drawing anything twice.
		}

		/// Once per frame, before Estimate() is read.
		void Advance(float dt) noexcept
		{
			if (!m_valid) return;
			const double before = m_estimate;
			const double step = static_cast<double>(std::clamp(dt, 0.f, kMaxFrameSeconds)) * NetClock::kTickRate;
			m_estimate += step;
			m_target += step;   // the host kept running too
			const double error = m_target - m_estimate;
			m_estimate += std::clamp(error * 0.1, -0.1 * step, 0.1 * step);

			// Never past what arrived (a paused host sends nothing), and never back.
			const double ceiling = static_cast<double>(m_newest) + m_transit + kMaxAheadTicks;
			m_target = std::min(m_target, ceiling);
			m_estimate = std::max(before, std::min(m_estimate, ceiling));
		}

		bool          Valid() const noexcept    { return m_valid; }
		double        Estimate() const noexcept { return m_estimate; }   // fractional server tick, now
		double        Target() const noexcept   { return m_target; }
		Net::NetTick  Newest() const noexcept   { return m_newest; }
		std::uint32_t Resyncs() const noexcept  { return m_resyncs; }    // jumps after the first sample

	private:
		double        m_estimate = 0.0;   // what interpolation uses
		double        m_target = 0.0;     // newest tick + transit, advanced each frame
		double        m_transit = 0.0;    // RTT/2 of the newest snapshot, in ticks
		Net::NetTick  m_newest = 0;
		bool          m_valid = false;
		std::uint32_t m_resyncs = 0;
	};
}
