#pragma once

// Included at the bottom of Logger.h. Do not include this file directly.

#include <string>

namespace Crucible::Debug
{
    // Convenience version of LogSetting that takes any value std::format can
    // print, so callers do not have to convert to text themselves:
    //
    //     CRU_CONFIG("Window", "width", 1200);
    //     CRU_CONFIG("Window", "vsync", true);
    //
    // The formatted text is kept in a named local rather than passed straight
    // through, so there is no question of the string_view outliving it.

    template <typename T>
    void LogSetting(const std::string_view& system,
                    const std::string_view& key,
                    const T& value)
    {
        const std::string text = std::format("{}", value);
        LogSetting(system, key, std::string_view{ text });
    }
}
