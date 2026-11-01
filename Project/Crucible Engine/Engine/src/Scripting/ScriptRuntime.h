/*************************************************************************
* \file:         ScriptRuntime.h
* \author:       <author>
* \par:          Email: <email>
* \date          30 October 2026
* \version       1.0
* \brief         The C# (Mono) host: starts Mono, loads C# DLLs, creates
*                C# objects and calls their methods.
*
* \details
*    Core owns exactly one ScriptRuntime for the whole process. The API is
*    layered; each layer is built on the one before it:
*
*      1. Lifetime    Initialize / Shutdown / State
*      2. Raw access  LoadAssembly, DiscoverClasses, CreateObject,
*                     Invoke, InvokeStatic, DestroyObject
*      3. Instances   CreateInstance, UpdateInstances, DestroyInstance...
*                     (objects with OnCreate / OnUpdate / OnDestroy)
*      4. Entities    CreateEntityInstance, UpdateEntityInstance and the
*                     public-field functions, used by EntityScriptSystem
*      5. Bindings    SetAnimatorBinding (engine features C# can call)
*
*    Rules that apply to every function:
*     - Call only from the main thread (the one that called Initialize).
*     - Functions return false on failure; LastError() says why.
*     - No Mono types appear here; they stay inside the Scripting .cpp files.
*     - Script DLLs live in a separate script domain; ReloadScripts() unloads
*       it and starts over (Editor, between Play sessions).
*
**************************************************************************/
#pragma once
#include "EngineExport.h"
#include "Scripting/ScriptingConfig.h"
#include "Scripting/ScriptTypes.h"
#include <memory>
#include <string>
#include <vector>

namespace Crucible
{
	class IScriptAnimatorBinding;
	class World;
	class PlayMode;

	/*********************************************************************
	 \brief  NotStarted  = Initialize() not called yet.
	         Unavailable = engine was built without Mono (CRUCIBLE_ENABLE_MONO=OFF).
	         Disabled    = scripting.enabled is false in the config.
	         Running     = Mono is up and C# can run.
	         Stopped     = Shutdown() has run (cannot be restarted).
	         Failed      = Initialize() failed; see LastError().
	 *********************************************************************/
	enum class ScriptRuntimeState
	{
		NotStarted,
		Unavailable,
		Disabled,
		Running,
		Stopped,
		Failed
	};

	// One process-lifetime, main-thread Mono host. No ECS, assets, gameplay bindings
	// or Mono SDK types in this interface. Final cleanup cannot be followed by restart.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // private Pimpl; allocation and destruction stay inside Engine.dll
#endif
	class ENGINE_API ScriptRuntime final
	{
	public:
		ScriptRuntime();
		~ScriptRuntime();
		ScriptRuntime(const ScriptRuntime&) = delete;
		ScriptRuntime& operator=(const ScriptRuntime&) = delete;

		// ==================================================================
		// 1. Lifetime
		// ==================================================================

		/*********************************************************************
		 \brief  Starts Mono: checks the deployed Mono folder, creates the root
		         domain, registers the C++ functions C# can call (Log, Animator)
		         and optionally runs a self-test.
		 \return true if Running, or if scripting is Disabled/Unavailable (the
		         engine can continue without C#); false on a real failure.
		 \note   Can only succeed once per process.
		 *********************************************************************/
		bool Initialize(const ScriptingConfig& config);

		/*********************************************************************
		 \brief  Destroys all remaining C# objects and shuts Mono down for good.
		         Safe to call more than once.
		 *********************************************************************/
		void Shutdown() noexcept;

		/*********************************************************************
		 \brief  true if this Engine.dll was compiled with Mono support.
		 *********************************************************************/
		static bool IsBuiltWithMono() noexcept;

		/*********************************************************************
		 \brief  Current lifetime state (see ScriptRuntimeState).
		 *********************************************************************/
		ScriptRuntimeState State() const noexcept;

		/*********************************************************************
		 \brief  Shorthand for State() == Running. Check this before using any
		         other function.
		 *********************************************************************/
		bool IsRunning() const noexcept;

		/*********************************************************************
		 \brief  true if the startup self-test actually executed C# code.
		 *********************************************************************/
		bool ManagedExecutionVerified() const noexcept;

		/*********************************************************************
		 \brief  Mono's version string, e.g. "6.12.0 (Visual Studio built mono)".
		 *********************************************************************/
		const std::string& Version() const noexcept;

		/*********************************************************************
		 \brief  Message describing the most recent failure. Cleared by the
		         next successful call.
		 *********************************************************************/
		const std::string& LastError() const noexcept;

		// ==================================================================
		// 2. Raw access: load DLLs, create objects, call any method
		// ==================================================================

		// Main/initialization thread only. Paths are UTF-8 and executable-relative.
		// Assemblies remain loaded until final shutdown; there is no reload/unload API.

