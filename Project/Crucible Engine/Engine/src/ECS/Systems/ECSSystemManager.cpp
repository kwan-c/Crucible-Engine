#include "ECSSystemManager.h"

namespace Crucible
{
	void ECSSystemManager::Init()
	{
		m_registry.GetSystem(ECSSystemId::Physics).Init();
	}

	void ECSSystemManager::Update(World& world)
	{
		m_registry.GetSystem(ECSSystemId::Physics).Update(world);
	}

	void ECSSystemManager::Shutdown() noexcept
	{
		m_registry.GetSystem(ECSSystemId::Physics).Shutdown();
	}

}