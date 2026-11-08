/*************************************************************************
* \file:         GamePrefabs.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.3
* \brief
*                Network prefabs registered in CODE (optional). The normal
*                way to make a prefab networked needs nothing here.
*
* \details
*                The normal way (Networking Phase 14): in the editor, add
*                a NetworkObjectAuthoring component to the prefab's ROOT,
*                choose what replicates and what is server-only or
*                owner-local, and save the .cprefab. Before every Host or
*                Client session the engine finds every such prefab and
*                registers it (NetObjectProfiles.h, NetContent::Prepare),
*                with an id derived from the asset GUIDs. The player is the
*                prefab with "spawnPerPlayer" set.
*
*                This file stays for content that must be registered from
*                code (engine-side tests, tools). Rules for it:
*                  - every id is written here, once; registration order
*                    never decides an id;
*                  - never renumber; a retired prefab keeps its id as a
*                    comment, and the id is never reused;
*                  - discovered prefabs are numbered after the highest id
*                    registered here.
*
*                  if (!table.Register(1, {
*                      .name = "Grunt", .prefabPath = "Prefabs/Grunt.cprefab",
*                      .replicated = { NetComponentId::TransformYawOnly },
*                      .serverOnly = { ComponentTypeId<FlowFieldAgent>(),
*                                      ComponentTypeId<StateGraphAgent>(),
*                                      ComponentTypeId<ScriptComponent>(),
*                                      ComponentTypeId<CharacterController>() } }, error))
*                      return false;
*
*                Do not register a prefab both here and with
*                NetworkObjectAuthoring: it would be two network prefabs.
*
*                Core calls RegisterGameNetPrefabs once at startup; a
*                mistake here is reported then and refuses every session.
*
* \history
*    -v1.0       code-registered network prefabs with fixed ids
*    -v1.1       Player = 1 reserved for NetInputSystem's auto-spawn
*    -v1.2       a generic example for any other prefab (an AI enemy); no id
*                reserved for it
*    -v1.3       optional: authored prefabs are discovered; the reserved
*                Player id is gone (the player is the spawnPerPlayer
*                prefab) (Networking Phase 14.2)
*
**************************************************************************/
#pragma once

#include <string>

#include "Network/NetPrefabTable.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	/// Permanent ids of prefabs registered in code. Append only. (Player = 1 was retired in Phase
	/// 14.2: the player is now the authored prefab with spawnPerPlayer.)
	enum class GamePrefab : Net::NetPrefabId
	{
	};

	constexpr Net::NetPrefabId Id(GamePrefab prefab) noexcept { return static_cast<Net::NetPrefabId>(prefab); }

	/// Registers every code-defined network prefab. False with the reason on the first refused one.
	inline bool RegisterGameNetPrefabs([[maybe_unused]] NetPrefabTable& table, [[maybe_unused]] std::string& error)
	{
		return true;
	}
}
