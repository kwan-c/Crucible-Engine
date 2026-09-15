#pragma once

#include <string_view>

namespace Crucible::Debug
{
    // Severity, lowest to highest. The order matters: filtering is a
    // simple "is this level >= the minimum we care about" comparison,
    // so the sequence itself encodes what counts as more serious.
    enum class Level
    {
        Trace,      // very chatty, safe to spam per-frame
        Debug,      // developer detail, not per-frame
        Info,       // normal lifecycle events ("window created")
        Warning,    // looks wrong, but we carried on
        Error,      // something failed, we recovered
        Fatal,      // unrecoverable, we are about to stop
        Off         // nothing passes this filter
    };

    // Padded to 5 characters so the columns line up in the log file.
    constexpr std::string_view LevelName(const Level& level)
    {
        switch (level)
        {
        case Level::Trace:   return "TRACE";
        case Level::Debug:   return "DEBUG";
        case Level::Info:    return "INFO ";
        case Level::Warning: return "WARN ";
        case Level::Error:   return "ERROR";
        case Level::Fatal:   return "FATAL";
        case Level::Off:     return "OFF  ";
        }
        return "?????";
    }
}
