/*************************************************************************
* \file:         SnapshotAckWindow.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          6 November 2026
* \version       1.0
* \brief
*                Client: which snapshots were applied, the way an Input
*                message reports them - the newest sequence, and bit i for
*                (newest - 1 - i), 32 back (Networking Guide 6.2).
*
* \details
*                The client's NetworkSession owns one (SnapshotAcks()):
*                NetReceiveSystem marks each snapshot it applied, and
*                NetInputSystem reports the window in every Input message.
*                The session resets it on every Start and Stop, so acks
*                never name another session's snapshots.
*
*                Sequence 0 is never sent, so Latest() == 0 means "none
*                yet". A sequence more than 32 behind the newest is too old
*                to report and is ignored; one far ahead (a gap of 33 or
*                more) starts a fresh window.
*
* \history
*    -v1.0       moved out of NetReceiveSystem.h (it was there since PR 9)
*                into the session, which both network systems share (PR 11)
*
**************************************************************************/
#pragma once

#include <cstdint>

#include "Network/Protocol.h"   // SequenceGreater

namespace Crucible
{
	class SnapshotAckWindow
	{
	public:
		void Reset() noexcept { m_newest = 0; m_bits = 0; }

		void Received(std::uint16_t sequence) noexcept
		{
			if (sequence == 0) return;
			if (m_newest == 0) { m_newest = sequence; m_bits = 0; return; }
			if (SequenceGreater(sequence, m_newest))
			{
				const std::uint16_t shift = static_cast<std::uint16_t>(sequence - m_newest);
				// The old newest becomes bit shift-1; everything older moves up by shift (and out).
				if (shift > 32) m_bits = 0;
				else if (shift == 32) m_bits = 1u << 31;
				else m_bits = (m_bits << shift) | (1u << (shift - 1));
				m_newest = sequence;
				return;
			}
			const std::uint16_t back = static_cast<std::uint16_t>(m_newest - sequence);
			if (back >= 1 && back <= 32) m_bits |= 1u << (back - 1);   // a late arrival: merged
		}

		std::uint16_t Latest() const noexcept { return m_newest; }
		std::uint32_t Bits() const noexcept { return m_bits; }

	private:
		std::uint16_t m_newest = 0;
		std::uint32_t m_bits = 0;
	};
}
