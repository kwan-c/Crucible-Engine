#pragma once

#include <chrono>

#include "EngineExport.h"
#include "LogLevel.h"

// A stopwatch that times the scope it lives in and logs the result when that
// scope ends - including on an early return or a thrown exception, because
// destructors always run. This is the same RAII idea as LogSession.
//
//     void LoadLevel()
//     {
//         CRU_SCOPED_TIMER("LoadLevel");
//         ...
//     }                                  // logs "LoadLevel took 84.221 ms"
//
// This is a stopgap, not a profiler: it answers "how long did that take"
// tonight. Do not put one inside a per-frame loop - that is what the real
// profiler is for.

namespace Crucible::Debug
{
    class ENGINE_API ScopedTimer
    {
    public:
        explicit ScopedTimer(const char* label, const Level& level = Level::Debug);
        ~ScopedTimer();

        ScopedTimer(const ScopedTimer&)            = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        // const char* rather than std::string, and a plain time_point, so this
        // class has no standard-library members. That is deliberate: exporting
        // a class that owns a std::string across the DLL boundary is what
        // triggers warning C4251.
        const char* m_label;
        Level       m_level;
        std::chrono::steady_clock::time_point m_start;
    };
}

// Two steps, because a##b pastes the literal text "__LINE__" rather than the
// line number. The extra indirection forces __LINE__ to expand first, so two
// timers in the same function get different variable names.
#define CRU_CONCAT_INNER(a, b) a##b
#define CRU_CONCAT(a, b)       CRU_CONCAT_INNER(a, b)

#define CRU_SCOPED_TIMER(label) \
    ::Crucible::Debug::ScopedTimer CRU_CONCAT(cruScopedTimer_, __LINE__){ (label) }
