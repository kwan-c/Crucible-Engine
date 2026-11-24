/*************************************************************************
* \file:         EntityContext.cpp
* \author:       Tan Chun Kwang Xavier (main author),
*                Jean David Vidal Cruzado (EditorCallScope)
* \par:          Email: chunkwangxavier.tan@digipen.edu,
*                Email: jeandavidvidal.c@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         Implementation - see EntityContext.h. Also defines
*                ScriptBindings::SetWorld.
*
**************************************************************************/
#include "Scripting/Bindings/EntityContext.h"
#include "Scripting/ScriptBindings.h"
#include "ECS/World.h"
#include "Scene/SceneManager.h"
#include "Systems/PlayMode.h"
#include "AssetPipe/Assetmanager.h"
#include "Audio/AudioComponents.h"
#if CRUCIBLE_WITH_MONO
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Crucible::ScriptBindings
{
	namespace
	{
		// Set by SetWorld() from Core (main thread).
		thread_local World* world = nullptr;
		thread_local PlayMode* play = nullptr;
		thread_local SceneManager* scenes = nullptr;
		thread_local AssetManager* assets = nullptr;

		// Entity.Destroy requests waiting for their delay (main thread only).
		struct PendingDestroy
		{
			Entity entity;
			float remaining; // seconds of Play time left
		};
		// Function-local thread_local, created on first use: a namespace-scope thread_local with
		// a constructor is built by MSVC for EVERY thread that starts in the process (Jolt workers,
		// FMOD, the GPU driver, ...), and threads still running at exit never destroy it - each
		// left one leaked copy in the Debug leak report.
		std::vector<PendingDestroy>& PendingDestroys()
		{
			thread_local std::vector<PendingDestroy> list;
			return list;
		}

		// Audio.PlayClipAtPoint entities waiting for their sound to end.
		struct OneShotAudio
		{
			Entity entity;
			float waited = 0.0f;  // seconds before it started playing
			bool started = false; // its AudioSource has reported playing
		};
		std::vector<OneShotAudio>& OneShots()   // created on first use, see PendingDestroys()
		{
			thread_local std::vector<OneShotAudio> list;
			return list;
		}

		// C# Time: Playing time and frames since Play started.
		thread_local double playTime = 0.0;
		thread_local int32_t playFrames = 0;
		// Open EditorCallScopes (an Inspector [EditorButton] running a script method in Edit mode).
		thread_local int editorCalls = 0;

		// A one-shot that has not started after this long never will (missing clip).
		constexpr float OneShotStartTimeout = 2.0f;

		// Set by EntityScriptSystem. Plain statics (not thread_local): it may be
		// installed from whichever thread initializes systems. Every caller goes
		// through ResolveEntity first, which already fails off the main thread.
		EntityContext::ScriptLookup scriptLookup = nullptr;
		void* scriptLookupContext = nullptr;
	}

	void SetWorld(World* newWorld, PlayMode* newPlay, SceneManager* newScenes, AssetManager* newAssets)
	{
		world = newWorld;
		play = newPlay;
		scenes = newScenes;
		assets = newAssets;
		PendingDestroys().clear();
		OneShots().clear();
		playTime = 0.0;
		playFrames = 0;
	}

	namespace EntityContext
	{
		World* GetWorld()
		{
			return world;
		}

		AssetManager* GetAssets()
		{
			return assets;
		}

		SceneManager* GetScenes()
		{
			return scenes;
		}

		bool IsPrefabAsset(uint64_t assetId)
		{
			if (!assets || assetId == 0)
				return false;
			const AssetEntry* entry = assets->FindByGuid(xresource::instance_guid{assetId});
			return entry && std::filesystem::path(entry->virtualPath).extension() == ".cprefab";
		}

		bool IsAudioAsset(uint64_t assetId)
		{
			if (!assets || assetId == 0)
				return false;
			const AssetEntry* entry = assets->FindByGuid(xresource::instance_guid{assetId});
			return entry && entry->type == AssetType::Audio;
		}

		bool IsTextureAsset(uint64_t assetId)
		{
			if (!assets || assetId == 0)
				return false;
			const AssetEntry* entry = assets->FindByGuid(xresource::instance_guid{assetId});
			return entry && entry->type == AssetType::Texture;
		}

		bool IsFontAsset(uint64_t assetId)
		{
			if (!assets || assetId == 0)
				return false;
			const AssetEntry* entry = assets->FindByGuid(xresource::instance_guid{assetId});
			return entry && entry->type == AssetType::Font;
		}

		bool IsMaterialAsset(uint64_t assetId)
		{
			if (!assets || assetId == 0)
				return false;
			const AssetEntry* entry = assets->FindByGuid(xresource::instance_guid{assetId});
			return entry && entry->type == AssetType::Material;
		}

		bool IsModelAsset(uint64_t assetId)
		{
			if (!assets || assetId == 0)
				return false;
			const AssetEntry* entry = assets->FindByGuid(xresource::instance_guid{assetId});
			return entry && entry->type == AssetType::Model;
		}

		void QueueDestroy(Entity entity, float delaySeconds)
		{
			const auto existing = std::find_if(PendingDestroys().begin(), PendingDestroys().end(),
			                                   [&](const PendingDestroy& pending) { return pending.entity == entity; });
			if (existing != PendingDestroys().end())
				existing->remaining = std::min(existing->remaining, delaySeconds);
			else
				PendingDestroys().push_back({entity, delaySeconds});
		}

		void TakeDueDestroys(float deltaTime, std::vector<Entity>& due)
		{
			for (auto it = PendingDestroys().begin(); it != PendingDestroys().end();)
			{
				it->remaining -= deltaTime;
				if (it->remaining <= 0.0f)
				{
					due.push_back(it->entity);
					it = PendingDestroys().erase(it);
				}
				else
					++it;
			}
		}

		void ClearDestroyQueue()
		{
			PendingDestroys().clear();
		}

		void PruneDestroyQueue(const World& liveWorld)
		{
			std::erase_if(PendingDestroys(),
			              [&liveWorld](const PendingDestroy& pending) { return !liveWorld.IsAlive(pending.entity); });
		}

		bool InPlaySession()
		{
			return play && play->InSession();
		}

		void AdvancePlayClock(float deltaTime)
		{
			if (std::isfinite(deltaTime) && deltaTime > 0.0f)
				playTime += deltaTime;
			++playFrames;
		}

		void ResetPlayClock()
		{
			playTime = 0.0;
			playFrames = 0;
		}

		double PlayTime()
		{
			return playTime;
		}

		int32_t PlayFrameCount()
		{
			return playFrames;
		}

		void TrackOneShotAudio(Entity entity)
		{
			OneShots().push_back({entity});
		}

		void UpdateOneShotAudio(float deltaTime)
		{
			if (!world)
				return;
			for (auto it = OneShots().begin(); it != OneShots().end();)
			{
				const AudioSource* source = world->IsAlive(it->entity) ? world->Read<AudioSource>(it->entity) : nullptr;
				bool done = !source; // destroyed by a script, or its AudioSource removed
				if (source)
				{
					if (source->m_isPlaying)
						it->started = true;
					else if (it->started)
						done = true; // finished
					else if ((it->waited += deltaTime) > OneShotStartTimeout)
						done = true; // never started
				}
				if (done)
				{
					if (source)
						QueueDestroy(it->entity, 0.0f);
					it = OneShots().erase(it);
				}
				else
					++it;
			}
		}

		void ClearOneShotAudio()
		{
			OneShots().clear();
		}

		EditorCallScope::EditorCallScope() { ++editorCalls; }
		EditorCallScope::~EditorCallScope() { --editorCalls; }

		bool InEditorCall()
		{
			return editorCalls > 0;
		}

		bool ResolveEntity(int32_t index, int32_t generation, Entity& entity)
		{
			if (!world || (!InPlaySession() && !InEditorCall()))
				return false;
			entity = Entity{static_cast<uint32_t>(index), static_cast<uint32_t>(generation)};
			return world->IsAlive(entity);
		}

		bool FromSceneId(uint64_t id, Entity& entity)
		{
			if (!world || !scenes || id == 0)
				return false;
			entity = scenes->Current().EntityOf(xresource::instance_guid{id});
			return entity.IsValid() && world->IsAlive(entity);
		}

		uint64_t SceneIdOf(Entity entity)
		{
			if (!world || !scenes || !world->IsAlive(entity))
				return 0;
			return scenes->Current().IdOf(entity).m_Value;
		}

		uint64_t EnsureSceneId(Entity entity)
		{
			if (!world || !scenes || !world->IsAlive(entity))
				return 0;
			return scenes->EnsureId(entity).m_Value;
		}

		void SetScriptLookup(ScriptLookup lookup, void* context)
		{
			scriptLookup = lookup;
			scriptLookupContext = context;
		}

		void FindScripts(Entity entity, std::vector<RunningScript>& out)
		{
			out.clear();
			if (scriptLookup)
				scriptLookup(scriptLookupContext, entity, out);
		}
	}
}
#endif
