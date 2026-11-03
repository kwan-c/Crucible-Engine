/*************************************************************************
* \file:         GamePrefabs.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The game's network prefabs: their explicit, permanent
*                NetPrefabIds and their registration.
*
* \details
*                Rules (Networking Guide 18.3, issue 6):
*                  - every id is written here, once; registration order
*                    never decides an id;
*                  - never renumber; a retired prefab keeps its id as a
*                    comment, and the id is never reused.
*
*                Empty for now: the project has no prefab assets yet. To
*                make one networked: create its .cprefab in the editor, add
*                an id below, and register it in RegisterGameNetPrefabs, e.g.
*
*                  table.Register(Id(GamePrefab::Player), {
*                      .name = "Player", .prefabPath = "Prefabs/Mech.cprefab",
*                      .replicated = { NetComponentId::TransformYawOnly },
*                      .serverOnly = { ComponentTypeId<CharacterController>() },
*                      .ownerLocalOnly = { ComponentTypeId<CameraComponent>() } }, error)
*
*                Core calls RegisterGameNetPrefabs once at startup; a
*                mistake here is reported then and refuses every session.
*
* \history
*    -v1.0       Networking Guide 13.2, 18.3 issue 6 (PR 7)
*
**************************************************************************/
#pragma once

#include <string>

#include "Network/NetPrefabTable.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	/// Permanent network prefab ids. Append only.
	enum class GamePrefab : Net::NetPrefabId
	{
		// Player = 1,   (example: first entry, once the player prefab exists)
	};

	constexpr Net::NetPrefabId Id(GamePrefab prefab) noexcept { return static_cast<Net::NetPrefabId>(prefab); }

	/// Registers every game network prefab. False with the reason on the first refused one.
	inline bool RegisterGameNetPrefabs([[maybe_unused]] NetPrefabTable& table, [[maybe_unused]] std::string& error)
	{
		return true;
	}
}
