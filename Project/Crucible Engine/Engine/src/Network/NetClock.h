/*************************************************************************
* \file:         NetClock.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The network tick: the server's count of fixed simulation
*                steps, and when a snapshot or an input packet is due.
*
* \details
*                The tick IS the physics step (60 Hz, ECSPhysicsSystem's
*                fixed step), not a second clock beside it: two clocks with
*                their own accumulators drift apart, and later client-side
*                prediction needs "input N was applied at step S" to name
*                an exact simulation step.
*
*                Server: AdvanceSimulation(stepsThisFrame) after physics;
*                SnapshotDue() is true at most once per frame, once at least
*                kSnapshotInterval steps passed, so a hitch of several steps
*                still sends one snapshot, not a burst.
*                Client: InputDue(dt) paces input packets at kInputRate from
*                frame time, catching up at most one packet after a hitch.
*
*                Phase 4 creates the clock and sends its tick and rate in
*                JoinAccepted; the replication send system (Phase 9) is what
*                advances it every frame.
*
* \history
*    -v1.0       NetClock: the fixed-step network tick
*
**************************************************************************/
#pragma once

#include <algorithm>
#include <cstdint>

#include "Network/NetTypes.h"

namespace Crucible
{
	class NetClock
	{
	public:
		static constexpr std::uint32_t kTickRate         = 60;     // steps per second = ECSPhysicsSystem's fixed step
		static constexpr std::uint32_t kSnapshotInterval = 2;      // a snapshot every 2 steps: 30 Hz
		static constexpr float         kInputRate        = 30.0f;  // client input packets per second

		void Reset() noexcept { m_tick = 0; m_lastSnapshot = 0; m_inputAccumulator = 0.f; }

		/// Server, once per frame after physics: how many fixed steps physics ran (0 while paused).
		void AdvanceSimulation(std::uint32_t steps) noexcept { m_tick += steps; }

		/// Server: true at most once per frame, when at least kSnapshotInterval steps passed.
		bool SnapshotDue() noexcept
		{
			if (m_tick - m_lastSnapshot < kSnapshotInterval) return false;
			m_lastSnapshot = m_tick;
			return true;
		}

		/// Client: true when an input packet is due. After a long frame, at most one is owed.
		bool InputDue(float dt) noexcept
		{
			constexpr float kSeconds = 1.0f / kInputRate;
			m_inputAccumulator = std::min(m_inputAccumulator + std::max(dt, 0.0f), 2.0f * kSeconds);
			if (m_inputAccumulator < kSeconds) return false;
			m_inputAccumulator -= kSeconds;
			return true;
		}

		/// Server: the current simulation step.
		Net::NetTick Tick() const noexcept { return m_tick; }

	private:
		Net::NetTick m_tick = 0;
		Net::NetTick m_lastSnapshot = 0;
		float        m_inputAccumulator = 0.f;
	};
}
