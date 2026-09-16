#include "ScopedTimer.h"

#include "Logger.h"

namespace Crucible::Debug
{
    ScopedTimer::ScopedTimer(const char* label, const Level& level)
        : m_label(label)
        , m_level(level)
        , m_start(std::chrono::steady_clock::now())
    {
        // Member initialisation order must match the declaration order in the
        // header, or /W4 warns (C5038). It does.
    }

    ScopedTimer::~ScopedTimer()
    {
        const auto elapsed = std::chrono::steady_clock::now() - m_start;
        const double ms = std::chrono::duration<double, std::milli>(elapsed).count();

        LogMessage(m_level, nullptr, 0,
                   std::format("{} took {:.3f} ms", m_label, ms));
    }
}
