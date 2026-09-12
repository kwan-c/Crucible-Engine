#include "LogTime.h"

#include <array>
#include <ctime>

namespace Crucible::Debug
{
    // Safe here: this is a .cpp. Never write this at namespace scope in a
    // header - it would leak into every file that includes it.
    using namespace std::chrono;

    std::string LocalTimestamp()
    {
        // Deliberately NOT std::chrono::current_zone().
        //
        // On MSVC the first call to current_zone() loads the entire IANA
        // timezone database into a global cache that is never freed, by
        // design (microsoft/STL issue #2504). With _CRTDBG_LEAK_CHECK_DF
        // enabled that shows up as several thousand reported "leaks" -
        // every one of them a timezone name - which buries any real leak.
        //
        // The C runtime's localtime gives the same answer for far less:
        // no database, no allocation, and no exceptions to worry about.

        const std::time_t seconds =
            system_clock::to_time_t(system_clock::now());

        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &seconds);
#else
        localtime_r(&seconds, &local);
#endif

        std::array<char, 32> buffer{};
        const std::size_t written =
            std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &local);

        return std::string(buffer.data(), written);
    }

    double SecondsSince(const TimePoint& start)
    {
        return duration<double>(Clock::now() - start).count();
    }
}
