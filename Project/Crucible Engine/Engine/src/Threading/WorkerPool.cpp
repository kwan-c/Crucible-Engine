/*************************************************************************
  \file       WorkerPool.cpp
  \brief
    See WorkerPool.h.
*************************************************************************/
#include "Threading/WorkerPool.h"

#include <algorithm>
#include <chrono>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    #include <immintrin.h>
    #define CRUCIBLE_SPIN_PAUSE() _mm_pause()
#else
    #define CRUCIBLE_SPIN_PAUSE() std::this_thread::yield()
#endif

namespace Crucible {

    namespace {
        // True while this thread is running a loop's item (so a nested ParallelFor runs inline).
        thread_local bool t_inLoop = false;

        // How long a worker keeps checking for the next loop before it goes to sleep.
        constexpr auto kSpinTime = std::chrono::microseconds(50);
    }

    uint32_t WorkerPool::DefaultWorkerCount() {

        const unsigned hardware = std::thread::hardware_concurrency();
        if (hardware <= 1) { return 0; }
        return std::min<uint32_t>(hardware - 1, kMaxDefaultWorkers);

    }   // WorkerPool::DefaultWorkerCount()

    WorkerPool::WorkerPool(uint32_t workerCount) {

        m_threads.reserve(workerCount);
        for (uint32_t i = 0; i < workerCount; ++i) { m_threads.emplace_back([this] { WorkerMain(); }); }

    }   // WorkerPool::WorkerPool()

    WorkerPool::~WorkerPool() {

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop.store(true);
            m_generation.fetch_add(1, std::memory_order_release);
        }
        m_wake.notify_all();
        for (std::thread& t : m_threads) { t.join(); }

    }   // WorkerPool::~WorkerPool()

    void WorkerPool::Run(uint32_t count, Invoke invoke, void* context) {

        // Nested, or another thread's loop is running: just do it here.
        if (t_inLoop || !m_dispatch.try_lock()) {
            for (uint32_t i = 0; i < count; ++i) { invoke(context, i); }
            return;
        }
        std::lock_guard<std::mutex> dispatch(m_dispatch, std::adopt_lock);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_invoke  = invoke;
            m_context = context;
            m_count   = count;
            m_next.store(0, std::memory_order_relaxed);
            m_error   = nullptr;
            m_busy    = static_cast<uint32_t>(m_threads.size());
            m_generation.fetch_add(1, std::memory_order_release);
        }
        m_wake.notify_all();

        t_inLoop = true;
        Work();
        t_inLoop = false;

        // Every worker must be done with this loop before its function and context go away.
        std::exception_ptr error;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_done.wait(lock, [this] { return m_busy == 0; });
            error = m_error;
            m_error = nullptr;
        }
        if (error) { std::rethrow_exception(error); }

    }   // WorkerPool::Run()

    void WorkerPool::Work() {

        for (uint32_t i = m_next.fetch_add(1, std::memory_order_relaxed); i < m_count; i = m_next.fetch_add(1, std::memory_order_relaxed)) {
            try {
                m_invoke(m_context, i);
            } catch (...) {
                std::lock_guard<std::mutex> lock(m_errorMutex);
                if (!m_error) { m_error = std::current_exception(); }
            }
        }

    }   // WorkerPool::Work()

    void WorkerPool::WorkerMain() {

        uint64_t seen = 0;
        for (;;) {
            // A loop often follows the last one within microseconds: check for a while before
            // paying for a sleep and a wake-up.
            bool ready = false;
            const auto spinUntil = std::chrono::steady_clock::now() + kSpinTime;
            for (uint32_t spin = 0; !ready; ++spin) {
                ready = m_generation.load(std::memory_order_acquire) != seen;
                if (!ready && (spin & 63) == 63 && std::chrono::steady_clock::now() >= spinUntil) { break; }
                if (!ready) { CRUCIBLE_SPIN_PAUSE(); }
            }
            if (!ready) {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait(lock, [&] { return m_generation.load(std::memory_order_relaxed) != seen; });
            }
            if (m_stop.load()) { return; }
            seen = m_generation.load(std::memory_order_acquire);

            t_inLoop = true;
            Work();
            t_inLoop = false;

            std::lock_guard<std::mutex> lock(m_mutex);
            if (--m_busy == 0) { m_done.notify_one(); }
        }

    }   // WorkerPool::WorkerMain()

}   // NAMESPACE CRUCIBLE
