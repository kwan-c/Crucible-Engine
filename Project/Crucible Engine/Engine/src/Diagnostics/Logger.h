/*************************************************************************
* \file:         Logger.h
* \author:       Hugo Low Ren Hao (main author),
*                Tan Chun Kwang Xavier (v1.1: listeners, user-facing messages)
* \par:          Email: low.h@digipen.edu,
*                Email: chunkwangxavier.tan@digipen.edu
* \date          12 September 2026
* \version       1.1
* \brief
*                The engine's logger: the CRU_TRACE to CRU_FATAL macros,
*                LogSession, and the functions behind them.
*
* \details
*                Messages use std::format syntax. Every line gets the date
*                and time, the time since startup, the level, and the
*                source file and line, and goes to every sink in
*                LogSink.h. A LogSession in main() starts and ends the
*                log.
*
*                Listeners let other code, such as the editor's console,
*                receive each line. The CRU_USER_* macros mark messages
*                meant for the person using the editor rather than for
*                programmers.
*
* \history
*    -v1.0       first version
*    -v1.1       listeners and the CRU_USER_* messages (Xavier)
*
**************************************************************************/
#pragma once

#include <cstdint>
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
        // Empty = no file. A relative path (like the default) is placed next to the running
        // .exe, not in the working directory, so the log is always beside the program it is
        // from - Build/Game/Debug/crucible.log, or the packaged game's folder.
        std::filesystem::path logFile = "crucible.log";
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

    // The full path of the log file being written, or empty if there is none (logFile was
    // empty, or the file could not be opened). Starts the logger if nothing has yet.
    ENGINE_API std::filesystem::path LogFilePath();

    // The single real entry point. Use the CRU_* macros instead of calling
    // this directly - they fill in the file and line for you.
    ENGINE_API void LogMessage(const Level& level,
                               const char* file,
                               int line,
                               const std::string_view& message);

    // Same as LogMessage, but marks the line as front-facing: something the person
    // USING the engine/editor should see (a scene failed to save, a dropped asset was
    // the wrong type), as opposed to developer detail. It still goes to every sink;
    // listeners such as the editor Console use the flag to decide what to show.
    // Use the CRU_USER_* macros rather than calling this directly.
    ENGINE_API void LogUserMessage(const Level& level,
                                   const char* file,
                                   int line,
                                   const std::string_view& message);

    // ---- Listeners -------------------------------------------------------------
    //
    // A listener is told about every line that passes the level filter, after the
    // sinks have written it. This is how code OUTSIDE Engine.dll (the editor's
    // Console panel) receives log output without the sink classes being exported.

    struct LogRecord
    {
        Level            level;
        bool             userFacing;   // logged through CRU_USER_*
        double           seconds;      // ElapsedSeconds() at the time of the call
        std::string_view file;         // file name only ("Core.cpp"); empty if none
        int              line;
        std::string_view message;      // the message alone - no timestamp/level prefix
    };
    // The string_views are only valid during the call: copy what you keep.

    // A plain function pointer + user data, rather than std::function, so nothing
    // with a library-specific layout crosses the DLL boundary.
    //
    // Called on whichever thread logged, while the logger's lock is held, so it
    // must be quick and thread-safe. Anything it logs itself is dropped (the lock
    // is not recursive; dropping beats deadlocking).
    using LogListener = void (*)(const LogRecord& record, void* userData);
    using ListenerId  = std::uint32_t;   // 0 is never a valid id

    ENGINE_API ListenerId AddListener(LogListener listener, void* userData);
    ENGINE_API void       RemoveListener(ListenerId id);   // unknown ids are ignored

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

// Front-facing versions: identical output, but flagged for the person using the
// editor, so the Console panel shows them by default. Use these for things the
// user did or must act on ("Saved scene 'demo'", "Model failed to load"); keep
// the plain macros for developer detail.
#define CRU_USER_LOG(level, ...) \
    ::Crucible::Debug::LogUserMessage((level), __FILE__, __LINE__, ::std::format(__VA_ARGS__))

#define CRU_USER_INFO(...)  CRU_USER_LOG(::Crucible::Debug::Level::Info,    __VA_ARGS__)
#define CRU_USER_WARN(...)  CRU_USER_LOG(::Crucible::Debug::Level::Warning, __VA_ARGS__)
#define CRU_USER_ERROR(...) CRU_USER_LOG(::Crucible::Debug::Level::Error,   __VA_ARGS__)

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
