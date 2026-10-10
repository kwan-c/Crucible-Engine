/*************************************************************************
* \file:         ScopedTimer.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          12 September 2026
* \version       1.1
* \brief
*                ScopedTimer's constructor and destructor.
*
* \details
*                The destructor measures the time since construction and
*                logs it at the timer's level.
*
* \history
*    -v1.0       first version
*    -v1.1       start time kept as a tick count, so no std::chrono member
*                crosses the DLL boundary (C4251)
*
**************************************************************************/
#include "ScopedTimer.h"

#include "Logger.h"

namespace Crucible::Debug
{
    ScopedTimer::ScopedTimer(const char* label, const Level& level)
        : m_label(label)
        , m_level(level)
        , m_startTicks(std::chrono::steady_clock::now().time_since_epoch().count())
    {
        // Member initialisation order must match the declaration order in the
        // header, or /W4 warns (C5038). It does.
    }

    ScopedTimer::~ScopedTimer()
    {
        const auto elapsed = std::chrono::steady_clock::now().time_since_epoch().count() - m_startTicks;
        const double ms = std::chrono::duration<double, std::milli>(elapsed).count();

        LogMessage(m_level, nullptr, 0,
                   std::format("{} took {:.3f} ms", m_label, ms));
    }
}
