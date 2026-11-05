/*************************************************************************
* \file:         GamePrefabs.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.2
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
*                To make a prefab networked: create its .cprefab in the
*                editor, add an id below, and register it in
*                RegisterGameNetPrefabs.
*
*                The player (PR 11). Its id is reserved; NetInputSystem
*                spawns it for the host and for every client that joins, but
*                only once it is registered. To turn that on: save a mech
*                (the root with CharacterController and PlayerController,
*                its CameraOrbit pivot as a child) as Prefabs/Mech.cprefab
*                in the editor, then register it, in RegisterGameNetPrefabs:
*
*                  if (!table.Register(Id(GamePrefab::Player), {
*                      .name = "Player", .prefabPath = "Prefabs/Mech.cprefab",
*                      .replicated = { NetComponentId::TransformYawOnly },
*                      .serverOnly = { ComponentTypeId<CharacterController>(),
*                                      ComponentTypeId<PlayerController>() },
*                      .ownerLocalOnly = { ComponentTypeId<CameraOrbit>(),
*                                          ComponentTypeId<CameraComponent>() } }, error))
*                      return false;
*
*                  serverOnly: clients never move a player themselves; the
*                    host's controller and physics do (Guide 6.1).
*                  ownerLocalOnly: only the owner's pivot turns and its
*                    camera participates in rendering and movement sampling.
*                  Include ECS/Components/Components.h when enabling this example.
*                  A network scene should not also contain a placed mech: on
*                  every machine it would be a local player too.
*
*                Any other prefab works the same way - an AI enemy, a prop,
*                a vehicle: nothing here knows what it is. An example for an
*                enemy built in the editor (navigation, state graph, physics,
*                scripts), once its .cprefab exists (pick the next free id):
*
*                  if (!table.Register(2, {
*                      .name = "Grunt", .prefabPath = "Prefabs/Grunt.cprefab",
*                      .replicated = { NetComponentId::TransformYawOnly },
*                      .serverOnly = { ComponentTypeId<FlowFieldAgent>(),
*                                      ComponentTypeId<StateGraphAgent>(),
*                                      ComponentTypeId<ScriptComponent>(),
*                                      ComponentTypeId<CharacterController>() } }, error))
*                      return false;
*
*                  serverOnly: the host alone runs its AI, navigation, scripts
*                    and physics; clients draw the replicated pose,
*                    interpolated. List every component that decides or moves
*                    anything (RigidBody and Collider for physics bodies), or
*                    a client would run it too and fight the snapshots.
*                  Spawning stays the game's: RequestSpawn(id, kServerPeer,
*                    position) on the host (C#, later); destroying the entity
*                    despawns it everywhere.
*                  Values a script needs on clients (health, say) go into a
*                    native component listed in .replicated, with a
*                    serializer (NetComponentId::FirstGameId and up), until
*                    script fields can be replicated directly.
*
*                Core calls RegisterGameNetPrefabs once at startup; a
*                mistake here is reported then and refuses every session.
*
* \history
*    -v1.0       Networking Guide 13.2, 18.3 issue 6 (PR 7)
*    -v1.1       Player = 1 reserved for NetInputSystem's auto-spawn (PR 11)
*    -v1.2       a generic example for any other prefab (an AI enemy); no id
*                reserved for it (PR 12)
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
		Player = 1,   // spawned per peer by NetInputSystem once registered (see above)
	};

	constexpr Net::NetPrefabId Id(GamePrefab prefab) noexcept { return static_cast<Net::NetPrefabId>(prefab); }

	/// Registers every game network prefab. False with the reason on the first refused one.
	inline bool RegisterGameNetPrefabs([[maybe_unused]] NetPrefabTable& table, [[maybe_unused]] std::string& error)
	{
		return true;
	}
}
