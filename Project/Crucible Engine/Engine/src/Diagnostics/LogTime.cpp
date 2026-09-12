#include "LogTime.h"

#include <format>

namespace Crucible::Debug
{
    // Safe here: this is a .cpp. Never write this at namespace scope in a
    // header - it would leak into every file that includes it.
    using namespace std::chrono;

    std::string LocalTimestamp()
    {
        const auto nowUtc = floor<seconds>(system_clock::now());

        try
        {
            return std::format("{:%Y-%m-%d %H:%M:%S}",
                               zoned_time{ current_zone(), nowUtc });
        }
        catch (const std::exception&)
        {
            // Some machines have no timezone database. Fall back to UTC
            // rather than losing the timestamp entirely.
            return std::format("{:%Y-%m-%d %H:%M:%S}Z", nowUtc);
        }
    }

    double SecondsSince(const TimePoint& start)
    {
        return duration<double>(Clock::now() - start).count();
    }
}
