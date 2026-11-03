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
		int32_t ManagedSceneLoad(MonoString* path) noexcept
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
				if (!scenes->RequestLoad(assets->Resolve(virtualPath)))
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

	void RegisterSceneBindings()
	{
		mono_add_internal_call("Crucible.Scene::LoadInternal", reinterpret_cast<const void*>(ManagedSceneLoad));
		mono_add_internal_call("Crucible.Scene::ReloadInternal", reinterpret_cast<const void*>(ManagedSceneReload));
		mono_add_internal_call("Crucible.Scene::GetNameInternal", reinterpret_cast<const void*>(ManagedSceneGetName));
	}
}
#endif
