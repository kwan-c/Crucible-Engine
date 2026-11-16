#pragma once
/*!
* @file			SceneVisibility.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			16 November 2026
*
* @brief		Editor scene visibility: hiding entities in the Scene view only (Unity's eye in
*				the Hierarchy). Shared by the Hierarchy, the Scene view and the Inspector.
*
* The state is the SceneHidden tag component (ECS/Components/Components.h). It is RuntimeOnly, so
* it is never saved in a scene or .cprefab, never part of an undo step, the Play snapshot or
* copy/paste, and never a prefab override. RenderExtractSystem passes it to the Renderer, which
* leaves the entity's meshes out of Scene-role viewports; the Game view and the built game are
* not affected.
*
* Hiding is not inherited: hiding a parent tags each of its descendants (includeChildren), so
* the renderer only ever checks the entity itself.
*
* Nothing here is an undo step and nothing marks the scene as changed.
*
* Play Mode: Stop rebuilds the world from the Play snapshot, which never holds the tag, so the
* editor remembers the hidden entities by scene guid at Play and hides them again after Stop
* (EditorManager::OnPlayStateChanged). Hiding or showing done while playing is not kept.
*
* SetHidden / ShowAll add and remove a component, which the World refuses while a Query is
* iterating - call them after the loop (the Hierarchy defers them to RunPendingAction).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ECS/Core/ECSCore.h"   // Entity

#include <cstddef>
#include <vector>

namespace Crucible {

	class World;

	namespace SceneVisibility {

		// Whether the entity itself is hidden in the Scene view. Only reads: safe mid-query.
		bool IsHidden(World const& world, Entity entity);

		// Hides or shows the entity - and, with includeChildren, everything under it.
		void SetHidden(World& world, Entity entity, bool hidden, bool includeChildren);

		// Every entity that is hidden right now. Builds a query: not for every frame.
		std::vector<Entity> ListHidden(World& world);

		// Shows every hidden entity again. Returns how many there were.
		std::size_t ShowAll(World& world);

	}
}
