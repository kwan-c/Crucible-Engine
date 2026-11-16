/*!
* @file			SceneVisibility.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			16 November 2026
*
* @brief		Editor scene visibility - see SceneVisibility.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "SceneVisibility.h"

#include "ECS/World.h"
#include "ECS/Components/Components.h"

#include <vector>

namespace Crucible::SceneVisibility {

	namespace {
		// `root` and everything under it, parents before children.
		void Collect(World const& world, Entity root, std::vector<Entity>& out) {
			out.push_back(root);
			world.ForEachChild(root, [&](Entity child) { Collect(world, child, out); });
		}

		void Apply(World& world, Entity entity, bool hidden) {
			if (!world.IsAlive(entity)) { return; }
			bool const isHidden = world.HasComponent<SceneHidden>(entity);
			if (hidden && !isHidden)      { world.AddComponent<SceneHidden>(entity); }
			else if (!hidden && isHidden) { world.RemoveComponent<SceneHidden>(entity); }
		}
	}

	bool IsHidden(World const& world, Entity entity) {
		return world.IsAlive(entity) && world.HasComponent<SceneHidden>(entity);
	}

	void SetHidden(World& world, Entity entity, bool hidden, bool includeChildren) {
		if (!world.IsAlive(entity)) { return; }
		if (!includeChildren) { Apply(world, entity, hidden); return; }

		// Listed first: adding / removing a component moves an entity to another archetype, so
		// the tree is not walked while it changes.
		std::vector<Entity> all;
		Collect(world, entity, all);
		for (Entity const e : all) { Apply(world, e, hidden); }
	}

	std::vector<Entity> ListHidden(World& world) {
		std::vector<Entity> hidden;
		auto query = world.CreateQuery<const SceneHidden>();
		query.ForEach([&hidden](Entity entity, SceneHidden const&) { hidden.push_back(entity); });
		return hidden;
	}

	std::size_t ShowAll(World& world) {
		// Listed first, for the same reason (and a Query must not be iterating during the change).
		std::vector<Entity> const hidden = ListHidden(world);
		for (Entity const e : hidden) { Apply(world, e, false); }
		return hidden.size();
	}

}
