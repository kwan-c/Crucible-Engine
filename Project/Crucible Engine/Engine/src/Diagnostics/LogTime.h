/*************************************************************************
* \file:         LogTime.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          12 September 2026
* \version       1.0
* \brief
*                Time helpers for the logger: today's date and time, and
*                the seconds since the session started.
*
* \details
*                Two clocks on purpose: system_clock for the date, which
*                can jump, and steady_clock for durations, which never
*                goes backwards. Engine-internal: only Logger.cpp uses
*                these.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

#include <chrono>
#include <string>

// Engine-internal helpers. Not exported: only Logger.cpp uses these.
//
// Two different clocks, on purpose:
//   system_clock - "what time is it?"   Printable as a date, but it can
//                                       jump (NTP sync, DST, user changes it).
//   steady_clock - "how long has passed?" Never goes backwards, but its zero
//                                       point is arbitrary so it is not a date.
// Use the first for timestamps, the second for durations. Mixing them up is
// how you end up logging a negative runtime.

namespace Crucible::Debug
{
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    // Local wall-clock time, e.g. "2026-09-11 14:32:07".
    std::string LocalTimestamp();

    // Seconds elapsed since 'start', e.g. 12.483.
    double SecondsSince(const TimePoint& start);
}
