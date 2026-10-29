/*************************************************************************
* \file:         Logger.cpp
* \author:       Hugo Low Ren Hao (main author),
*                Tan Chun Kwang Xavier (v1.1: listeners, user-facing messages)
* \par:          Email: low.h@digipen.edu,
*                Email: chunkwangxavier.tan@digipen.edu
* \date          12 September 2026
* \version       1.1
* \brief
*                The logger's implementation: shared state behind one
*                mutex, line formatting, the sinks and the listeners.
*
* \details
*                Logging before Init starts a default session, so early
*                messages are not lost. The mutex makes it safe to log
*                from Jolt's worker threads. A listener that logs from
*                inside its own callback is dropped rather than
*                deadlocking.
*
* \history
*    -v1.0       first version
*    -v1.1       listeners and the CRU_USER_* messages (Xavier)
*
**************************************************************************/
#include "Logger.h"

#include "LogSink.h"
#include "LogTime.h"
#include "Systems/ExecutablePath.h"

#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace Crucible::Debug
{
    namespace   // everything in here is private to this file
    {
        struct ListenerEntry
        {
            ListenerId  id;
            LogListener fn;
            void*       userData;
        };

        struct LoggerState
        {
            std::vector<std::unique_ptr<ISink>> sinks;
            std::vector<ListenerEntry>          listeners;
            ListenerId                          nextListenerId = 1;
            std::mutex                          mutex;

            Level minLevel   = Level::Trace;
            bool  showSource = true;

            std::atomic<bool> initialised{ false };

            std::string startedAt;      // wall-clock text, captured once at Init
            std::filesystem::path logFilePath;   // resolved path of the open log file, or empty
            TimePoint   startTick{};    // monotonic, for the runtime figure
        };

        // A function-local static is constructed the first time this is
        // called, which means there is no static-initialisation-order problem
        // no matter which part of the engine logs first.
        LoggerState& State()
        {
            static LoggerState state;
            return state;
        }

        // "C:\Users\...\Core.cpp"  ->  "Core.cpp"
        std::string_view FileNameOnly(const char* path)
        {
            if (path == nullptr)
            {
                return {};
            }

            const std::string_view full{ path };
            const auto slash = full.find_last_of("/\\");

            return (slash == std::string_view::npos) ? full : full.substr(slash + 1);
        }

        // Writes straight to the sinks, skipping the level filter. Used for
        // the session banners, which must appear whatever the filter is set to.
        void WriteRaw(const std::string_view& line, const bool& flushAfter)
        {
            auto& state = State();
            const std::scoped_lock lock{ state.mutex };

            for (auto& sink : state.sinks)
            {
                sink->Write(line);

                if (flushAfter)
                {
                    sink->Flush();
                }
            }
        }
    }

    // ------------------------------------------------------------------ Setup

    void Init(const LogConfig& config)
    {
        auto& state = State();

        {
            const std::scoped_lock lock{ state.mutex };

            if (state.initialised)
            {
                return;             // calling Init twice is harmless
            }

            state.minLevel   = config.minLevel;
            state.showSource = config.showSourceLocation;
            state.startTick  = Clock::now();
            state.startedAt  = LocalTimestamp();

            state.sinks.clear();

            if (config.consoleSink)
            {
                state.sinks.push_back(std::make_unique<ConsoleSink>());
            }

            if (config.debugOutputSink)
            {
                state.sinks.push_back(std::make_unique<DebugOutputSink>());
            }

            // The file sink is created before anything is logged, so the
            // opening banner is the first line in the file.
            state.logFilePath.clear();
            if (!config.logFile.empty())
            {
                // Relative -> next to the .exe (see LogConfig::logFile).
                std::filesystem::path path = config.logFile;
                if (path.is_relative() && !ExecutableDirectory().empty())
                {
                    path = ExecutableDirectory() / path;
                }

                auto file = std::make_unique<FileSink>(path);

                if (file->IsOpen())
                {
                    state.sinks.push_back(std::move(file));
                    state.logFilePath = path;
                }
            }

            state.initialised = true;
        }
        // Lock released before logging: the mutex is not recursive, so
        // logging while still holding it would deadlock.

        WriteRaw("========================================"
                 "========================================", false);
        WriteRaw(std::format("Crucible session started {}", state.startedAt), false);

        // Record how this run was configured, so a log file is always
        // self-explanatory when someone sends you one.
        LogSetting("Logger", "console",         config.consoleSink);
        LogSetting("Logger", "vsOutput",        config.debugOutputSink);
        // UTF-8, not .string(): that throws on Windows for folder names outside the code page.
        const std::u8string logFileText = state.logFilePath.u8string();
        LogSetting("Logger", "logFile",         state.logFilePath.empty()
                                                    ? std::string{ "(none)" }
                                                    : std::string(reinterpret_cast<const char*>(logFileText.data()), logFileText.size()));
        LogSetting("Logger", "minLevel",        LevelName(config.minLevel));
        LogSetting("Logger", "sourceLocations", config.showSourceLocation);

        WriteRaw("----------------------------------------"
                 "----------------------------------------", true);
    }

    void Shutdown()
    {
        auto& state = State();

        if (!state.initialised)
        {
            return;
        }

        WriteRaw("----------------------------------------"
                 "----------------------------------------", false);
        WriteRaw(std::format("Crucible session ended {} - started {}, ran for {:.3f}s",
                             LocalTimestamp(),
                             state.startedAt,
                             SecondsSince(state.startTick)),
                 true);
        WriteRaw("----------------------------------------"
                 "----------------------------------------", false);

        const std::scoped_lock lock{ state.mutex };

        for (auto& sink : state.sinks)
        {
            sink->Flush();
        }

        // Release the buffers, not just their contents. clear() keeps a
        // vector's capacity allocated, and this DLL's statics are destroyed
        // *after* the exe's leak dump runs - so anything still held here
        // gets reported as a leak.
        state.sinks.clear();
        state.sinks.shrink_to_fit();
        state.listeners.clear();
        state.listeners.shrink_to_fit();

        state.startedAt.clear();
        state.startedAt.shrink_to_fit();

        state.initialised = false;
    }

    // ---------------------------------------------------------------- Queries

    void SetMinLevel(const Level& level)
    {
        auto& state = State();
        const std::scoped_lock lock{ state.mutex };
        state.minLevel = level;
    }

    Level GetMinLevel()
    {
        auto& state = State();
        const std::scoped_lock lock{ state.mutex };
        return state.minLevel;
    }

    bool IsEnabled(const Level& level)
    {
        return level >= GetMinLevel();
    }

    void Flush()
    {
        auto& state = State();
        const std::scoped_lock lock{ state.mutex };

        for (auto& sink : state.sinks)
        {
            sink->Flush();
        }
    }

    double ElapsedSeconds()
    {
        return SecondsSince(State().startTick);
    }

    std::filesystem::path LogFilePath()
    {
        auto& state = State();
        if (!state.initialised)
        {
            Init();     // same as the first log call would
        }

        const std::scoped_lock lock{ state.mutex };
        return state.logFilePath;
    }

    // --------------------------------------------------------------- Logging

    namespace
    {
        // True while this thread is inside a listener callback. A listener that
        // logs would try to take the (non-recursive) mutex it is already under.
        thread_local bool t_inListener = false;

        void Dispatch(const Level& level,
                      const char* file,
                      int line,
                      const std::string_view& message,
                      bool userFacing)
        {
            if (t_inListener)
            {
                return;     // see LogListener's comment: dropped, not deadlocked
            }

            auto& state = State();

            // Logging before Init would otherwise be silently lost. Start a
            // default session instead so nothing disappears.
            if (!state.initialised)
            {
                Init();
            }

            const std::scoped_lock lock{ state.mutex };

            if (level < state.minLevel)
            {
                return;
            }

            const double seconds = SecondsSince(state.startTick);

            std::string out = std::format("[{}] [+{:8.3f}s] [{}]",
                                          LocalTimestamp(),
                                          seconds,
                                          LevelName(level));

            if (state.showSource && file != nullptr)
            {
                out += std::format(" [{}:{}]", FileNameOnly(file), line);
            }

            out += ' ';
            out += message;

            // Anything at Error or worse is flushed immediately: if the process
            // dies a moment later, this line is already safely on disk.
            const bool urgent = (level >= Level::Error);

            for (auto& sink : state.sinks)
            {
                sink->Write(out);

                if (urgent)
                {
                    sink->Flush();
                }
            }

            if (!state.listeners.empty())
            {
                const LogRecord record{ level, userFacing, seconds, FileNameOnly(file), line, message };

                t_inListener = true;
                for (const auto& listener : state.listeners)
                {
                    listener.fn(record, listener.userData);
                }
                t_inListener = false;
            }
        }
    }

    void LogMessage(const Level& level,
                    const char* file,
                    int line,
                    const std::string_view& message)
    {
        Dispatch(level, file, line, message, false);
    }

    void LogUserMessage(const Level& level,
                        const char* file,
                        int line,
                        const std::string_view& message)
    {
        Dispatch(level, file, line, message, true);
    }

    ListenerId AddListener(LogListener listener, void* userData)
    {
        if (listener == nullptr)
        {
            return 0;
        }

        auto& state = State();
        const std::scoped_lock lock{ state.mutex };

        const ListenerId id = state.nextListenerId++;
        state.listeners.push_back({ id, listener, userData });
        return id;
    }

    void RemoveListener(ListenerId id)
    {
        auto& state = State();
        const std::scoped_lock lock{ state.mutex };

        std::erase_if(state.listeners, [id](const ListenerEntry& entry) { return entry.id == id; });
    }

    void LogSetting(const std::string_view& system,
                    const std::string_view& key,
                    const std::string_view& value)
    {
        LogMessage(Level::Info, nullptr, 0,
                   std::format("  config | {:<10} {:<16} = {}", system, key, value));
    }

    void FatalExit(const std::string_view& reason, const char* file, int line)
    {
        LogMessage(Level::Fatal, file, line, reason);
        Flush();

#ifdef _WIN32
        // Break at the caller's line if a debugger is attached, so you land
        // on the failure rather than inside this function.
        if (::IsDebuggerPresent())
        {
            __debugbreak();
        }
#endif

        Shutdown();     // closes the log file cleanly before we go
        std::abort();
    }

    // -------------------------------------------------------------- Session

    LogSession::LogSession(const LogConfig& config)
    {
        Init(config);
    }

    LogSession::~LogSession()
    {
        Shutdown();
    }
}
