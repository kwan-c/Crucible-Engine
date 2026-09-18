#pragma once

#pragma once


namespace Crucible
{
	class World;

	// base system class for ecs

	class IECSSystem
	{
	public:
		
		virtual ~IECSSystem() = default;

		virtual void Init() {}
		virtual void Update(World& world) = 0;
		virtual void Shutdown()noexcept {}


	};
}