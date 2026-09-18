#pragma once

#include "Systems/IECSSystems.h"
//this is a sample place holder file 

namespace Crucible
{

	class ECSPhysicsSystem final :public IECSSystem
	{
	public:
		void Init() override;
		void Update(World& world)override;
		void Shutdown() noexcept override;
	};
}