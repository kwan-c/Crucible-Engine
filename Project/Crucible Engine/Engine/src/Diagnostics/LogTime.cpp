/*************************************************************************
* \file:         LogTime.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          12 September 2026
* \version       1.1
* \brief
*                LocalTimestamp and SecondsSince.
*
* \details
*                The date comes from the C runtime's localtime, not
*                std::chrono::current_zone. On MSVC current_zone loads the
*                whole timezone database into a cache that is never freed,
*                and the CRT leak check reports it as thousands of leaks.
*
* \history
*    -v1.0       first version
*    -v1.1       localtime instead of current_zone, to stop the false leak
*                reports
*
**************************************************************************/
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
