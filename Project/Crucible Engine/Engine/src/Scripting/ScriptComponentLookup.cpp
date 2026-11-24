/*************************************************************************
* \file:         ScriptComponentLookup.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief         Implementation - see ScriptComponentLookup.h.
*
**************************************************************************/
#include "Scripting/ScriptComponentLookup.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Audio/AudioComponents.h"
#include "Physics/PhysicsComponents.h"
#include "Player/PlayerComponents.h"

namespace Crucible
{
	bool HasScriptComponent(const World& world, Entity entity, std::string_view type)
	{
		if (!world.IsAlive(entity))
			return false;
		// Keep in step with kScriptComponentTypes (ScriptTypes.h).
		if (type == "Transform")           return world.HasComponent<Crucible::Transform>(entity);
		if (type == "RigidBody")           return world.HasComponent<Crucible::RigidBody>(entity);
		if (type == "Animator")            return world.HasComponent<Crucible::AnimatedModel>(entity);
		if (type == "AudioSource")         return world.HasComponent<Crucible::AudioSource>(entity);
		if (type == "CharacterController") return world.HasComponent<Crucible::CharacterController>(entity);
		if (type == "Camera")              return world.HasComponent<Crucible::CameraComponent>(entity);
		if (type == "CameraOrbit")         return world.HasComponent<Crucible::CameraOrbit>(entity);
		if (type == "Light")               return world.HasComponent<Crucible::Light>(entity);
		if (type == "ParticleEmitter")     return world.HasComponent<Crucible::ParticleEmitter>(entity);
		if (type == "Collider")            return world.HasComponent<Crucible::Collider>(entity);
		if (type == "MeshRenderer")        return world.HasComponent<Crucible::MeshRenderer>(entity);
		if (type == "PlayerController")    return world.HasComponent<Crucible::PlayerController>(entity);
		if (type == "WorldText")           return world.HasComponent<Crucible::WorldText>(entity);
		if (type == "MaterialParams")      return world.HasComponent<Crucible::MaterialParams>(entity);
		if (type == "FlowFieldAgent")      return world.HasComponent<Crucible::FlowFieldAgent>(entity);
		if (type == "FlowFieldSurface")    return world.HasComponent<Crucible::FlowFieldSurface>(entity);
		if (type == "StateGraphAgent")     return world.HasComponent<Crucible::StateGraphAgent>(entity);
		if (type == "Terrain")             return world.HasComponent<Crucible::Terrain>(entity);
		if (type == "UIText")              return world.HasComponent<Crucible::UIText>(entity);
		if (type == "UIImage")             return world.HasComponent<Crucible::UIImage>(entity);
		if (type == "UISlider")            return world.HasComponent<Crucible::UISlider>(entity);
		if (type == "UIButton")            return world.HasComponent<Crucible::UIButton>(entity);
		if (type == "UIRect")              return world.HasComponent<Crucible::UIRect>(entity);
		if (type == "UICanvas")            return world.HasComponent<Crucible::UICanvas>(entity);
		return false;
	}
}
