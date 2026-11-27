/*************************************************************************
* \file:         NetSceneObjects.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          7 November 2026
* \version       1.1
* \brief
*                Scene objects: the network objects a loaded scene already
*                contains on every machine, and the hash that proves two
*                machines loaded the same ones.
*
* \details
*                A scene object is an entity of the loaded scene (it may sit
*                under ordinary, non-networked scene entities) that
*                  - carries NetworkObjectAuthoring with allowScenePlacement,
*                  - is not inside another network object, and
*                  - has a persistent scene id (Scene::IdOf: it was saved).
*                Its replication profile:
*                  - the root of an instance of a discovered network prefab
*                    whose NetworkObjectAuthoring equals the prefab's: the
*                    prefab's NetPrefabId;
*                  - otherwise (Networking Phase 14.7) - a scene-only object,
*                    or an instance whose settings were overridden - a scene
*                    profile built from its own authoring (checked like the
*                    Inspector's CheckAuthoring). Identical scene profiles
*                    share one id; ids follow the content's highest id, in
*                    scene-id order, so equal scenes give equal ids. They are
*                    session-local and never saved.
*                Host and client run the same scan at the start of their
*                session, before anything has run on it, so both see the
*                scene exactly as it was loaded.
*
*                The scene hash covers, in scene-id order, each object's
*                scene id and profile and every component of every entity
*                of its subtree as EntityIO writes them (instance overrides,
*                materials, child entities, values): exactly the authored
*                data each machine keeps locally instead of receiving it.
*                Two machines whose hashes differ would show different
*                objects, so the client is refused (SceneMismatch) before it
*                binds anything. The host takes its hash once, when its
*                session starts, so later changes (moves, deaths) never make
*                a late joiner look incompatible.
*
*                Placed players (Networking Phase 14.6): an instance root of
*                the spawnPerPlayer prefab is never a scene object - every
*                peer gets its own spawned player instead. The scan lists
*                them apart (players); in a session each machine removes
*                them and the host spawns players where they stood. They are
*                not in the hash.
*
*                Skipped, with a reason (the editor shows the same ones),
*                and left as ordinary local entities on every machine:
*                  - its own settings are invalid (CheckAuthoring's first
*                    problem is the reason), or ask for spawnPerPlayer (only a
*                    prefab can be the player);
*                  - more distinct scene profiles than prefab ids are left;
*                  - an entity the scene has no id for (never saved);
*                  - one with allowScenePlacement off.
*                A skipped object is not in the hash.
*
* \history
*    -v1.0       Networking Phase 14.5
*                placed players listed apart (Networking Phase 14.6)
*                scene-only objects, per-instance profiles (Phase 14.7)
*    -v1.1       the scene's player prefab: the one its placed players are instances of
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ECS/Core/ECSCore.h"
#include "EngineExport.h"
#include "Network/NetPrefabTable.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	class NetContent;
	class Scene;
	class World;

	/// One network object of the loaded scene.
	struct SceneObject
	{
		Entity           entity = kNullEntity;
		std::uint64_t    sceneId = 0;                     // persistent: the same on every machine
		Net::NetPrefabId profile = Net::kInvalidNetPrefab;   // its network prefab's id
	};

	/// A scene profile the scan assigned (NetContent::AddSceneProfile registers it).
	struct SceneProfile
	{
		Net::NetPrefabId id = Net::kInvalidNetPrefab;
		NetPrefabDesc    desc;
	};

	struct SceneObjectScan
	{
		std::vector<SceneObject> objects;   // in scene-id order
		std::vector<SceneObject> players;   // placed instances of a spawnPerPlayer prefab, in scene-id order
		Net::NetPrefabId         playerPrefab = Net::kInvalidNetPrefab;   // the prefab they are instances of (the scene's player)
		std::vector<SceneProfile> profiles; // the scene profiles its objects use, in id order
		std::uint64_t            hash = 0;  // over the objects and their subtrees (never 0)
		std::vector<std::string> skipped;   // one line per NetworkObjectAuthoring left out, with why
	};

	/// The scene objects of `world` (see the file notes). Read-only: the scene profiles are only
	/// assigned (call NetContent::ForgetSceneProfiles first, so the ids start above the prefabs).
	ENGINE_API SceneObjectScan ScanSceneObjects(World& world, const Scene& scene, const NetContent& content);
}
