/*************************************************************************
* \file:         ECSSystemManager.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Implementation of ECSSystemManager: drives every
*                registered system's Init/Update/Shutdown, in order.
*                See ECSSystemManager.h for the public interface.
*
* \details
*                Currently only ECSSystemId::Physics is registered (see
*                IECSSystemRegistry.cpp), so each function here is a
*                single call. As more systems are added to
*                ECSSystemId (IECSSystemRegistry.h), add the matching
*                m_registry.GetSystem(...) call to each of these three
*                functions, in the order the systems should run.
*
* \history
*    -v1.0       initial sequential Init/Update/Shutdown (Physics only)
*
*
**************************************************************************/
#include "ECSSystemManager.h"
#include <algorithm>
#include <Diagnostics/Logger.h>

namespace Crucible
{
	ECSSystemManager::ECSSystemManager()
	{
		for (std::size_t i = 0; i < kSystemCount; ++i)
		{
			m_updateOrder[i] = static_cast<ECSSystemId>(i);
		}

		std::stable_sort(m_updateOrder.begin(), m_updateOrder.end(),
			[this](ECSSystemId a, ECSSystemId b)
			{
				return m_registry.GetSystem(a).Priority() < m_registry.GetSystem(b).Priority();
			});
	}

	void ECSSystemManager::Init()
	{
		for (ECSSystemId id : m_updateOrder)
		{
			m_registry.GetSystem(id).Init();
		}
	}

	void ECSSystemManager::Update(World& world)
	{
		try
		{
			for (ECSSystemId id : m_updateOrder)
			{
				m_registry.GetSystem(id).Update(world, m_commands);
			}
			m_commands.Execute(world);
		}
		catch (const std::exception& e)
		{
			CRU_ERROR("ECSSystemManager::Update: this frame's ECS update failed and was aborted - {}", e.what());
			m_commands.Clear();
		}
		catch (...)
		{
			CRU_ERROR("ECSSystemManager::Update: this frame's ECS update failed with a non-std::exception and was aborted");
			m_commands.Clear();
		}
	}

	void ECSSystemManager::Shutdown() noexcept
	{
		for (auto it = m_updateOrder.rbegin(); it != m_updateOrder.rend(); ++it)
		{
			m_registry.GetSystem(*it).Shutdown();
		}
	}

}