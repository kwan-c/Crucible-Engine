/*************************************************************************
* \file:         PrefabBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         Internal calls behind spawning and destroying from scripts:
*                C# Prefab.Instantiate (ManagedScripts/Runtime/Prefab.cs) and
*                Entity.Destroy (ManagedScripts/Runtime/Entity.cs).
*
* \details
*    Instantiate builds the prefab immediately with Prefab::Instantiate (the
*    same call the editor uses to place one) and returns the new root. The
*    script system is Exclusive, so creating entities from inside it is
*    allowed. Scripts on the new entities start on the next frame.
*
*    Loaded prefabs are cached by asset guid and reloaded only when the
*    .cprefab file changes on disk, so spawning a bullet every frame does not
*    re-read the file.
*
*    Destroy only queues the request (EntityContext::QueueDestroy);
*    EntityScriptSystem destroys at the end of its Update.
*
*    Asset guids are 64-bit; they cross as two int halves because the
*    binding rules keep `long` out of internal calls.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "AssetPipe/Assetmanager.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Scene/Prefab.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		/*********************************************************************
		 \brief  A loaded prefab and the file time it was loaded at.
		 *********************************************************************/
		struct CachedPrefab
		{
			std::unique_ptr<Prefab> prefab;
			std::filesystem::file_time_type writeTime;
		};
		// Asset guid -> prefab (main thread). Function-local thread_local, created on first use: at
		// namespace scope MSVC builds one for every thread in the process, and threads still
		// running at exit leak theirs (see PendingDestroys() in EntityContext.cpp).
		std::map<uint64_t, CachedPrefab>& PrefabCache()
		{
			thread_local std::map<uint64_t, CachedPrefab> cache;
			return cache;
		}

		/*********************************************************************
		 \brief  The two int halves from C# back into one 64-bit guid.
		 *********************************************************************/
		uint64_t JoinId(int32_t low, int32_t high)
		{
			return (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | static_cast<uint32_t>(low);
		}

		/*********************************************************************
		 \brief  The prefab for an asset guid, loading it (again) if it is not
		         cached or the file changed since. nullptr if it cannot load.
		 *********************************************************************/
		const Prefab* GetPrefab(uint64_t assetId)
		{
			AssetManager* assets = EntityContext::GetAssets();
			if (!assets || !EntityContext::IsPrefabAsset(assetId))
				return nullptr;
			const AssetEntry* entry = assets->FindByGuid(xresource::instance_guid{assetId});
			if (!entry)
				return nullptr;
			std::error_code ec;
			const auto writeTime = std::filesystem::last_write_time(entry->absolutePath, ec);
			auto& cached = PrefabCache()[assetId];
			if (cached.prefab && !ec && cached.writeTime == writeTime)
				return cached.prefab.get();
			auto prefab = std::make_unique<Prefab>();
			if (!prefab->Load(entry->absolutePath))
			{
				CRU_USER_ERROR("[Script] Prefab '{}' failed to load (see the log)", entry->virtualPath);
				PrefabCache().erase(assetId);
				return nullptr;
			}
			cached.prefab = std::move(prefab);
			cached.writeTime = writeTime;
			return cached.prefab.get();
		}

		/*********************************************************************
		 \brief  C# Prefab.Instantiate. Builds the prefab, optionally under a
		         parent (parentIndex 0 = no parent), and returns its root.
		 \return 1 with the root's (index, generation), or 0 if the prefab
		         is missing, the parent is gone, or it is not Play.
		 *********************************************************************/
		int32_t ManagedPrefabInstantiate(int32_t idLow, int32_t idHigh, int32_t parentIndex, int32_t parentGeneration,
		                                 int32_t* outIndex, int32_t* outGeneration) noexcept
		{
			try
			{
				if (!outIndex || !outGeneration)
					return 0;
				*outIndex = 0;
				*outGeneration = 0;

				Entity parent = kNullEntity;
				if (parentIndex != 0 && !EntityContext::ResolveEntity(parentIndex, parentGeneration, parent))
					return 0; // the parent was destroyed

				const Prefab* prefab = GetPrefab(JoinId(idLow, idHigh));
				World* world = EntityContext::GetWorld();
				if (!prefab || !world)
					return 0;

				const Entity root = prefab->Instantiate(*world, parent, &EntityContext::EnsureSceneId); // parts that reference each other point at the new copy
				// ResolveEntity also confirms this is a Play session.
				Entity checked;
				if (!EntityContext::ResolveEntity(static_cast<int32_t>(root.index), static_cast<int32_t>(root.generation),
				                                  checked))
					return 0;
				*outIndex = static_cast<int32_t>(root.index);
				*outGeneration = static_cast<int32_t>(root.generation);
				return 1;
			}
			catch (const std::exception& error)
			{
				CRU_USER_ERROR("[Script] Prefab.Instantiate failed: {}", error.what());
				return 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Prefab.Name: the .cprefab's file name without extension, or
		         null if the asset is gone.
		 *********************************************************************/
		MonoString* ManagedPrefabGetName(int32_t idLow, int32_t idHigh) noexcept
		{
			try
			{
				AssetManager* assets = EntityContext::GetAssets();
				if (!assets)
					return nullptr;
				const AssetEntry* entry = assets->FindByGuid(xresource::instance_guid{JoinId(idLow, idHigh)});
				if (!entry)
					return nullptr;
				std::wstring wide;
				if (!ToUtf16(std::filesystem::path(entry->virtualPath).stem().string(), wide))
					return nullptr;
				return mono_string_new_utf16(mono_domain_get(), reinterpret_cast<const mono_unichar2*>(wide.data()),
				                             static_cast<int32_t>(wide.size()));
			}
			catch (...)
			{
				return nullptr;
			}
		}

		/*********************************************************************
		 \brief  C# Entity.Destroy(entity, delay). Queues the entity (and its
		         children) to be destroyed after `delay` seconds of Play time,
		         at the end of a frame.
		 \return 1 if queued, 0 if the entity is already gone or delay is NaN.
		 *********************************************************************/
		int32_t ManagedEntityDestroy(int32_t index, int32_t generation, float delay) noexcept
		{
			try
			{
				Entity entity;
				if (!std::isfinite(delay) || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				EntityContext::QueueDestroy(entity, delay > 0.0f ? delay : 0.0f);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}
	}

	Entity EntityContext::SpawnPrefab(uint64_t assetId)
	{
		try
		{
			const Prefab* prefab = GetPrefab(assetId);
			World* world = EntityContext::GetWorld();
			if (!prefab || !world)
				return kNullEntity;
			return prefab->Instantiate(*world, kNullEntity, &EntityContext::EnsureSceneId);
		}
		catch (const std::exception& error)
		{
			CRU_USER_ERROR("Spawning prefab {:X} failed: {}", assetId, error.what());
			return kNullEntity;
		}
	}

	void RegisterPrefabBindings()
	{
		mono_add_internal_call("Crucible.Prefab::InstantiateInternal",
		                       reinterpret_cast<const void*>(ManagedPrefabInstantiate));
		mono_add_internal_call("Crucible.Prefab::GetNameInternal", reinterpret_cast<const void*>(ManagedPrefabGetName));
		mono_add_internal_call("Crucible.Entity::DestroyInternal", reinterpret_cast<const void*>(ManagedEntityDestroy));
	}
}
#endif
