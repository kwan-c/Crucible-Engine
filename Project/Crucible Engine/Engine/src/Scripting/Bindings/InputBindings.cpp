/*************************************************************************
* \file:         InputBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         Internal calls behind the C# Input class
*                (ManagedScripts/Runtime/Input.cs).
*
* \details
*    Views of Crucible::Input (Engine/src/Input, owned by Hugo). The one
*    write is the cursor-lock request (Input.SetMouseLocked): scripts own
*    it, and a menu or the editor can still hold the cursor free over it.
*
*    Same shape as the other bindings: noexcept, catch everything, write
*    results through an out pointer and return 1 on success, 0 on failure.
*    Booleans cross as int (0/1) because bool is not blittable.
*
*    Keys cross as int. C# KeyCode (Runtime/KeyCode.cs) mirrors the order of
*    Input/KeyCode.h; Input.cs compares KeyCode.Count with
*    GetKeyCodeCountInternal() when it first loads and refuses key queries
*    if they disagree.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Input/InputSystem.h"
#include "Input/KeyCode.h"
#if CRUCIBLE_WITH_MONO
#include <mono/metadata/appdomain.h>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		constexpr int32_t KEY_COUNT = static_cast<int32_t>(KeyCode::Count);

		// If this fires, a key was added to Input/KeyCode.h. Add it to
		// ManagedScripts/Runtime/KeyCode.cs in the same place, then update this number.
		static_assert(KEY_COUNT == 130, "KeyCode.h changed - update ManagedScripts/Runtime/KeyCode.cs to match");

		/*********************************************************************
		 \brief  Copies a glm vec2 into the C# Vector2 layout.
		 *********************************************************************/
		void CopyVec2(const Math::vec2& from, ScriptVec2* to)
		{
			to->x = from.x;
			to->y = from.y;
		}

		/*********************************************************************
		 \brief  Shared body of IsDown / IsPressed / IsReleased: range-checks the
		         key from C# and runs the query.
		 \return 1 on success, 0 for a null pointer or an out-of-range key.
		 *********************************************************************/
		template <bool (*Query)(const KeyCode&)>
		int32_t QueryKey(int32_t key, int32_t* out) noexcept
		{
			try
			{
				if (!out)
					return 0;
				*out = 0;
				if (key < 0 || key >= KEY_COUNT)
					return 0;
				*out = Query(static_cast<KeyCode>(key)) ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Shared body of the vec2 getters (mouse and sticks).
		 *********************************************************************/
		template <Math::vec2 (*Getter)()>
		int32_t GetVec2(ScriptVec2* out) noexcept
		{
			try
			{
				if (!out)
					return 0;
				CopyVec2(Getter(), out);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Shared body of the bool getters, returned as int 0/1.
		 *********************************************************************/
		template <bool (*Getter)()>
		int32_t GetFlag(int32_t* out) noexcept
		{
			try
			{
				if (!out)
					return 0;
				*out = Getter() ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Shared body of the trigger getters.
		 *********************************************************************/
		template <float (*Getter)()>
		int32_t GetFloat(float* out) noexcept
		{
			try
			{
				if (!out)
					return 0;
				*out = Getter();
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Input's load-time check: how many KeyCodes the engine has.
		 *********************************************************************/
		int32_t ManagedInputGetKeyCodeCount() noexcept
		{
			return KEY_COUNT;
		}

		/*********************************************************************
		 \brief  C# Input.GamepadName. Returns a new C# string, or null when no
		         pad is connected or the name cannot be converted.
		 *********************************************************************/
		MonoString* ManagedInputGetGamepadName() noexcept
		{
			try
			{
				const std::string name = Input::GetGamepadName();
				if (name.empty())
					return nullptr;
				std::wstring wide;
				if (!ToUtf16(name, wide))
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
		 \brief  C# Input.SetMouseLocked: the game's cursor-lock request.
		 *********************************************************************/
		int32_t ManagedInputSetMouseLocked(int32_t locked) noexcept
		{
			try
			{
				Input::SetMouseLocked(locked != 0);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Registers one internal call. Keeps the table below short.
		 *********************************************************************/
		template <typename Fn>
		void Add(const char* name, Fn* function)
		{
			mono_add_internal_call(name, reinterpret_cast<const void*>(function));
		}
	}

	void RegisterInputBindings()
	{
		Add("Crucible.Input::GetKeyCodeCountInternal", &ManagedInputGetKeyCodeCount);

		Add("Crucible.Input::IsDownInternal", &QueryKey<&Input::IsDown>);
		Add("Crucible.Input::IsPressedInternal", &QueryKey<&Input::IsPressed>);
		Add("Crucible.Input::IsReleasedInternal", &QueryKey<&Input::IsReleased>);

		Add("Crucible.Input::GetMousePositionInternal", &GetVec2<&Input::GetMousePosition>);
		Add("Crucible.Input::GetMouseDeltaInternal", &GetVec2<&Input::GetMouseDelta>);
		Add("Crucible.Input::GetMouseScrollInternal", &GetVec2<&Input::GetMouseScroll>);
		Add("Crucible.Input::IsMouseLockedInternal", &GetFlag<&Input::IsMouseLocked>);
		Add("Crucible.Input::SetMouseLockedInternal", &ManagedInputSetMouseLocked);
		Add("Crucible.Input::IsPointerOverUIInternal", &GetFlag<&Input::IsPointerOverUI>);

		Add("Crucible.Input::IsGamepadConnectedInternal", &GetFlag<&Input::IsGamepadConnected>);
		Add("Crucible.Input::GetGamepadNameInternal", &ManagedInputGetGamepadName);
		Add("Crucible.Input::GetGamepadLeftStickInternal", &GetVec2<&Input::GetGamepadLeftStick>);
		Add("Crucible.Input::GetGamepadRightStickInternal", &GetVec2<&Input::GetGamepadRightStick>);
		Add("Crucible.Input::GetGamepadLeftTriggerInternal", &GetFloat<&Input::GetGamepadLeftTrigger>);
		Add("Crucible.Input::GetGamepadRightTriggerInternal", &GetFloat<&Input::GetGamepadRightTrigger>);
	}
}
#endif
