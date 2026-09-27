/*************************************************************************
* \file:         ECSSystemManager.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.3
* \brief
*                Implementation of ECSSystemManager: drives every
*                registered system's Init/Update/Shutdown, in order.
*                See ECSSystemManager.h for the public interface.
*
* \details
*                Execution order comes from BuildSystemSchedule()
*                (SystemScheduler.h) and is rebuilt whenever the set of
*                systems changes - no per-system code lives here, so adding
*                a system only means registering it. Update() runs one stage
*                at a time: every batch of the stage runs (serially, on this
*                thread), then every one of the stage's CommandBuffers is
*                replayed. Failures are isolated per system: a throwing
*                Update() or command replay discards only that system's
*                commands, and a system that fails
*                kMaxConsecutiveFailures frames in a row is disabled. A
*                failure counter is only reset once BOTH the Update() and
*                the replay of a frame succeeded (failedThisFrame).
*
* \history
*    -v1.0       initial sequential Init/Update/Shutdown (Physics only)
*    -v1.1       stage/priority ordering, per-system CommandBuffers,
*                per-system failure isolation, idempotent Init/Shutdown
*    -v1.2       schedule built by SystemScheduler (dependencies, access
*                conflicts, batches); batches executed serially through the
*                ExecuteBatch() seam; LogSchedule()
*    -v1.3       each Update() runs inside the system's declared-access
*                scope; MainThread affinity enforced
*
**************************************************************************/

#include <exception>
#include <string>
#include <string_view>
#include <thread>
#include <typeinfo>

#include "ECSSystemManager.h"
#include "SystemAccessScope.h"

#include <Diagnostics/Logger.h>

namespace Crucible
{
	namespace
	{
		// Sets a flag for the lifetime of a scope, restoring it on every way out.
		class ScopedFlag
		{
		public:
			explicit ScopedFlag(bool& flag) noexcept : m_flag(flag), m_previous(flag) { m_flag = true; }
			~ScopedFlag() { m_flag = m_previous; }

			ScopedFlag(const ScopedFlag&) = delete;
			ScopedFlag& operator=(const ScopedFlag&) = delete;

		private:
			bool& m_flag;
			bool  m_previous;
		};

		// "class Game::MovementSystem" -> "Game::MovementSystem" for readable logs.
		std::string SystemName(const IECSSystem& system)
		{
			std::string_view name = typeid(system).name();
			for (std::string_view prefix : { std::string_view("class "), std::string_view("struct ") })
			{
				if (name.substr(0, prefix.size()) == prefix) name.remove_prefix(prefix.size());
			}
			return std::string(name);
		}

		const char* AffinityName(SystemAffinity affinity) noexcept
		{
			switch (affinity)
			{
			case SystemAffinity::Worker:       return "Worker";
			case SystemAffinity::MainThread:   return "MainThread";
			case SystemAffinity::RenderThread: return "RenderThread";
			default:                           return "Unknown";
			}
		}

		// Component ids set in a signature, e.g. "0,2" (empty -> "-").
		std::string ComponentList(const ComponentSignature& signature)
		{
			std::string text;
			for (std::size_t id = 0; id < signature.size(); ++id)
			{
				if (!signature.test(id)) continue;
				if (!text.empty()) text += ',';
				text += std::to_string(id);
			}
			return text.empty() ? "-" : text;
		}
	}

	void ECSSystemManager::RebuildSchedule()
	{
		std::vector<SystemNode> nodes;
		nodes.reserve(m_registry.Size());

		for (std::size_t index = 0; index < m_registry.Size(); ++index)
		{
			const IECSSystem& system = m_registry.GetSystem(index);

			// Stage()/Priority() are the defaults; Describe() may refine them.
			SystemDesc desc;
			desc.stage = system.Stage();
			desc.priority = system.Priority();
			system.Describe(desc);

			nodes.push_back(SystemNode{ std::move(desc), m_registry.TypeAt(index), SystemName(system) });
		}

		try
		{
			// Only replace the current plan once the new one is known to be valid.
			m_schedule = BuildSystemSchedule(nodes);
		}
		catch (const std::exception& e)
		{
			CRU_ERROR("ECSSystemManager: invalid system schedule - {}", e.what());
			throw;
		}
	}

	void ECSSystemManager::Init()
	{
		if (m_iterating)
		{
			throw std::logic_error("ECSSystemManager::Init called while systems are running");
		}
		if (m_initialized)
		{
			CRU_WARN("ECSSystemManager::Init called more than once - ignored");
			return;
		}

		// Cycles and impossible dependencies fail here, before any system's Init() runs.
		RebuildSchedule();

		// The thread that initialises the manager is the main thread for affinity checks.
		m_mainThread = std::this_thread::get_id();

		{
			ScopedFlag running(m_iterating);

			const std::vector<std::size_t>& order = m_schedule.serialOrder;
			for (std::size_t i = 0; i < order.size(); ++i)
			{
				const std::size_t index = order[i];
				try
				{
					m_registry.GetSystem(index).Init();
					m_slots[index].initialised = true;
				}
				catch (...)
				{
					// Undo the systems already brought up, newest first.
					for (std::size_t j = i; j-- > 0;)
					{
						SystemSlot& slot = m_slots[order[j]];
						if (!slot.initialised) continue;
						m_registry.GetSystem(order[j]).Shutdown();
						slot.initialised = false;
					}
					throw;
				}
			}
		}

		m_initialized = true;
	}