		/*********************************************************************
		 \brief  Loads a C# DLL (e.g. "Managed/Crucible.Scripts.dll").
		         Loading the same file again returns the same handle.
		         The DLL is read into memory and loaded from there (dependencies
		         next to it too), so the file is never locked: a build can replace
		         it while the app runs. The running app keeps the copy it loaded.
		 \param[out] assembly  Handle to pass to the other functions.
		 *********************************************************************/
		bool LoadAssembly(const std::string& path, ScriptAssemblyHandle& assembly);

		/*********************************************************************
		 \brief  Lists the public, top-level, non-generic classes in a DLL.
		 *********************************************************************/
		bool DiscoverClasses(ScriptAssemblyHandle assembly, std::vector<ScriptClassInfo>& classes);

		/*********************************************************************
		 \brief  Creates a C# object with its public no-argument constructor.
		         The runtime keeps it alive until DestroyObject().
		 \note   No OnCreate/OnUpdate/OnDestroy; use CreateInstance for that.
		 *********************************************************************/
		bool CreateObject(ScriptAssemblyHandle assembly, const std::string& nameSpace,
		                  const std::string& name, ScriptObjectHandle& object);

		/*********************************************************************
		 \brief  Releases an object from CreateObject so the C# garbage
		         collector may free it. Clears the handle.
		 *********************************************************************/
		bool DestroyObject(ScriptObjectHandle& object);

		/*********************************************************************
		 \brief  Calls a public instance method on an object.
		 \param  arguments  Must match the C# parameter types exactly
		                    (int32_t for int, float for float, ...).
		 \param[out] result The return value (std::monostate for void).
		 *********************************************************************/
		bool Invoke(ScriptObjectHandle object, const std::string& method,
		            const std::vector<ScriptValue>& arguments, ScriptValue& result);

		/*********************************************************************
		 \brief  Calls a public static method on a class. Same argument rules
		         as Invoke().
		 *********************************************************************/
		bool InvokeStatic(ScriptAssemblyHandle assembly, const std::string& nameSpace,
		                  const std::string& name, const std::string& method,
		                  const std::vector<ScriptValue>& arguments, ScriptValue& result);

		/*********************************************************************
		 \brief  Runs the optional "startup*" method from the config once.
		         Does nothing if those settings are empty.
		 *********************************************************************/
		// Optional one-shot configured entry; not a frame callback or gameplay lifecycle.
		bool RunStartupEntry(const ScriptingConfig& config);

		// ==================================================================
		// 3. Instances: objects with OnCreate / OnUpdate / OnDestroy
		// ==================================================================

		/*********************************************************************
		 \brief  Creates a standalone (not entity-attached) script instance and
		         calls its OnCreate().
		 \param  enabled  false = created, but OnUpdate is skipped until enabled.
		 *********************************************************************/
		// S3 contract: public instance void OnCreate(), OnUpdate(float), OnDestroy().
		// The host owns the object; callback methods are validated before construction.
		bool CreateInstance(ScriptAssemblyHandle assembly, const std::string& nameSpace,
		                    const std::string& name, ScriptInstanceHandle& instance, bool enabled = true);

		/*********************************************************************
		 \brief  Pauses (false) or resumes (true) OnUpdate for an instance.
		         A Faulted instance cannot be re-enabled.
		 *********************************************************************/
		bool SetInstanceEnabled(ScriptInstanceHandle instance, bool enabled);

		/*********************************************************************
		 \brief  Copies an instance's current state, counters and last error.
		 *********************************************************************/
		bool GetInstanceInfo(ScriptInstanceHandle instance, ScriptInstanceInfo& info);

		/*********************************************************************
		 \brief  Snapshots every live instance, in creation order.
		 *********************************************************************/
		bool ListInstances(std::vector<ScriptInstanceInfo>& instances);

		/*********************************************************************
		 \brief  Calls OnUpdate(deltaTime) on every enabled STANDALONE instance.
		         Core calls this once per frame. Entity scripts are skipped
		         (EntityScriptSystem updates those).
		 \return false if any instance threw this frame (it becomes Faulted).
		 *********************************************************************/
		bool UpdateInstances(float deltaTime);

		/*********************************************************************
		 \brief  Calls OnDestroy() on one instance, releases it and clears the
		         handle (even if OnDestroy throws).
		 *********************************************************************/
		bool DestroyInstance(ScriptInstanceHandle& instance);

		/*********************************************************************
		 \brief  Destroys every instance, newest first.
		 *********************************************************************/
		bool DestroyAllInstances();

		/*********************************************************************
		 \brief  Creates the standalone instances listed in
		         config.instances. If one fails, the ones from this call are
		         destroyed again.
		 *********************************************************************/
		// Opt-in config batch; a failed creation rolls back only this batch.
		bool CreateConfiguredInstances(const ScriptingConfig& config);

		// ==================================================================
		// 4. Entity scripts (used by EntityScriptSystem and the Inspector)
		// ==================================================================

