/*************************************************************************
* \file:         ECSSystemManager.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.3
* \brief
*                Declares ECSSystemManager: the top-level driver that
*                owns an ECSSystemRegistry (see IECSSystemRegistry.h)
*                and runs every registered system's Init/Update/Shutdown.
*
* \details
*                This is the class the engine's Core (see
*                Engine/src/Core.cpp) owns and drives each frame - it
*                hides the per-system loop behind a single
*                Init()/Update(World&)/Shutdown() call. Systems run stage
*                by stage (IECSSystem::Stage()), ordered by Priority()
*                within a stage. Each system has its own CommandBuffer,
*                flushed at the end of its stage. A system that throws
*                loses only its own commands for that frame, and is
*                disabled after kMaxConsecutiveFailures failing frames in
*                a row.
*
*                The order comes from the scheduler (SystemScheduler.h):
*                RunAfter/RunBefore dependencies first, then Priority(),
*                then registration order, with systems grouped into batches
*                that could theoretically run in parallel. Batches are
*                executed serially here - a future job system plugs in at
*                ExecuteBatch() without changing this class's interface.
*
* \history
*    -v1.0       initial sequential single-system driver (Physics only)
*    -v1.1       stages, per-system CommandBuffers flushed per stage,
*                per-system failure isolation, idempotent Init/Shutdown,
*                registration after Init, FindSystem<T>(), ENGINE_API;
*                existing public names unchanged
*    -v1.2       dependency/access-aware scheduling via SystemScheduler
*                (DAG + topological order + batches), serial batch
*                execution, Schedule()/LogSchedule(); existing public names
*                unchanged
*    -v1.3       declared-access enforcement and MainThread affinity check
*                while systems run
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "EngineExport.h"
#include "IECSSystemRegistry.h"
#include "SystemScheduler.h"

namespace Crucible
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
    /*************************************************************************
     \brief
        Top-level driver for all registered ECS systems.

     \details
        Owns the ECSSystemRegistry and one CommandBuffer per system.
        Registered systems are ordered by the scheduler (stage, then
        RunAfter/RunBefore dependencies, then Priority(), then registration
        order), updated stage by stage each frame, and shut down in reverse
        execution order.

     \note
        Systems may be registered before or after Init(); a system
        registered after Init() is initialised immediately. Registering a
        system from inside Update() throws std::logic_error.
     *************************************************************************/
    class ENGINE_API ECSSystemManager
    {
    public:

        /*********************************************************************
         \brief  Consecutive failing frames after which a system is disabled.
         *********************************************************************/
        static constexpr std::uint32_t kMaxConsecutiveFailures = 3;

        ECSSystemManager() = default;
        ~ECSSystemManager() = default;

        ECSSystemManager(const ECSSystemManager&) = delete;
        ECSSystemManager& operator=(const ECSSystemManager&) = delete;
        ECSSystemManager(ECSSystemManager&&) = delete;
        ECSSystemManager& operator=(ECSSystemManager&&) = delete;

        /*********************************************************************
         \brief
            Registers a system into the ECS system manager.

         \tparam T
            Type derived from IECSSystem.

         \param args
            Constructor arguments forwarded to T.

         \return
            Reference to the newly created system.

         \note
            Throws std::logic_error if called from inside Update(), or if a
            system of type T is already registered. If Init() has already
            run, the schedule is rebuilt and the new system's Init() is
            called immediately. If the new system makes the schedule invalid
            (e.g. a dependency cycle) it is unregistered again and the error
            propagates; if its Init() throws, it stays registered but
            disabled and the exception propagates.
         *********************************************************************/
        template<typename T, typename... Args>
        T& RegisterSystem(Args&&... args)
        {
            if (m_iterating)
            {
                throw std::logic_error("ECSSystemManager::RegisterSystem called while systems are running");
            }

            // Reserve first so the registry and m_slots cannot get out of step.
            m_slots.reserve(m_slots.size() + 1);

            T& system = m_registry.RegisterSystem<T>(std::forward<Args>(args)...);
            m_slots.emplace_back();

            if (m_initialized)
            {
                // Re-plan with the new system; undo the registration if the plan becomes invalid.
                try
                {
                    RebuildSchedule();
                }
                catch (...)
                {
                    m_registry.PopBackSystem();
                    m_slots.pop_back();
                    throw;
                }

                try
                {
                    system.Init();
                    m_slots.back().initialised = true;
                }
                catch (...)
                {
                    m_slots.back().disabled = true;
                    throw;
                }
            }

            return system;
        }

        /*********************************************************************
         \brief  Builds the schedule, then calls Init() on every registered
                 system in execution order.
         \note   A second call is ignored with a warning. An invalid schedule
                 (dependency cycle, dependency contradicting stage order)
                 throws std::logic_error before any system's Init() runs. If
                 a system's Init() throws, every system already initialised
                 is shut down again (reverse order) and the exception
                 propagates.
         *********************************************************************/
        void Init();

        /*********************************************************************
         \brief  Runs one frame: each stage's batches in schedule order, then
                 that stage's command buffers.
         \param[in,out] world The world every system will read/modify.
         \note   Every system runs serially on the calling thread. A system
                 that declared its access (Describe()) is held to it while
                 it runs: undeclared component access and direct structural
                 World calls throw and count as a failure. A MainThread
                 system run from a thread other than Init()'s fails too.
         *********************************************************************/
        void Update(World& world);

        /*********************************************************************
         \brief  Calls Shutdown() on every initialised system, in reverse
                 execution order. Safe to call more than once.
         *********************************************************************/
        void Shutdown() noexcept;

        /*********************************************************************
         \brief  Fetches a registered system by index, mutable access.
         \param[in] index The system to fetch.
         \return Reference to that system.
         *********************************************************************/
        IECSSystem& GetSystem(std::size_t index)
        {
            return m_registry.GetSystem(index);
        }

        /*********************************************************************
         \brief  Fetches a registered system by index, read-only access.
         \param[in] index The system to fetch.
         \return Const reference to that system.
         *********************************************************************/
        const IECSSystem& GetSystem(std::size_t index) const
        {
            return m_registry.GetSystem(index);
        }

        /*********************************************************************
         \brief  The registered system of type T.
         \return Pointer to it, or nullptr if no system of type T exists.
         *********************************************************************/
        template <typename T>
        T* FindSystem() noexcept
        {
            return m_registry.FindSystem<T>();
        }

        /*********************************************************************
         \brief  The current execution plan (stages, batches, resolved
                 descriptions). Empty until Init().
         *********************************************************************/
        const SystemSchedule& Schedule() const noexcept
        {
            return m_schedule;
        }

        /*********************************************************************
         \brief  Logs the current schedule (stages, batches, affinity and
                 access) for debugging.
         *********************************************************************/
        void LogSchedule() const;

    private:

        struct SystemSlot
        {
            CommandBuffer commands;
            std::uint32_t consecutiveFailures = 0;
            bool failedThisFrame = false;   // Update or command replay failed this frame
            bool initialised = false;       // Init() succeeded, so Shutdown() is owed
            bool disabled = false;
        };

        void RebuildSchedule();

        void ExecuteBatch(const ExecutionBatch& batch, World& world);

        void ExecuteBatchSerial(const ExecutionBatch& batch, World& world);

        void RunSystem(std::size_t index, World& world);

        void FlushStage(const StageSchedule& stage, World& world);

        void HandleFailure(std::size_t index, const char* what) noexcept;

        ECSSystemRegistry m_registry;

        std::vector<SystemSlot> m_slots;          // parallel to the registry

        SystemSchedule m_schedule;                // execution plan, rebuilt on Init()/late registration

        bool m_initialized = false;
        bool m_iterating = false;                 // inside Init() or Update()
        std::thread::id m_mainThread{};           // thread that called Init(); MainThread systems run only here
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
