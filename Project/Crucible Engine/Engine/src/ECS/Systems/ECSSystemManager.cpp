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

namespace Crucible
{
	void ECSSystemManager::Init()
	{
		m_registry.GetSystem(ECSSystemId::Physics).Init();
	}

	void ECSSystemManager::Update(World& world)
	{
		m_registry.GetSystem(ECSSystemId::Physics).Update(world, m_commands);

		m_commands.Execute(world);
	}

	void ECSSystemManager::Shutdown() noexcept
	{
		m_registry.GetSystem(ECSSystemId::Physics).Shutdown();
	}

}