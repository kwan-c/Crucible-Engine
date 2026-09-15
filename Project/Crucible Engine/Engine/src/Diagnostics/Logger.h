#pragma once

#include <filesystem>
#include <format>
#include <string_view>

#include "EngineExport.h"
#include "LogLevel.h"

// ---------------------------------------------------------------------------
// Crucible logger
//
// Typical use, from anywhere in the engine or the game:
//
//     CRU_INFO("Loaded {} assets from {}", count, path);
//     CRU_WARN("Gamepad {} disconnected", index);
//     CRU_ERROR("Could not open {}", filename);
//     CRU_FATAL("No OpenGL context - cannot continue");
//
// Set it up once, in main():
//
//     Crucible::Debug::LogSession session{};   // starts and ends the session
//
// Every line comes out looking like this:
//
//   [2026-09-11 14:32:07] [+   0.014s] [INFO ] [Core.cpp:87] Window created
//    wall clock            runtime      level    where          message
// ---------------------------------------------------------------------------

namespace Crucible::Debug
{
    struct LogConfig
    {
        bool consoleSink     = true;                 // stdout
        bool debugOutputSink = true;                 // VS Output window
        std::filesystem::path logFile = "crucible.log";   // empty = no file
        Level minLevel = Level::Trace;
        bool  showSourceLocation = true;             // the [Core.cpp:87] part
    };

    // Start and stop. Init is safe to call twice; the second call does nothing.
    ENGINE_API void Init(const LogConfig& config = LogConfig{});
    ENGINE_API void Shutdown();

    ENGINE_API void  SetMinLevel(const Level& level);
    ENGINE_API Level GetMinLevel();
    ENGINE_API bool  IsEnabled(const Level& level);

    // Push every sink's buffered text out now. Called automatically for
    // Error and Fatal, because a crash log still sitting in a buffer is
    // empty exactly when you need it most.
    ENGINE_API void Flush();

    // Seconds since Init().
    ENGINE_API double ElapsedSeconds();

    // The single real entry point. Use the CRU_* macros instead of calling
    // this directly - they fill in the file and line for you.
    ENGINE_API void LogMessage(const Level& level,
                               const char* file,
                               int line,
                               const std::string_view& message);

    // Records one setting, so a log file always says how the run was configured.
    //     LogSetting("Window", "width", "1200");
    // There is a template version below that accepts any printable value.
    ENGINE_API void LogSetting(const std::string_view& system,
                               const std::string_view& key,
                               const std::string_view& value);

    // Logs the reason, flushes, breaks into the debugger, then aborts.
    [[noreturn]] ENGINE_API void FatalExit(const std::string_view& reason,
                                           const char* file,
                                           int line);

    // RAII wrapper: construct one in main() and the session is started and
    // ended for you. The destructor runs on EVERY way out of the scope -
    // normal return, early return, thrown exception - so the "session ended"
    // line and the final flush are never skipped.
    class ENGINE_API LogSession
    {
    public:
        explicit LogSession(const LogConfig& config = LogConfig{});
        ~LogSession();

        LogSession(const LogSession&)            = delete;
        LogSession& operator=(const LogSession&) = delete;
    };
}

#include "Logger.tpp"

// ---------------------------------------------------------------------------
// Macros - this is what you actually type.
//
// They are macros rather than plain functions for two reasons:
//   1. __FILE__ and __LINE__ expand at YOUR call site, so the log line points
//      at your code instead of at the inside of the logger.
//   2. In a shipping build CRU_TRACE and CRU_DEBUG vanish completely, so
//      their arguments are never even evaluated. A function cannot do that:
//      CRU_TRACE(BuildExpensiveString(x)) would still build the string.
//
// Macros ignore namespaces entirely, which is why they are SHOUTY and
// prefixed. The leading :: means "start from the global namespace", so they
// work no matter where they are used.
// ---------------------------------------------------------------------------

#define CRU_LOG(level, ...) \
    ::Crucible::Debug::LogMessage((level), __FILE__, __LINE__, ::std::format(__VA_ARGS__))

#define CRU_INFO(...)  CRU_LOG(::Crucible::Debug::Level::Info,    __VA_ARGS__)
#define CRU_WARN(...)  CRU_LOG(::Crucible::Debug::Level::Warning, __VA_ARGS__)
#define CRU_ERROR(...) CRU_LOG(::Crucible::Debug::Level::Error,   __VA_ARGS__)

#define CRU_FATAL(...) \
    ::Crucible::Debug::FatalExit(::std::format(__VA_ARGS__), __FILE__, __LINE__)

#define CRU_CONFIG(system, key, value) \
    ::Crucible::Debug::LogSetting((system), (key), (value))

// CRUCIBLE_DIST is defined by CMake for the shipping configuration.
// Until that exists, these always compile to the real thing.
#ifdef CRUCIBLE_DIST
    #define CRU_TRACE(...) ((void)0)
    #define CRU_DEBUG(...) ((void)0)
#else
    #define CRU_TRACE(...) CRU_LOG(::Crucible::Debug::Level::Trace, __VA_ARGS__)
    #define CRU_DEBUG(...) CRU_LOG(::Crucible::Debug::Level::Debug, __VA_ARGS__)
#endif
