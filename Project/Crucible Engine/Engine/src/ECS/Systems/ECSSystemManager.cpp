/*************************************************************************
* \file:         ECSSystemManager.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.1
* \brief
*                Implementation of ECSSystemManager: drives every
*                registered system's Init/Update/Shutdown, in order.
*                See ECSSystemManager.h for the public interface.
*
* \details
*                Execution order is rebuilt from (Stage(), Priority())
*                whenever the set of systems changes - no per-system code
*                lives here, so adding a system only means registering it.
*                Update() runs one stage at a time: every system in the
*                stage runs, then every one of their CommandBuffers is
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
*
**************************************************************************/

#include <algorithm>
#include <exception>
#include <numeric>
#include <typeinfo>

#include "ECSSystemManager.h"

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
	}

	void ECSSystemManager::RebuildUpdateOrder()
	{
		m_updateOrder.resize(m_registry.Size());

		std::iota(m_updateOrder.begin(), m_updateOrder.end(), std::size_t{ 0 });

		std::stable_sort(m_updateOrder.begin(), m_updateOrder.end(),
			[this](std::size_t a, std::size_t b)
			{
				const IECSSystem& lhs = m_registry.GetSystem(a);
				const IECSSystem& rhs = m_registry.GetSystem(b);
				if (lhs.Stage() != rhs.Stage()) return lhs.Stage() < rhs.Stage();
				return lhs.Priority() < rhs.Priority();
			}
		);
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

		RebuildUpdateOrder();

		{
			ScopedFlag running(m_iterating);

			for (std::size_t i = 0; i < m_updateOrder.size(); ++i)
			{
				const std::size_t index = m_updateOrder[i];
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
						SystemSlot& slot = m_slots[m_updateOrder[j]];
						if (!slot.initialised) continue;
						m_registry.GetSystem(m_updateOrder[j]).Shutdown();
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

		std::size_t begin = 0;
		while (begin < m_updateOrder.size())
		{
			const SystemStage stage = m_registry.GetSystem(m_updateOrder[begin]).Stage();

			std::size_t end = begin;
			while (end < m_updateOrder.size() && m_registry.GetSystem(m_updateOrder[end]).Stage() == stage)
			{
				RunSystem(m_updateOrder[end], world);
				++end;
			}

			// Later stages see this stage's structural changes in the same frame.
			FlushStage(begin, end, world);
			begin = end;
		}
	}

	void ECSSystemManager::RunSystem(std::size_t index, World& world)
	{
		SystemSlot& slot = m_slots[index];
		if (slot.disabled) return;

		slot.failedThisFrame = false;

		try
		{
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

	void ECSSystemManager::FlushStage(std::size_t begin, std::size_t end, World& world)
	{
		for (std::size_t i = begin; i < end; ++i)
		{
			const std::size_t index = m_updateOrder[i];
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

		for (auto it = m_updateOrder.rbegin(); it != m_updateOrder.rend(); ++it)
		{
			SystemSlot& slot = m_slots[*it];
			if (!slot.initialised) continue;

			m_registry.GetSystem(*it).Shutdown();
			slot.initialised = false;
		}

		for (SystemSlot& slot : m_slots) slot.commands.Clear();

		m_initialized = false;
	}

}
