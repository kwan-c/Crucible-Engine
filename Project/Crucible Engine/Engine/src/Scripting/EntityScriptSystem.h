/*************************************************************************
* \file:         EntityScriptSystem.h
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          13 November 2026
* \version       1.1
* \brief         ECS system that runs the C# scripts attached to entities
*                through ScriptComponent.
*
* \details
*    ScriptComponent only stores WHICH C# classes to run (any number per
*    entity, in ScriptComponent::scripts) and their saved field values.
*    This system decides WHEN to create, update and destroy the live C#
*    objects - one per script, run in list order - following Play mode:
*
*      Edit mode    nothing runs
*      Play         create (OnCreate) once, then OnUpdate every frame
*      Pause        objects kept, no updates
*      Stop         everything destroyed (OnDestroy)
*
*    It runs exclusively on the main thread, so no other system touches the
*    world while C# code is executing.
*
**************************************************************************/
#pragma once
#include "EngineExport.h"
#include "ECS/Systems/IECSSystems.h"
#include "ECS/Components/Components.h"
#include "Scripting/ScriptTypes.h"
#include <map>
#include <set>
#include <memory>
#include <vector>

namespace Crucible
{
	class ScriptRuntime;
	class PlayMode;
	class SceneManager;
	namespace ScriptBindings::EntityContext
	{
		struct RunningScript; // Scripting/Bindings/EntityContext.h
	}

	/*********************************************************************
	 \brief  What the Inspector shows for an entity's script: its live
	         instance (empty if not running) and the last error, if any.
	 *********************************************************************/
	struct EntityScriptStatus
	{
		ScriptInstanceHandle instance;
		std::string error;
	};
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251 4275) // private STL; IECSSystem is a shared header-only native interface
#endif
	class ENGINE_API EntityScriptSystem final : public IECSSystem
	{
	public:
		/*********************************************************************
		 \brief  Stores references to the services it needs. Core registers
		         one EntityScriptSystem at startup.
		 *********************************************************************/
		EntityScriptSystem(ScriptRuntime& runtime, World& world, PlayMode& play, SceneManager& scenes);

		/*********************************************************************
		 \brief  Subscribes to "ScriptComponent removed" and "scene changed"
		         events so their scripts get cleaned up on the next Update.
		 *********************************************************************/
		void Init() override;

		/*********************************************************************
		 \brief  Tells the scheduler to run this system alone, on the main
		         thread.
		 *********************************************************************/
		void Describe(SystemDesc& desc) const override;

		/*********************************************************************
		 \brief  Called every frame. Cleans up removed/old scripts, creates
		         scripts that should be running, applies Inspector field edits
		         and calls OnUpdate on enabled scripts while Playing.
		         New scripts are all created first and only then given OnCreate,
		         so OnCreate can find any other entity's script.
		 *********************************************************************/
		void Update(World& world, SystemContext& ctx) override;

		/*********************************************************************
		 \brief  Destroys every script (engine shutdown).
		 *********************************************************************/
		void Shutdown() noexcept override;

		/*********************************************************************
		 \brief  Destroys every live entity script (calls OnDestroy), walking
		         the records in reverse key order. Used on Stop, scene change
		         and shutdown.
		 *********************************************************************/
		void StopAll();

		/*********************************************************************
		 \brief  Live instance and last error for one of an entity's scripts
		         (`slot` = its place in ScriptComponent::scripts), for the
		         Inspector. Empty if that script is not running.
		 *********************************************************************/
		EntityScriptStatus Status(Entity entity, std::size_t slot = 0) const;

	private:
		/*********************************************************************
		 \brief  Spawns SceneManager::kLoadingScreenPrefab and marks it
		         DontDestroyOnLoad, at the start of every play session. A
		         missing prefab is a warning: scene changes are then instant.
		 *********************************************************************/
		void SpawnLoadingScreen();

		/*********************************************************************
		 \brief  Bookkeeping for one script on an entity (one entry of
		         ScriptComponent::scripts).
		 *********************************************************************/
		struct Record
		{
			ScriptEntry definition;        // copy of the script entry when the instance was made
			ScriptInstanceHandle instance; // live C# object (empty if not made yet, or creation failed)
			std::string error;             // last failure, shown in the Inspector
			bool attempted = false;        // creation was tried: not tried again until the script or session changes
			bool started = false;          // OnCreate has run. A script that is off at Play is created (GetComponent
			                               // finds it) but not started until it is first switched on.
		};

		/*********************************************************************
		 \brief  Same script for keeping an instance: same class, namespace
		         and assembly (enabled and field values may differ).
		 *********************************************************************/
		static bool SameScript(const ScriptEntry& a, const ScriptEntry& b)
		{
			return a.className == b.className && a.nameSpace == b.nameSpace && a.assembly == b.assembly;
		}

		/*********************************************************************
		 \brief  Packs an entity's generation and index into one 64-bit map key.
		 *********************************************************************/
		static uint64_t Key(Entity entity)
		{
			return (uint64_t(entity.generation) << 32) | entity.index;
		}

		/*********************************************************************
		 \brief  Destroys one record's C# instance (OnDestroy) and clears its
		         handle. Logs if cleanup fails.
		 *********************************************************************/
		void Release(Record& record);

		/*********************************************************************
		 \brief  Release() for every script on one entity, last first (the
		         reverse of the order they were created in).
		 *********************************************************************/
		void ReleaseAll(std::vector<Record>& records);

		/*********************************************************************
		 \brief  For C# GetComponent<SomeScript>() / GetComponents: the GC
		         handles of the scripts running on `entity`, in list order.
		         Registered with the script bindings in Init
		         (EntityContext::SetScriptLookup).
		 *********************************************************************/
		static void LookupScripts(void* self, Entity entity, std::vector<ScriptBindings::EntityContext::RunningScript>& out);

		/*********************************************************************
		 \brief  Carries out C# Entity.Destroy requests that are due: calls
		         OnDestroy on every script in the entity's subtree (while it
		         still exists), then destroys the entity and its children.
		         Repeats for destroys requested from inside those OnDestroys.
		 *********************************************************************/
		void ApplyDestroys(World& world);

		ScriptRuntime& m_runtime;
		World& m_world;
		PlayMode& m_play;
		SceneManager& m_scenes;
		std::map<uint64_t, std::vector<Record>> m_records; // entity key -> its scripts, in ScriptComponent::scripts order
		std::set<uint64_t> m_removed;         // entities whose ScriptComponent was removed since last Update
		std::shared_ptr<int> m_lifetime = std::make_shared<int>(0); // lets event callbacks detect this system is gone
		uint64_t m_session = 0;               // Play session revision the records belong to
		uint64_t m_reloadSeen = 0;            // ScriptRuntime::ReloadCount() the records belong to
		bool m_sceneChanged = false;          // set by the scene-changed callback
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
