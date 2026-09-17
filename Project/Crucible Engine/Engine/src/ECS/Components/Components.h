#pragma once

#include <cstdint>

#include"ComponentKind.h"
#include"Math/Math.h"

namespace Crucible
{
	struct Transform
	{
		Math::vec3  positions{};
	};

	struct MeshRenderer
	{
		bool visible = true;
		Math::vec4 colour{};
	};
	
	struct Audio
	{
		bool play = false;
		bool loop = false;
	};

	struct RigidBody
	{
		Math::vec3 velocity {};
		float mass = 1.f;
	};

	CRUCIBLE_COMPONENT(Transform, Transform);
	CRUCIBLE_COMPONENT(RigidBody, RigidBody);
	CRUCIBLE_COMPONENT(MeshRenderer, MeshRenderer);
	CRUCIBLE_COMPONENT(Audio, Audio);
	

}