	void ECSSystemManager::Update(World& world)
	{
		ScopedFlag running(m_iterating);

		for (const StageSchedule& stage : m_schedule.stages)
		{
			for (const ExecutionBatch& batch : stage.batches)
			{
				ExecuteBatch(batch, world);
			}

			// Flush structural ECS changes after the stage finishes, so later stages see them.
			FlushStage(stage, world);
		}
	}

	void ECSSystemManager::ExecuteBatch(const ExecutionBatch& batch, World& world)
	{
		// Parallel execution is not available yet: every batch runs serially.
		// A future job system branches here (serial stays as the "parallel off" mode).
		ExecuteBatchSerial(batch, world);
	}

	void ECSSystemManager::ExecuteBatchSerial(const ExecutionBatch& batch, World& world)
	{
		// Batch order is the scheduler's serial order, so this is deterministic.
		for (std::size_t index : batch.systems)
		{
			RunSystem(index, world);
		}
	}

	void ECSSystemManager::RunSystem(std::size_t index, World& world)
	{
		SystemSlot& slot = m_slots[index];
		if (slot.disabled) return;

		slot.failedThisFrame = false;

		try
		{
			const SystemDesc& desc = m_schedule.descs[index];

			// Main-thread systems (e.g. anything touching the window or graphics context) never run elsewhere.
			if (desc.affinity == SystemAffinity::MainThread && std::this_thread::get_id() != m_mainThread)
			{
				throw std::logic_error("system has MainThread affinity but was run on another thread");
			}

			// While Update runs, World and Query hold the system to the access it declared.
			const ActiveSystemAccess active{ &desc.access, m_schedule.names[index].c_str() };
			SystemAccessScope accessScope(active);

			m_registry.GetSystem(index).Update(world, slot.commands);
		}
		catch (const std::exception& e)
		{
			HandleFailure(index, e.what());
		}
		catch (...)
		{
			HandleFailure(index, "non-std::exception");
		}
	}

	void ECSSystemManager::FlushStage(const StageSchedule& stage, World& world)
	{
		// Replay in execution order, one system at a time.
		for (const ExecutionBatch& batch : stage.batches)
		{
			for (std::size_t index : batch.systems)
			{
				SystemSlot& slot = m_slots[index];
				if (slot.disabled) continue;

				try
				{
					slot.commands.Execute(world);
				}
				catch (const std::exception& e)
				{
					HandleFailure(index, e.what());
				}
				catch (...)
				{
					HandleFailure(index, "non-std::exception");
				}

				// Only a frame whose Update AND replay both succeeded counts as healthy.
				if (!slot.failedThisFrame) slot.consecutiveFailures = 0;
			}
		}
	}

	void ECSSystemManager::HandleFailure(std::size_t index, const char* what) noexcept
	{
		SystemSlot& slot = m_slots[index];
		slot.commands.Clear();   // lose only this system's commands

		// A system failing in both Update and replay in one frame counts once.
		if (!slot.failedThisFrame)
		{
			slot.failedThisFrame = true;
			++slot.consecutiveFailures;
		}

		const char* name = typeid(m_registry.GetSystem(index)).name();

		try
		{
			CRU_ERROR("ECSSystemManager: system '{}' failed ({} in a row) - {}", name, slot.consecutiveFailures, what);

			if (slot.consecutiveFailures >= kMaxConsecutiveFailures)
			{
				CRU_ERROR("ECSSystemManager: system '{}' disabled after {} consecutive failures", name, slot.consecutiveFailures);
			}
		}
		catch (...)
		{
			// Logging must never take the frame down.
		}

		if (slot.consecutiveFailures >= kMaxConsecutiveFailures) slot.disabled = true;
	}

	void ECSSystemManager::Shutdown() noexcept
	{
		if (!m_initialized) return;

		const std::vector<std::size_t>& order = m_schedule.serialOrder;
		for (auto it = order.rbegin(); it != order.rend(); ++it)
		{
			SystemSlot& slot = m_slots[*it];
			if (!slot.initialised) continue;

			m_registry.GetSystem(*it).Shutdown();
			slot.initialised = false;
		}

		for (SystemSlot& slot : m_slots) slot.commands.Clear();

		m_initialized = false;
	}

	void ECSSystemManager::LogSchedule() const
	{
		CRU_INFO("ECS schedule: {} systems, {} stages (serial execution)", m_schedule.serialOrder.size(), m_schedule.stages.size());

		for (const StageSchedule& stage : m_schedule.stages)
		{
			for (std::size_t batchIndex = 0; batchIndex < stage.batches.size(); ++batchIndex)
			{
				for (std::size_t index : stage.batches[batchIndex].systems)
				{
					const SystemDesc& desc = m_schedule.descs[index];
					const std::string access = !desc.access.declared ? std::string("undeclared (exclusive)")
						: desc.access.exclusive ? std::string("exclusive")
						: "reads " + ComponentList(desc.access.reads) + " writes " + ComponentList(desc.access.writes);

					CRU_INFO("  {} batch {}: {} [priority {}, {}, {}]", StageName(stage.stage), batchIndex,
						m_schedule.names[index], desc.priority, AffinityName(desc.affinity), access);
				}
			}
		}
	}

}
