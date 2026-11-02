/*************************************************************************
* \file:         EntityContext.h
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
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
#include <string>
#include <vector>
#include "ECS/Core/ECSCore.h"   // Entity

namespace Crucible
{
	class World;
	class AssetManager;
	class SceneManager;

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
		 \brief  The scene manager (Scene.Load), or nullptr.
		 *********************************************************************/
		SceneManager* GetScenes();

		/*********************************************************************
		 \brief  true if `assetId` is an indexed .cprefab asset.
		 *********************************************************************/
		bool IsPrefabAsset(uint64_t assetId);

		/*********************************************************************
		 \brief  true if `assetId` is an indexed audio file (AssetType::Audio).
		 *********************************************************************/
		bool IsAudioAsset(uint64_t assetId);

		/*********************************************************************
		 \brief  true if `assetId` is an indexed image (AssetType::Texture).
		 *********************************************************************/
		bool IsTextureAsset(uint64_t assetId);

		/*********************************************************************
		 \brief  true if `assetId` is an indexed font (AssetType::Font:
		         .ttf / .otf / .cfont).
		 *********************************************************************/
		bool IsFontAsset(uint64_t assetId);

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

		// --- UI events (C# UIButton.WasClicked, UISlider.WasChanged, UI.WasActionTriggered) //

		/*********************************************************************
		 \brief  One UI event this frame, in the order UISystem sent them.
		 *********************************************************************/
		struct UIEvent
		{
			enum class Kind : int32_t
			{
				ButtonClicked = 0, // source = the button
				SliderChanged = 1, // source = the slider, value = its new value
				CustomAction = 2,  // source = the button, actionId = its Action Id
			};
			Kind kind = Kind::ButtonClicked;
			Entity source{};
			float value = 0.0f;
			std::string actionId;
		};

		/*********************************************************************
		 \brief  This frame's UI events (filled by the UIEvents listeners,
		         before scripts run). EntityScriptSystem hands each to the
		         scripts' OnButtonClicked / OnSliderChanged / OnUIAction.
		 *********************************************************************/
		const std::vector<UIEvent>& UIEvents();

		/*********************************************************************
		 \brief  Forgets this frame's UI events, so WasClicked etc. read true
		         for one frame only. EntityScriptSystem calls it at the end of
		         every Update. Defined in Bindings/UIBindings.cpp, next to the
		         event listeners that fill the list.
		 *********************************************************************/
		void ClearUIEvents();

		// --- Physics events (C# OnCollisionEnter/Exit, OnTriggerEnter/Exit) -- //

		/*********************************************************************
		 \brief  One PhysicsEvents event (Physics/PhysicsEvents.h), as sent.
		 *********************************************************************/
		struct ContactEvent
		{
			enum class Kind : int32_t
			{
				CollisionEnter = 0, // a, b = the two bodies; point / normal (a -> b) / speed set
				CollisionExit = 1,  // a, b
				TriggerEnter = 2,   // a = the trigger, b = what entered
				TriggerExit = 3,    // a = the trigger, b = what left
			};
			Kind kind = Kind::CollisionEnter;
			Entity a{}, b{};
			float point[3] = {0.0f, 0.0f, 0.0f};
			float normal[3] = {0.0f, 1.0f, 0.0f};
			float impactSpeed = 0.0f;
		};

		/*********************************************************************
		 \brief  Physics events received since scripts last ran. ECSPhysicsSystem
		         sends them after its step (PostUpdate, after scripts), so
		         EntityScriptSystem hands them out at the start of the next
		         frame's Update and then clears them (ClearCollisionEvents).
		         Defined in Bindings/PhysicsBindings.cpp, next to the listeners.
		 *********************************************************************/
		const std::vector<ContactEvent>& CollisionEvents();
		void ClearCollisionEvents();

		// --- Entity.SetActive records ------------------------------------- //

		/*********************************************************************
		 \brief  Forgets every SetActive(false) record (Play stopped, scene
		         changed - the snapshot / new scene has the real on/off state).
		         Defined in Bindings/EnableBindings.cpp.
		 *********************************************************************/
		void ClearActiveStates();

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
		 \brief  The entity's scene id, issuing one if it has none yet
		         (SceneManager::EnsureId) - for spawned prefab parts whose
		         script fields must point at each other. 0 for a dead entity.
		 *********************************************************************/
		uint64_t EnsureSceneId(Entity entity);

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
