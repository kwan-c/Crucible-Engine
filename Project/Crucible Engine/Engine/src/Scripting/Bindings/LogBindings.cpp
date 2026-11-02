/*************************************************************************
* \file:         LogBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          30 October 2026
* \version       1.0
* \brief         Internal call behind the C# Log class
*                (ManagedScripts/Runtime/EntityScript.cs).
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Diagnostics/Logger.h"
#if CRUCIBLE_WITH_MONO
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		/*********************************************************************
		 \brief  C# Log.Info / Log.Warning / Log.Error end up here (level 0 / 1 / 2).
		         Writes "[Script] message" to the Editor Console.
		 *********************************************************************/
		// C ABI boundary: never let native exceptions cross into managed code.
		void ManagedLog(int32_t level, MonoString* message) noexcept
		{
			try
			{
				GcRoot root(reinterpret_cast<MonoObject*>(message));
				std::string text;
				if (root.Get() && !FromManagedString(reinterpret_cast<MonoString*>(root.Get()), text))
					text = "<invalid UTF-16 log message>";
				if (level == 2)
					CRU_USER_ERROR("[Script] {}", text);
				else if (level == 1)
					CRU_USER_WARN("[Script] {}", text);
				else
					CRU_USER_INFO("[Script] {}", text);
			}
			catch (...)
			{
			}
		}
	}

	void RegisterLogBindings()
	{
		mono_add_internal_call("Crucible.Log::WriteInternal", reinterpret_cast<const void*>(ManagedLog));
	}
}
#endif
