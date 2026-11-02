/*************************************************************************
* \file:         ScriptTypes.h
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          30 October 2026
* \version       1.0
* \brief         Plain data types shared by the scripting system.
*
* \details
*    Everything here is ordinary C++ with no Mono types, so any engine or
*    editor file can include it (ScriptComponent in Components.h does).
*    The "handle" structs are opaque IDs handed out by ScriptRuntime; the
*    real Mono pointers stay hidden inside ScriptRuntime.cpp.
*
**************************************************************************/
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace Crucible
{
	/*********************************************************************
	 \brief  C# field types that can be shown in the Inspector and saved
	         in scenes (bool, int, float, double, string, Crucible.Entity,
	         Crucible.Prefab, Crucible.AudioClip, Crucible.Texture,
	         Crucible.Font).
	 *********************************************************************/
	enum class ScriptFieldType
	{
		Boolean,
		Int32,
		Single,
		Double,
		String,
		Entity,    // a reference to another entity, saved as its scene id
		Prefab,    // a .cprefab asset, saved as its asset guid
		AudioClip, // an audio file (.wav, .ogg, ...), saved as its asset guid
		Texture,   // an image (.png, .jpg, ...), saved as its asset guid
		Font       // a font (.ttf / .otf / .cfont), saved as its asset guid
	};

	/*********************************************************************
	 \brief  The saved value of one public C# field on a ScriptComponent.
	 \note   Only the member matching `type` is meaningful (e.g. `single`
	         for ScriptFieldType::Single); the others stay at defaults.
	 *********************************************************************/
	// Scene-safe field override. Identity includes the declaring type so hidden
	// inherited fields cannot overwrite one another. No Mono pointers/GC handles.
	struct ScriptFieldValue
	{
		std::string declaringType, name;                 // class that declares the field + field name
		ScriptFieldType type = ScriptFieldType::Boolean; // which value member below is used
		bool boolean = false;
		int32_t integer = 0;
		float single = 0;
		double real = 0;
		std::string text;
		bool stringIsNull = false; // distinguishes a null string from ""
		uint64_t entityId = 0;     // Entity: the target's scene id (Scene::EnsureId); 0 = none
		uint64_t assetId = 0;      // Prefab / AudioClip / Texture / Font: the asset guid (from its .meta); 0 = none
		bool operator==(const ScriptFieldValue&) const = default;
	};

	/*********************************************************************
	 \brief  True if a and b describe the same field (same declaring class,
	         name and type), regardless of their values.
	 *********************************************************************/
	inline bool SameScriptField(const ScriptFieldValue& a, const ScriptFieldValue& b)
	{
		return a.declaringType == b.declaringType && a.name == b.name && a.type == b.type;
	}

	// Host-local handles, never serialized. Copying does not create another object.

	/*********************************************************************
	 \brief  ID of a loaded C# DLL. 0 = empty.
	 *********************************************************************/
	struct ScriptAssemblyHandle
	{
		uint64_t value = 0;
		explicit operator bool() const
		{
			return value != 0;
		}
	};

	/*********************************************************************
	 \brief  ID of a raw C# object created with ScriptRuntime::CreateObject
	         (no lifecycle callbacks). 0 = empty.
	 *********************************************************************/
	struct ScriptObjectHandle
	{
		uint64_t value = 0;
		explicit operator bool() const
		{
			return value != 0;
		}
	};

	/*********************************************************************
	 \brief  ID of a script instance with OnCreate/OnUpdate/OnDestroy,
	         either standalone or attached to an entity. 0 = empty.
	 *********************************************************************/
	struct ScriptInstanceHandle
	{
		uint64_t value = 0;
		explicit operator bool() const
		{
			return value != 0;
		}
	};

	/*********************************************************************
	 \brief  Running  = receives OnUpdate every frame.
	         Disabled = alive, but OnUpdate is skipped.
	         Faulted  = a callback threw; the object was cleaned up and
	                    will not run again.
	 *********************************************************************/
	enum class ScriptInstanceState
	{
		Running,
		Disabled,
		Faulted
	};

	/*********************************************************************
	 \brief  Read-only snapshot of a script instance, for diagnostics and
	         the Inspector status display.
	 *********************************************************************/
	struct ScriptInstanceInfo
	{
		ScriptInstanceHandle handle;
		ScriptAssemblyHandle assembly;
		std::string nameSpace, name; // C# class
		ScriptInstanceState state = ScriptInstanceState::Disabled;
		uint64_t updateCount = 0;    // successful OnUpdate calls
		double elapsedSeconds = 0;   // sum of deltaTime over those calls
		std::string lastError;       // set when Faulted
		bool entityOwned = false;    // true = created by EntityScriptSystem for a ScriptComponent
		uint32_t entityIndex = 0, entityGeneration = 0;
	};

	/*********************************************************************
	 \brief  One C# class found in a DLL by DiscoverClasses().
	 *********************************************************************/
	struct ScriptClassInfo
	{
		std::string nameSpace, name;
		bool isAbstract = false;
		bool hasPublicDefaultConstructor = false; // required to create it from C++
	};

	/*********************************************************************
	 \brief  A value passed to or returned from a C# method by
	         ScriptRuntime::Invoke / InvokeStatic.
	 *********************************************************************/
	// monostate = void, nullptr = null String. Arguments require exact managed types.
	using ScriptValue =
	    std::variant<std::monostate, bool, int32_t, float, double, std::string, std::nullptr_t>;
}
