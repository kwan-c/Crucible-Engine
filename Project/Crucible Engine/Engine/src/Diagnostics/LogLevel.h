/*************************************************************************
* \file:         LogLevel.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          12 September 2026
* \version       1.0
* \brief
*                The log severity levels, Trace up to Fatal plus Off, and
*                the name printed for each.
*
* \details
*                The order of the enum is the filter: a message is written
*                when its level is at or above the logger's minimum.
*                LevelName pads every name to five characters so the
*                columns in the log line up.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
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
