/*************************************************************************
* \file:         SceneBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the C# Scene class
*                (ManagedScripts/Runtime/Scene.cs).
*
* \details
*    Scene.Load calls SceneManager::RequestLoad, the same path the UI's
*    "Load Scene" button uses. During Play that is a runtime level switch:
*    it happens at the next frame's safe point (never in the middle of a
*    system), every script in the old scene gets OnDestroy, the new scene's
*    scripts start, and Stop still returns to the scene open at Play.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "AssetPipe/Assetmanager.h"
#include "Diagnostics/Logger.h"
#include "Scene/SceneManager.h"
#include "Resources/ResourceManager.h"
#if CRUCIBLE_WITH_MONO
#include <filesystem>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		/*********************************************************************
		 \brief  C# Scene.Load("Scenes/Level2.scene"): a path inside Assets.
		 \return 1 if queued; 0 (with a log line) if the file is not a scene,
		         does not exist, or it is not Play.
		 *********************************************************************/
		int32_t LoadScenePath(MonoString* path, bool bypassLoadGate) noexcept
		{
			try
			{
				GcRoot root(reinterpret_cast<MonoObject*>(path));
				std::string virtualPath;
				SceneManager* scenes = EntityContext::GetScenes();
				AssetManager* assets = EntityContext::GetAssets();
				if (!scenes || !assets || !EntityContext::InPlaySession() || !root.Get() ||
				    !FromManagedString(reinterpret_cast<MonoString*>(root.Get()), virtualPath) || virtualPath.empty())
					return 0;
				if (std::filesystem::path(virtualPath).extension() != ".scene")
				{
					CRU_USER_WARN("[Script] Scene.Load: '{}' is not a .scene file", virtualPath);
					return 0;
				}
				if (!scenes->RequestLoad(assets->Resolve(virtualPath), bypassLoadGate))
				{
					CRU_USER_WARN("[Script] Scene.Load: '{}' was not found under Assets", virtualPath);
					return 0;
				}
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/// C# Scene.Load: through the loading screen, if there is one.
		int32_t ManagedSceneLoad(MonoString* path) noexcept { return LoadScenePath(path, false); }

		/// C# Scene.LoadImmediate: straight away, past the loading screen.
		int32_t ManagedSceneLoadImmediate(MonoString* path) noexcept { return LoadScenePath(path, true); }

		/*********************************************************************
		 \brief  C# Scene.ClaimLoads(bool): the loading screen takes (1) or
		         gives back (0) every runtime scene change
		         (SceneManager::SetLoadGate).
		 *********************************************************************/
		void ManagedSceneClaimLoads(int32_t claimed) noexcept
		{
			if (SceneManager* scenes = EntityContext::GetScenes())
				scenes->SetLoadGate(claimed != 0);
		}

		/// C# Scene.HasHeldLoad: a scene change is waiting for the loading screen.
		int32_t ManagedSceneHasHeldLoad() noexcept
		{
			SceneManager* scenes = EntityContext::GetScenes();
			return scenes && scenes->HasHeldLoad() ? 1 : 0;
		}

		/// C# Scene.HeldLoadSerial: changes when a newer scene change replaces the held one.
		int32_t ManagedSceneHeldLoadSerial() noexcept
		{
			SceneManager* scenes = EntityContext::GetScenes();
			return scenes ? static_cast<int32_t>(scenes->HeldLoadSerial()) : 0;
		}

		/*********************************************************************
		 \brief  C# Scene.BeginPreloadHeld: queues the held scene's assets
		         (ResourceManager::BeginPreload).
		 \return How many were queued; 0 if there is nothing to preload (a New
		         scene, or all loaded already) or the file can't be read - the
		         load itself then reports why.
		 *********************************************************************/
		int32_t ManagedSceneBeginPreloadHeld() noexcept
		{
			try
			{
				SceneManager* scenes = EntityContext::GetScenes();
				ResourceManager* resources = scenes ? scenes->Resources() : nullptr;
				std::string text;
				if (!resources || !scenes->ReadHeldScene(text))
					return 0;
				return static_cast<int32_t>(resources->BeginPreload(text));
			}
			catch (...)
			{
				return 0;
			}
		}

		/// C# Scene.ReleaseHeldLoad: lets the held scene change through (next frame).
		void ManagedSceneReleaseHeldLoad() noexcept
		{
			if (SceneManager* scenes = EntityContext::GetScenes())
				scenes->ReleaseHeldLoad();
		}

		/*********************************************************************
		 \brief  C# Scene.Reload: loads the open scene's file again (a level
		         restart). 0 if the scene has never been saved to a file.
		 *********************************************************************/
		int32_t ManagedSceneReload() noexcept
		{
			try
			{
				SceneManager* scenes = EntityContext::GetScenes();
				if (!scenes || !EntityContext::InPlaySession() || !scenes->HasPath())
					return 0;
				return scenes->RequestLoad(scenes->CurrentPath()) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Scene.BeginPreload("Scenes/Level2.scene"): queues the
		         assets that scene uses (ResourceManager::BeginPreload). The
		         scene is read through the ResourceManager, so this works in
		         the packaged game too.
		 \return How many assets were queued (0 = all loaded already), or -1
		         (with a log line) if the scene can't be found or it is not
		         Play.
		 *********************************************************************/
		int32_t ManagedSceneBeginPreload(MonoString* path) noexcept
		{
			try
			{
				GcRoot root(reinterpret_cast<MonoObject*>(path));
				std::string virtualPath;
				SceneManager* scenes = EntityContext::GetScenes();
				ResourceManager* resources = scenes ? scenes->Resources() : nullptr;
				if (!resources || !EntityContext::InPlaySession() || !root.Get() ||
				    !FromManagedString(reinterpret_cast<MonoString*>(root.Get()), virtualPath) || virtualPath.empty())
					return -1;
				const AssetId scene = resources->GetIdForPath(virtualPath);
				std::string text;
				if (!resources->IsScene(scene) || !resources->ReadScene(scene, text))
				{
					CRU_USER_WARN("[Script] Scene.BeginPreload: '{}' is not a scene in the project", virtualPath);
					return -1;
				}
				return static_cast<int32_t>(resources->BeginPreload(text));
			}
			catch (...)
			{
				return -1;
			}
		}

		/*********************************************************************
		 \brief  C# Scene.StepPreload(budgetMs): loads queued assets for up
		         to budgetMs milliseconds. Returns progress, 0-1.
		 *********************************************************************/
		float ManagedSceneStepPreload(float budgetMs) noexcept
		{
			try
			{
				SceneManager* scenes = EntityContext::GetScenes();
				ResourceManager* resources = scenes ? scenes->Resources() : nullptr;
				if (!resources || !EntityContext::InPlaySession())
					return 1.0f;
				return resources->StepPreload(static_cast<double>(budgetMs > 0.0f ? budgetMs : 0.0f));
			}
			catch (...)
			{
				return 1.0f;
			}
		}

		/*********************************************************************
		 \brief  C# Scene.PreloadProgress: 0-1, 1 when nothing is queued.
		 *********************************************************************/
		float ManagedSceneGetPreloadProgress() noexcept
		{
			SceneManager* scenes = EntityContext::GetScenes();
			ResourceManager* resources = scenes ? scenes->Resources() : nullptr;
			return resources ? resources->PreloadProgress() : 1.0f;
		}

		/*********************************************************************
		 \brief  C# Scene.CancelPreload: drops what is still queued.
		 *********************************************************************/
		void ManagedSceneCancelPreload() noexcept
		{
			SceneManager* scenes = EntityContext::GetScenes();
			if (ResourceManager* resources = scenes ? scenes->Resources() : nullptr)
				resources->CancelPreload();
		}

		/*********************************************************************
		 \brief  C# Scene.Name: the open scene's name, or null.
		 *********************************************************************/
		MonoString* ManagedSceneGetName() noexcept
		{
			try
			{
				SceneManager* scenes = EntityContext::GetScenes();
				if (!scenes)
					return nullptr;
				std::wstring wide;
				if (!ToUtf16(scenes->Current().Name(), wide))
					return nullptr;
				return mono_string_new_utf16(mono_domain_get(), reinterpret_cast<const mono_unichar2*>(wide.data()),
				                             static_cast<int32_t>(wide.size()));
			}
			catch (...)
			{
				return nullptr;
			}
		}
	}

	namespace
	{
		/*********************************************************************
		 \brief  C# Scene.DontDestroyOnLoad(entity). 1 if marked; 0 for a
		         dead entity, a child, or outside Play (SceneManager logs why).
		 *********************************************************************/
		int32_t ManagedSceneDontDestroyOnLoad(int32_t index, int32_t generation) noexcept
		{
			try
			{
				Entity entity;
				SceneManager* scenes = EntityContext::GetScenes();
				if (!scenes || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				return scenes->DontDestroyOnLoad(entity) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Scene.IsDontDestroyOnLoad(entity).
		 *********************************************************************/
		int32_t ManagedSceneIsDontDestroyOnLoad(int32_t index, int32_t generation) noexcept
		{
			try
			{
				Entity entity;
				SceneManager* scenes = EntityContext::GetScenes();
				if (!scenes || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				return scenes->IsPersistent(entity) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}
	}

	void RegisterSceneBindings()
	{
		mono_add_internal_call("Crucible.Scene::DontDestroyOnLoadInternal",
		                       reinterpret_cast<const void*>(ManagedSceneDontDestroyOnLoad));
		mono_add_internal_call("Crucible.Scene::IsDontDestroyOnLoadInternal",
		                       reinterpret_cast<const void*>(ManagedSceneIsDontDestroyOnLoad));
		mono_add_internal_call("Crucible.Scene::LoadInternal", reinterpret_cast<const void*>(ManagedSceneLoad));
		mono_add_internal_call("Crucible.Scene::ReloadInternal", reinterpret_cast<const void*>(ManagedSceneReload));
		mono_add_internal_call("Crucible.Scene::GetNameInternal", reinterpret_cast<const void*>(ManagedSceneGetName));
		mono_add_internal_call("Crucible.Scene::BeginPreloadInternal", reinterpret_cast<const void*>(ManagedSceneBeginPreload));
		mono_add_internal_call("Crucible.Scene::StepPreloadInternal", reinterpret_cast<const void*>(ManagedSceneStepPreload));
		mono_add_internal_call("Crucible.Scene::GetPreloadProgressInternal", reinterpret_cast<const void*>(ManagedSceneGetPreloadProgress));
		mono_add_internal_call("Crucible.Scene::CancelPreloadInternal", reinterpret_cast<const void*>(ManagedSceneCancelPreload));
		mono_add_internal_call("Crucible.Scene::LoadImmediateInternal", reinterpret_cast<const void*>(ManagedSceneLoadImmediate));
		mono_add_internal_call("Crucible.Scene::ClaimLoadsInternal", reinterpret_cast<const void*>(ManagedSceneClaimLoads));
		mono_add_internal_call("Crucible.Scene::HasHeldLoadInternal", reinterpret_cast<const void*>(ManagedSceneHasHeldLoad));
		mono_add_internal_call("Crucible.Scene::HeldLoadSerialInternal", reinterpret_cast<const void*>(ManagedSceneHeldLoadSerial));
		mono_add_internal_call("Crucible.Scene::BeginPreloadHeldInternal", reinterpret_cast<const void*>(ManagedSceneBeginPreloadHeld));
		mono_add_internal_call("Crucible.Scene::ReleaseHeldLoadInternal", reinterpret_cast<const void*>(ManagedSceneReleaseHeldLoad));
	}
}
#endif
