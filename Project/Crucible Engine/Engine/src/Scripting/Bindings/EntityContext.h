/*************************************************************************
* \file:         EntityContext.h
* \author:       <author>
* \par:          Email: <email>
* \date          1 November 2026
* \version       1.0
* \brief         INTERNAL: the world, Play state and scene that entity
*                bindings (Transform, RigidBody, Entity, ...) work against.
*
* \details
*    Set once by ScriptBindings::SetWorld (through
*    ScriptRuntime::SetWorldBinding). Every binding that receives an
*    (index, generation) from C# should call ResolveEntity before touching
*    the world, so destroyed entities and calls outside Play are rejected.
*
*    The pointers are thread_local: a call from any thread other than the
*    one that set them (e.g. a GC finalizer) sees nullptr and fails.
*
*    Only include this from .cpp files inside Engine/src/Scripting.
*
**************************************************************************/
#pragma once
#include <cstdint>
#include <vector>
#include "ECS/Core/ECSCore.h"   // Entity

namespace Crucible
{
	class World;
	class AssetManager;

	namespace ScriptBindings::EntityContext
	{
		/*********************************************************************
		 \brief  The world scripts act on, or nullptr before SetWorld().
		 *********************************************************************/
		World* GetWorld();

		/*********************************************************************
		 \brief  The asset manager (prefab fields are asset guids), or nullptr.
		 *********************************************************************/
		AssetManager* GetAssets();

		/*********************************************************************
		 \brief  true if `assetId` is an indexed .cprefab asset.
		 *********************************************************************/
		bool IsPrefabAsset(uint64_t assetId);

		/*********************************************************************
		 \brief  true if `assetId` is an indexed audio file (AssetType::Audio).
		 *********************************************************************/
		bool IsAudioAsset(uint64_t assetId);

		// --- Destroy queue (C# Entity.Destroy) ----------------------------- //
		// Destroys are queued, not done on the spot: the entity stays valid for
		// the rest of the frame, like Unity. EntityScriptSystem applies them at
		// the end of its Update.

		/*********************************************************************
		 \brief  Queues `entity` (and its children) for destruction after
		         `delaySeconds` of Play time; 0 = at the end of this frame.
		         Queuing an entity again keeps the earlier deadline.
		 *********************************************************************/
		void QueueDestroy(Entity entity, float delaySeconds);

		/*********************************************************************
		 \brief  Counts every queued delay down by `deltaTime` and moves the
		         ones that are due into `due` (removing them from the queue).
		 *********************************************************************/
		void TakeDueDestroys(float deltaTime, std::vector<Entity>& due);

		/*********************************************************************
		 \brief  Forgets every queued destroy (Play stopped, scene changed).
		 *********************************************************************/
		void ClearDestroyQueue();

		/*********************************************************************
		 \brief  true during a Play session (Playing or Paused).
		 *********************************************************************/
		bool InPlaySession();

		// --- Play clock (C# Time) ------------------------------------------ //

		/*********************************************************************
		 \brief  Adds one Playing frame of `deltaTime` seconds to the play
		         clock. EntityScriptSystem calls it once per Playing frame,
		         before any OnUpdate, so Time.ElapsedTime includes the current frame.
		 *********************************************************************/
		void AdvancePlayClock(float deltaTime);

		/*********************************************************************
		 \brief  Back to zero (a new Play session, Stop, scene change).
		 *********************************************************************/
		void ResetPlayClock();

		/*********************************************************************
		 \brief  Seconds of Playing time since Play started (Pause excluded).
		 *********************************************************************/
		double PlayTime();

		/*********************************************************************
		 \brief  Playing frames since Play started.
		 *********************************************************************/
		int32_t PlayFrameCount();

		// --- One-shot audio (C# Audio.PlayClipAtPoint) ---------------------- //

		/*********************************************************************
		 \brief  Remembers a temporary "One Shot Audio" entity so it can be
		         destroyed once its AudioSource has finished.
		 *********************************************************************/
		void TrackOneShotAudio(Entity entity);

		/*********************************************************************
		 \brief  Queues a destroy for every tracked one-shot whose sound has
		         finished (its AudioSource reported playing, then stopped), or
		         that never started within a couple of seconds (bad clip).
		         EntityScriptSystem calls it once per Playing frame.
		 *********************************************************************/
		void UpdateOneShotAudio(float deltaTime);

		/*********************************************************************
		 \brief  Forgets every tracked one-shot (Play stopped, scene changed).
		 *********************************************************************/
		void ClearOneShotAudio();

		/*********************************************************************
		 \brief  Turns the (index, generation) a script passed in into a live
		         Entity. Fails outside a Play session, before SetWorld(), or if
		         the entity was destroyed (its generation no longer matches,
		         e.g. after a scene reload).
		 *********************************************************************/
		bool ResolveEntity(int32_t index, int32_t generation, Entity& entity);

		/*********************************************************************
		 \brief  The live entity saved under a scene id (Scene::EnsureId), for
		         Inspector-assigned entity fields.
		 \return false for id 0, an id the open scene does not have, or an
		         entity that is no longer alive.
		 \note   Searches the scene's id map - call it when a script is created
		         or a field is edited, not every frame.
		 *********************************************************************/
		bool FromSceneId(uint64_t id, Entity& entity);

		/*********************************************************************
		 \brief  The scene id an entity is saved under (Scene::IdOf), or 0 if it
		         has none yet (e.g. spawned during Play). Never issues a new id.
		 *********************************************************************/
		uint64_t SceneIdOf(Entity entity);

		/*********************************************************************
		 \brief  Finds the GC handle of the C# script running on an entity
		         (0 = none). `context` is passed back unchanged.
		 *********************************************************************/
		using ScriptLookup = uint32_t (*)(void* context, Entity entity);

		/*********************************************************************
		 \brief  Installs (or clears, with nullptr) the script lookup.
		         EntityScriptSystem sets it in Init and clears it in Shutdown.
		 *********************************************************************/
		void SetScriptLookup(ScriptLookup lookup, void* context);

		/*********************************************************************
		 \brief  GC handle of the script on `entity`, or 0 if it has none, it
		         failed, or no lookup is installed. The script may not have had
		         OnCreate yet (see EntityScriptSystem step 5).
		 *********************************************************************/
		uint32_t FindScriptHandle(Entity entity);
	}
}