		/*********************************************************************
		 \brief  Lists classes that can be attached to entities: concrete
		         subclasses of Crucible.EntityScript with a public no-argument
		         constructor. Used by Inspector > Add Component.
		 *********************************************************************/
		// Entity adapters own scheduling; UpdateInstances ticks standalone ones only.
		bool DiscoverEntityClasses(ScriptAssemblyHandle assembly, std::vector<ScriptClassInfo>& classes);

		/*********************************************************************
		 \brief  Creates an entity script: constructs it, binds it to the
		         entity (index + generation), then calls OnCreate().
		 *********************************************************************/
		bool CreateEntityInstance(ScriptAssemblyHandle assembly, const std::string& nameSpace,
		                          const std::string& name, uint32_t index, uint32_t generation,
		                          ScriptInstanceHandle& instance);

		/*********************************************************************
		 \brief  Calls OnUpdate(deltaTime) on one entity script.
		 *********************************************************************/
		bool UpdateEntityInstance(ScriptInstanceHandle instance, float deltaTime);

		/*********************************************************************
		 \brief  Reads the initial values of a class's public fields (from its
		         C# field initializers) for the Inspector. Constructs one
		         temporary object per class and caches the result.
		 *********************************************************************/
		// Public mutable instance fields: bool/int/float/double/string, including
		// inherited fields. Default inspection constructs once per class, without callbacks.
		bool GetEntityFieldDefaults(ScriptAssemblyHandle assembly, const std::string& nameSpace,
		                            const std::string& name, std::vector<ScriptFieldValue>& fields);

		/*********************************************************************
		 \brief  Same as the overload above, but also writes the saved
		         Inspector field values into the object before OnCreate().
		 *********************************************************************/
		bool CreateEntityInstance(ScriptAssemblyHandle assembly, const std::string& nameSpace,
		                          const std::string& name, uint32_t index, uint32_t generation,
		                          ScriptInstanceHandle& instance,
		                          const std::vector<ScriptFieldValue>& fields);

		/*********************************************************************
		 \brief  Reads the current public field values of a live entity script
		         (shown in the Inspector during Play).
		 *********************************************************************/
		bool ReadEntityFields(ScriptInstanceHandle instance, std::vector<ScriptFieldValue>& fields);

		/*********************************************************************
		 \brief  Writes public field values into a live entity script (Inspector
		         edits during Play). All values are checked first; if any is
		         invalid, none are written.
		 *********************************************************************/
		bool SetEntityFields(ScriptInstanceHandle instance, const std::vector<ScriptFieldValue>& fields);

		// ==================================================================
		// Reload (Editor): swap in rebuilt script DLLs without restarting
		// ==================================================================

		/*********************************************************************
		 \brief  Unloads every script DLL and C# object and starts a fresh script
		         domain; the next LoadAssembly reads the DLLs from disk again.
		         Calls OnDestroy on standalone instances, then recreates the ones
		         from CreateConfiguredInstances.
		 \note   Refused while entity scripts are running: call it between Play
		         sessions. Every assembly/object/instance handle obtained before
		         is stale afterwards (calls with it fail) - fetch new ones.
		         Static C# fields reset.
		 \return false with LastError() on failure or problems (e.g. OnDestroy
		         threw); scripting stays usable unless State() becomes Failed.
		 *********************************************************************/
		bool ReloadScripts();

		/*********************************************************************
		 \brief  Number of successful ReloadScripts calls. Editor panels compare it
		         with a saved value to know when cached script data is stale.
		 *********************************************************************/
		uint64_t ReloadCount() const noexcept;

		// ==================================================================
		// 5. Bindings: engine features C# can call
		// ==================================================================

		/*********************************************************************
		 \brief  Connects the C# Animator API to the animation system. Pass
		         nullptr to disconnect (Core does this before shutdown).
		 *********************************************************************/
		// Non-owning; initialization thread only. Clear before destroying the adapter.
		bool SetAnimatorBinding(IScriptAnimatorBinding* binding);

		/*********************************************************************
		 \brief  Gives the entity bindings (Transform, ...) access to the world and
		         Play state. Pass nullptr, nullptr to disconnect (Core does this
		         before the world is destroyed).
		 *********************************************************************/
		bool SetWorldBinding(World* world, PlayMode* play);

	private:
		/*********************************************************************
		 \brief  Shared implementation of CreateInstance and both
		         CreateEntityInstance overloads.
		 \param  entityIdentity  nullptr for standalone instances, otherwise
		                         a 2-element array {index, generation}.
		 \param  fields          Optional saved field values to apply before
		                         OnCreate().
		 *********************************************************************/
		bool CreateInstanceImpl(ScriptAssemblyHandle assembly, const std::string& nameSpace,
		                        const std::string& name, ScriptInstanceHandle& instance, bool enabled,
		                        const uint32_t* entityIdentity,
		                        const std::vector<ScriptFieldValue>* fields = nullptr);

		// All Mono state (domain, loaded DLLs, objects, instances) lives in Impl,
		// which is defined in ScriptRuntime.cpp so this header needs no Mono headers.
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
