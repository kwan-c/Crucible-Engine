/*************************************************************************
* \file:         NetQueryBodies.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          25 November 2026
* \version       1.1
* \brief
*                Client query bodies (see the header).
*
* \history
*    -v1.0       Networking C# Phase 8; FindClientQueryBody
*    -v1.1       a CharacterController wins over a Collider on the same entity (the host's
*                physics ignores that Collider), and its capsule ignores the entity's scale,
*                as the host's character capsule does
*
**************************************************************************/
#include "Network/NetQueryBodies.h"

#include <cmath>
#include <optional>
#include <string>

#include "ECS/Components/Components.h"
#include "ECS/World.h"

namespace Crucible
{
	namespace
	{
		struct Source
		{
			Collider shape;
			bool     character = false;   // from a CharacterController: its capsule ignores scale
		};

		// The shape a switched-off Collider or CharacterController on `e` stands for, if any.
		// As on the host: an entity with a CharacterController is that character's capsule (its
		// hidden inner body is what raycasts hit) and any Collider on it is ignored - the physics
		// system builds no body for it (ECSPhysicsSystem's collider query excludes them).
		std::optional<Source> SourceShape(const World& world, Entity e)
		{
			if (const CharacterController* character = world.Read<CharacterController>(e))
			{
				if (world.IsComponentEnabled(e, ComponentTypeId<CharacterController>())) return std::nullopt;   // a real capsule already
				Source capsule{ .character = true };
				capsule.shape.m_type   = ShapeType::Capsule;
				capsule.shape.m_height = character->m_height;
				capsule.shape.m_radius = character->m_radius;
				capsule.shape.m_center = character->m_center;
				capsule.shape.m_layer  = character->m_layer;
				return capsule;
			}
			if (const Collider* collider = world.Read<Collider>(e); collider != nullptr && !world.IsComponentEnabled(e, ComponentTypeId<Collider>()))
				return Source{ .shape = *collider };
			return std::nullopt;
		}

		// A character's capsule is not sized by its entity's scale (ECSPhysicsSystem builds it
		// from height, radius and centre as they are); a Collider is. So a body copying a
		// character undoes the entity's world scale: 1 / scale on each axis.
		Math::vec3 UnscaleFor(const World& world, Entity source)
		{
			Math::mat4 m(1.f);
			if (!world.TryGetWorldMatrix(source, m)) return Math::vec3(1.f);
			Math::vec3 inverse(1.f);
			for (int axis = 0; axis < 3; ++axis)
			{
				const float length = glm::length(Math::vec3(m[axis]));
				inverse[axis] = length > 1e-6f && std::isfinite(length) ? 1.f / length : 1.f;
			}
			return inverse;
		}

		bool HasQueryBodyChild(const World& world, Entity e)
		{
			bool found = false;
			world.ForEachChild(e, [&](Entity child) { found = found || IsClientQueryBody(world, child); });
			return found;
		}
	}

	bool IsClientQueryBody(const World& world, Entity entity)
	{
		const EntityName* name = world.IsAlive(entity) ? world.Read<EntityName>(entity) : nullptr;
		return name != nullptr && name->name == kQueryBodyName;
	}

	Entity FindClientQueryBody(const World& world, Entity source)
	{
		Entity found = kNullEntity;
		if (world.IsAlive(source))
			world.ForEachChild(source, [&](Entity child) { if (!found.IsValid() && IsClientQueryBody(world, child)) found = child; });
		return found;
	}

	std::size_t AddClientQueryBodies(World& world, Entity root, std::vector<Entity>& created)
	{
		if (!world.IsAlive(root)) return 0;
		std::vector<Entity> subtree;
		world.CollectSubtree(root, subtree);   // parents before children; collected first, so new bodies are not visited

		std::size_t added = 0;
		for (const Entity e : subtree)
		{
			if (IsClientQueryBody(world, e) || HasQueryBodyChild(world, e)) continue;
			const std::optional<Source> source = SourceShape(world, e);
			if (!source) continue;

			Transform local;   // identity under the entity it copies: it moves with it
			if (source->character) local.scale = UnscaleFor(world, e);
			const Entity body = world.CreateEntity();
			world.AddComponent(body, local);
			world.AddComponent(body, EntityName{ kQueryBodyName });
			world.SetParent(body, e, false);
			world.AddComponent(body, source->shape);
			RigidBody kinematic;
			kinematic.isKinematic = true;      // follows the Transform; never simulated, never pushed
			kinematic.useGravity = false;
			world.AddComponent(body, kinematic);
			created.push_back(body);
			++added;
		}
		return added;
	}

	void RemoveClientQueryBodies(World& world, std::span<const Entity> bodies)
	{
		for (const Entity body : bodies)
			if (IsClientQueryBody(world, body)) world.DestroyEntity(body);
	}
}
