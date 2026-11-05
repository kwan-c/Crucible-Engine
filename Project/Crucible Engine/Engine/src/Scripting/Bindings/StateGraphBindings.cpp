/*************************************************************************
* \file:         StateGraphBindings.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.0
* \brief         Internal calls behind the C# StateGraphAgent class
*                (ManagedScripts/Runtime/StateGraphAgent.cs).
*
* \details
*    An enemy's variables live in its running copy of the graph, which
*    ECSStateGraphSystem owns - not in the component. The bindings only get
*    the World, so they reach the system through ECSStateGraphSystem::Active().
*    Asking for the variables makes the copy if it doesn't exist yet, so a
*    script's OnStart can set them before the graph's first frame.
*
*    A script may only SET variables the graph already has, with the same
*    type (like Unity's BehaviorGraphAgent.SetVariableValue): a typo would
*    otherwise quietly add a new variable no node reads. A wrong name or
*    type is warned about once per name - scripts do this every frame.
*
*    One internal call per value type (Entity, Vector3, float, int, bool);
*    StateGraphAgent.cs picks the right one from the generic T.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "AI/Blackboard.h"
#include "Diagnostics/Logger.h"
#include "ECS/ECSStateGraphSystem.h"
#include "ECS/World.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <string>
#include <unordered_set>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;   // ScriptVec3, GcRoot, FromManagedString, ToUtf16

		// The C# name as a std::string; false for null or absurdly long.
		bool ReadName(MonoString* name, std::string& out)
		{
			if (!name || mono_string_length(name) > 256)
				return false;
			GcRoot root(reinterpret_cast<MonoObject*>(name));   // keeps the GC from moving it while it's read
			return FromManagedString(reinterpret_cast<MonoString*>(root.Get()), out);
		}

		// Null: not Playing, a dead entity, no StateGraphAgent, or its graph didn't load.
		Blackboard* VariablesOf(int32_t index, int32_t generation)
		{
			ECSStateGraphSystem* system = ECSStateGraphSystem::Active();
			Entity entity;
			if (!system || !EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return system->VariablesOf(*EntityContext::GetWorld(), entity);
		}

		// Once per name, not 60 times a second from a script's OnUpdate.
		void WarnOnce(const std::string& name, const Blackboard& variables)
		{
			static std::unordered_set<std::string> warned;
			if (!warned.insert(name).second)
				return;
			CRU_USER_WARN("[Script] StateGraphAgent variable \"{}\": {}", name,
			              variables.Has(name) ? "it holds a different type" : "the graph has no variable by that name");
		}

		// The variable `name` if it exists and holds a T, else null (and warned).
		template <typename T>
		const T* Find(int32_t index, int32_t generation, MonoString* name, std::string& key, Blackboard*& variables)
		{
			if (!ReadName(name, key))
				return nullptr;
			variables = VariablesOf(index, generation);
			if (!variables)
				return nullptr;
			const T* value = variables->Get<T>(key);
			if (!value)
				WarnOnce(key, *variables);
			return value;
		}

		template <typename T>
		int32_t Set(int32_t index, int32_t generation, MonoString* name, const T& value)
		{
			std::string key;
			Blackboard* variables = nullptr;
			if (!Find<T>(index, generation, name, key, variables))
				return 0;
			variables->Set(key, value);
			return 1;
		}

		template <typename T>
		int32_t Get(int32_t index, int32_t generation, MonoString* name, T& out)
		{
			std::string key;
			Blackboard* variables = nullptr;
			const T* value = Find<T>(index, generation, name, key, variables);
			if (!value)
				return 0;
			out = *value;
			return 1;
		}

		// ======================================================================
		// Set: one per type. Values are checked here; names in Find.
		// ======================================================================

		int32_t ManagedSetFloat(int32_t index, int32_t generation, MonoString* name, float value) noexcept
		{
			try { return std::isfinite(value) ? Set<float>(index, generation, name, value) : 0; }
			catch (...) { return 0; }
		}

		int32_t ManagedSetInt(int32_t index, int32_t generation, MonoString* name, int32_t value) noexcept
		{
			try { return Set<int>(index, generation, name, value); }
			catch (...) { return 0; }
		}

		int32_t ManagedSetBool(int32_t index, int32_t generation, MonoString* name, int32_t value) noexcept
		{
			try { return Set<bool>(index, generation, name, value != 0); }
			catch (...) { return 0; }
		}

		int32_t ManagedSetVector3(int32_t index, int32_t generation, MonoString* name, const ScriptVec3* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(value->x) || !std::isfinite(value->y) || !std::isfinite(value->z))
					return 0;
				return Set<Math::vec3>(index, generation, name, Math::vec3(value->x, value->y, value->z));
			}
			catch (...) { return 0; }
		}

		// valueIndex 0 = C# passed null: the variable is set to no entity.
		int32_t ManagedSetEntity(int32_t index, int32_t generation, MonoString* name, int32_t valueIndex, int32_t valueGeneration) noexcept
		{
			try
			{
				Entity value = kNullEntity;
				if (valueIndex != 0 && !EntityContext::ResolveEntity(valueIndex, valueGeneration, value))
					return 0;
				return Set<Entity>(index, generation, name, value);
			}
			catch (...) { return 0; }
		}

		// ======================================================================
		// Get: one per type
		// ======================================================================

		int32_t ManagedGetFloat(int32_t index, int32_t generation, MonoString* name, float* out) noexcept
		{
			try { return out ? Get<float>(index, generation, name, *out) : 0; }
			catch (...) { return 0; }
		}

		int32_t ManagedGetInt(int32_t index, int32_t generation, MonoString* name, int32_t* out) noexcept
		{
			try
			{
				int value = 0;
				if (!out || !Get<int>(index, generation, name, value))
					return 0;
				*out = value;
				return 1;
			}
			catch (...) { return 0; }
		}

		int32_t ManagedGetBool(int32_t index, int32_t generation, MonoString* name, int32_t* out) noexcept
		{
			try
			{
				bool value = false;
				if (!out || !Get<bool>(index, generation, name, value))
					return 0;
				*out = value ? 1 : 0;
				return 1;
			}
			catch (...) { return 0; }
		}

		int32_t ManagedGetVector3(int32_t index, int32_t generation, MonoString* name, ScriptVec3* out) noexcept
		{
			try
			{
				Math::vec3 value(0.0f);
				if (!out || !Get<Math::vec3>(index, generation, name, value))
					return 0;
				*out = { value.x, value.y, value.z };
				return 1;
			}
			catch (...) { return 0; }
		}

		// A variable holding no entity comes back as index 0: C# turns that into null.
		int32_t ManagedGetEntity(int32_t index, int32_t generation, MonoString* name, int32_t* outIndex, int32_t* outGeneration) noexcept
		{
			try
			{
				Entity value = kNullEntity;
				if (!outIndex || !outGeneration || !Get<Entity>(index, generation, name, value))
					return 0;
				*outIndex = value.IsValid() ? static_cast<int32_t>(value.index) : 0;
				*outGeneration = value.IsValid() ? static_cast<int32_t>(value.generation) : 0;
				return 1;
			}
			catch (...) { return 0; }
		}

		// ======================================================================
		// The state it's in
		// ======================================================================

		MonoString* ManagedGetCurrentState(int32_t index, int32_t generation) noexcept
		{
			try
			{
				ECSStateGraphSystem* system = ECSStateGraphSystem::Active();
				Entity entity;
				if (!system || !EntityContext::ResolveEntity(index, generation, entity))
					return nullptr;
				std::wstring wide;
				if (!ToUtf16(std::string(system->CurrentStateOf(entity)), wide))
					return nullptr;
				return mono_string_new_utf16(mono_domain_get(), reinterpret_cast<const mono_unichar2*>(wide.data()),
				                             static_cast<int32_t>(wide.size()));
			}
			catch (...)
			{
				return nullptr;
			}
		}

		template <typename Fn>
		void Add(const char* name, Fn* function)
		{
			mono_add_internal_call(name, reinterpret_cast<const void*>(function));
		}
	}

	void RegisterStateGraphBindings()
	{
		Add("Crucible.StateGraphAgent::SetFloatInternal", &ManagedSetFloat);
		Add("Crucible.StateGraphAgent::SetIntInternal", &ManagedSetInt);
		Add("Crucible.StateGraphAgent::SetBoolInternal", &ManagedSetBool);
		Add("Crucible.StateGraphAgent::SetVector3Internal", &ManagedSetVector3);
		Add("Crucible.StateGraphAgent::SetEntityInternal", &ManagedSetEntity);
		Add("Crucible.StateGraphAgent::GetFloatInternal", &ManagedGetFloat);
		Add("Crucible.StateGraphAgent::GetIntInternal", &ManagedGetInt);
		Add("Crucible.StateGraphAgent::GetBoolInternal", &ManagedGetBool);
		Add("Crucible.StateGraphAgent::GetVector3Internal", &ManagedGetVector3);
		Add("Crucible.StateGraphAgent::GetEntityInternal", &ManagedGetEntity);
		Add("Crucible.StateGraphAgent::GetCurrentStateInternal", &ManagedGetCurrentState);
	}
}
#endif
