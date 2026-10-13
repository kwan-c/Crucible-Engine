/*************************************************************************
  \file       WorkerPool.h
  \brief
    A small pool of worker threads that splits one loop across them:
    ParallelFor(count, fn) runs fn(0) .. fn(count - 1), spread over the
    workers and the calling thread, and returns when every call is done.
  \details
    Made for the renderer's per-view culling and batching (RenderScene),
    which owns no threads of its own; nothing in it is renderer-specific.

    - The calling thread works too, so a pool of N workers runs a loop on
      N + 1 threads. Items are handed out one at a time from a shared
      counter, so uneven items balance themselves.
    - Between loops the workers wait on a condition variable. Right after a
      loop they spin for a short while first, because loops often come back
      to back within a frame and waking a sleeping thread takes time.
    - An exception thrown by fn on any thread is caught and rethrown from
      ParallelFor once the loop has finished (the first one, if several).
    - ParallelFor called from inside a loop, or while another thread's loop
      is running, simply runs on the calling thread.
    - fn(i) calls run at the same time on different threads: each must only
      write what belongs to its own i.
*************************************************************************/
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace Crucible {

    class WorkerPool {
    public:
        /// At most this many workers are started by default (the caller makes one more thread).
        static constexpr uint32_t kMaxDefaultWorkers = 7;

        /// Hardware threads minus one (the caller), at most kMaxDefaultWorkers; 0 on a single core.
        static uint32_t DefaultWorkerCount();

        /*********************************************************************
         \brief  Starts the workers.
         \param[in] workerCount  Threads to start besides the caller's; 0 =
                                 none (every loop runs on the caller).
         *********************************************************************/
        explicit WorkerPool(uint32_t workerCount = DefaultWorkerCount());
        ~WorkerPool();

        WorkerPool(const WorkerPool&) = delete;
        WorkerPool& operator=(const WorkerPool&) = delete;

        /// Threads a loop runs on: the workers plus the calling thread.
        uint32_t ThreadCount() const { return static_cast<uint32_t>(m_threads.size()) + 1; }

        /*********************************************************************
         \brief  Runs fn(i) for every i in [0, count), spread over the workers
                 and the calling thread, and returns when all have finished.
         \param[in] count  Number of calls.
         \param[in] fn     Callable as fn(uint32_t). Must be safe to call
                           concurrently for different i.
         *********************************************************************/
        template <typename Fn>
        void ParallelFor(uint32_t count, Fn&& fn) {
            if (count == 0) { return; }
            if (m_threads.empty() || count == 1) {
                for (uint32_t i = 0; i < count; ++i) { fn(i); }
                return;
            }
            using F = std::remove_reference_t<Fn>;
            Run(count, [](void* context, uint32_t i) { (*static_cast<F*>(context))(i); },
                const_cast<void*>(static_cast<const void*>(&fn)));
        }

    private:
        using Invoke = void (*)(void*, uint32_t);

        void Run(uint32_t count, Invoke invoke, void* context);
        void WorkerMain();
        void Work();   // takes items of the current loop until none are left

        std::vector<std::thread>  m_threads;

        std::mutex                m_dispatch;          // held by the thread whose loop is running
        std::mutex                m_mutex;             // guards the fields below that are not atomic
        std::condition_variable   m_wake;              // workers: a new loop, or stop
        std::condition_variable   m_done;              // caller: every worker has finished the loop
        std::atomic<uint64_t>     m_generation{ 0 };   // +1 per loop (and on stop)
        std::atomic<bool>         m_stop{ false };
        uint32_t                  m_busy = 0;          // workers that have not finished the current loop

        // The current loop. Written before m_generation is advanced; workers read it after seeing
        // the new value, and the caller does not return until every worker is done with it.
        Invoke                    m_invoke = nullptr;
        void*                     m_context = nullptr;
        uint32_t                  m_count = 0;
        std::atomic<uint32_t>     m_next{ 0 };
        std::mutex                m_errorMutex;
        std::exception_ptr        m_error;
    };

}   // NAMESPACE CRUCIBLE
