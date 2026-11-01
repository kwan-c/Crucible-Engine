/*************************************************************************
* \file:         ScriptingConfig.h
* \author:       <author>
* \par:          Email: <email>
* \date          30 October 2026
* \version       1.0
* \brief         Scripting settings read from EngineConfig.json.
*
* \details
*    These structs mirror the optional "scripting" section of
*    Assets/Config/EngineConfig.json (shared, or under "editor" / "game"
*    for per-application overrides). EngineConfigLoader fills them in and
*    Core passes them to ScriptRuntime::Initialize().
*
*    Entity scripts (ScriptComponent) do NOT use these settings; they are
*    stored in the scene instead.
*
**************************************************************************/
#pragma once
#include <string>
#include <vector>

namespace Crucible
{
	/*********************************************************************
	 \brief  One entry of "scripting.instances": a standalone C# object
	         that lives for the whole application (not tied to an entity
	         or to Play mode). Its OnCreate/OnUpdate/OnDestroy are called
	         by Core every frame.
	 *********************************************************************/
	struct ScriptInstanceConfig
	{
		std::string assembly, nameSpace, className; // e.g. "Managed/Crucible.Scripts.dll", "Crucible.Scripts", "GameManager"
		bool enabled = true;                         // false = created but OnUpdate is skipped
	};

	/*********************************************************************
	 \brief  All scripting settings. Every field has a working default, so
	         the JSON section is optional.
	 *********************************************************************/
	struct ScriptingConfig
	{
		// CMake controls availability; this flag controls startup in enabled builds.
		bool enabled = true;
		// Relative paths resolve against the executable, never the working directory.
		std::string runtimeDirectory = "Mono";
		// Runs a tiny C# call (System.Math.Abs) at startup to prove Mono really works.
		bool verifyManagedExecution = true;
		// Empty = no user code at startup. Nonempty entries are invoked once with no arguments.
		std::string startupAssembly, startupNamespace, startupClass, startupMethod;
		bool startupStatic = false; // true = call a static method; false = construct an object and call it
		// Optional standalone Core-lifetime instances. No ECS or gameplay bindings.
		std::vector<ScriptInstanceConfig> instances;
	};
}
