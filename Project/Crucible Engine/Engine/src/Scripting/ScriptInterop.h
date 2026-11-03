/*************************************************************************
* \file:         ScriptInterop.h
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          30 October 2026
* \version       1.0
* \brief         INTERNAL helpers shared by the scripting .cpp files: GC roots,
*                the managed-call counter, string conversion and the
*                C#-layout math structs (ScriptVec2, ScriptVec3, ScriptQuat).
*
* \details
*    Only include this from .cpp files inside Engine/src/Scripting. It
*    includes Mono headers, so it must never be included from a public
*    header (ScriptRuntime.h, ScriptTypes.h, ...) or from outside Engine.
*
**************************************************************************/
#pragma once
#if CRUCIBLE_WITH_MONO
#include <cstdint>
#include <string>
#include <utility>
#include <mono/metadata/object.h>

namespace Crucible::ScriptingDetail
{
	// How many C# calls are currently on this thread's stack. ManagedCallScope
	// raises it; ScriptRuntime refuses host calls while it is non-zero (no
	// re-entry from inside C#). Defined once, in ScriptInterop.cpp.
	extern thread_local unsigned managedCallDepth;

	// ------------------------------------------------------------------
	// Math Helpers (C# classes Crucible.Vector3 and Crucible.Quaternion, ManagedScripts/Runtime)
	// ------------------------------------------------------------------
	// Same memory layout as the C# structs in ManagedScripts/Runtime.
	// C# passes `ref Vector3` / `out Quaternion` as a pointer to one of these.
	struct ScriptVec2 { float x, y; };        // Crucible.Vector2
	struct ScriptVec3 { float x, y, z; };     // Crucible.Vector3
	struct ScriptQuat { float x, y, z, w; };  // Crucible.Quaternion (X, Y, Z, W order)
	struct ScriptColor { float r, g, b, a; }; // Crucible.Color (R, G, B, A order)
	static_assert(sizeof(ScriptVec2) == 8 && sizeof(ScriptVec3) == 12 && sizeof(ScriptQuat) == 16 &&
	                  sizeof(ScriptColor) == 16,
	              "must match the C# structs");

	/*********************************************************************
	 \brief  RAII counter: increments managedCallDepth while C# code is running,
	         so Ready() can refuse re-entrant host calls made from inside C#.
	 *********************************************************************/
	// Managed code can call native adapters from constructors, reflection,
	// exception formatting and finalizers as well as lifecycle callbacks.
	struct ManagedCallScope
	{
		ManagedCallScope()
		{
			++managedCallDepth;
		}
		~ManagedCallScope()
		{
			--managedCallDepth;
		}
		ManagedCallScope(const ManagedCallScope&) = delete;
		ManagedCallScope& operator=(const ManagedCallScope&) = delete;
	};

	/*********************************************************************
	 \brief  RAII GC handle. While a GcRoot exists, the C# garbage collector will not
	         free (and, when pinned, will not move) the object. The destructor
	         releases it automatically. Use it whenever C++ holds a MonoObject*.
	 *********************************************************************/
	// Temporary pins protect native buffers/pointers during argument conversion
	// and managed calls. Persistent objects use ordinary strong, movable handles.
	struct GcRoot
	{
		uint32_t handle = 0;
		explicit GcRoot(MonoObject* object = nullptr, bool pinned = true)
		    : handle(object ? mono_gchandle_new(object, pinned) : 0)
		{
		}
		~GcRoot()
		{
			if (handle)
				mono_gchandle_free(handle);
		}
		GcRoot(const GcRoot&) = delete;
		GcRoot& operator=(const GcRoot&) = delete;
		GcRoot(GcRoot&& other) noexcept
		    : handle(std::exchange(other.handle, 0))
		{
		}
		GcRoot& operator=(GcRoot&& other) noexcept
		{
			if (this != &other)
			{
				if (handle)
					mono_gchandle_free(handle);
				handle = std::exchange(other.handle, 0);
			}
			return *this;
		}
		MonoObject* Get() const
		{
			return handle ? mono_gchandle_get_target(handle) : nullptr;
		}
	};

	/*********************************************************************
	 \brief  UTF-8 std::string -> UTF-16 std::wstring (C# strings are UTF-16).
	         Returns false for invalid UTF-8.
	 *********************************************************************/
	bool ToUtf16(const std::string& value, std::wstring& wide);

	/*********************************************************************
	 \brief  C# string -> UTF-8 std::string. Reads the string's characters in place
	         (mono_string_chars), so the caller must keep the string rooted.
	         Returns false for invalid UTF-16.
	 *********************************************************************/
	bool FromManagedString(MonoString* value, std::string& text);
}
#endif
