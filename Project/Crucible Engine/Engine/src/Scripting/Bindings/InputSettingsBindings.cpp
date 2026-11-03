/*************************************************************************
* \file:         InputSettingsBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the static C# InputSettings class
*                (ManagedScripts/Runtime/InputSettings.cs): the player's
*                keybinds, look sensitivity and invert Y.
*
* \details
*    Edits Core's live UserSettings (the same object audio bus volumes use,
*    from GetUserSettings). ECSPlayerControllerSystem holds a reference to
*    UserSettings::input, so a change applies from that frame. Nothing is
*    written to UserSettings.json until SaveInternal (C# Save) - the same
*    contract as Audio.SaveSettings.
*
*    Keys cross as int (the C# KeyCode has the native numbers; Unknown = 0
*    = unbound). Sensitivities are clamped to the range UserSettingsLoader
*    accepts, so a script can't save a value the loader would change.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Configuration/UserSettings.h"
#include "Input/KeyCode.h"
#if CRUCIBLE_WITH_MONO
#include <algorithm>
#include <cmath>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		// Same range as UserSettingsLoader::Load clamps to.
		constexpr float kMinSensitivity = 0.1f;
		constexpr float kMaxSensitivity = 10.0f;

		InputSettings* Settings()
		{
			UserSettings* settings = GetUserSettings();
			return settings ? &settings->input : nullptr;
		}

		// ======================================================================
		// Keybinds
		// ======================================================================

		template <KeyCode InputSettings::*Member>
		int32_t GetKey(int32_t* out) noexcept
		{
			try
			{
				const InputSettings* input = out ? Settings() : nullptr;
				if (!input)
					return 0;
				*out = static_cast<int32_t>(input->*Member);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <KeyCode InputSettings::*Member>
		int32_t SetKey(int32_t value) noexcept
		{
			try
			{
				if (value < 0 || value >= static_cast<int32_t>(KeyCode::Count))
					return 0;
				InputSettings* input = Settings();
				if (!input)
					return 0;
				input->*Member = static_cast<KeyCode>(value);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# InputSettings.ResetKeys: back to W A S D / Space /
		         GamepadA (the InputSettings defaults). Sensitivity untouched.
		 *********************************************************************/
		int32_t ManagedResetKeys() noexcept
		{
			try
			{
				InputSettings* input = Settings();
				if (!input)
					return 0;
				const InputSettings defaults{};
				input->forwardKey = defaults.forwardKey;
				input->backKey = defaults.backKey;
				input->leftKey = defaults.leftKey;
				input->rightKey = defaults.rightKey;
				input->jumpKey = defaults.jumpKey;
				input->jumpButton = defaults.jumpButton;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Look sensitivity, invert Y
		// ======================================================================

		template <float InputSettings::*Member>
		int32_t GetSensitivity(float* out) noexcept
		{
			try
			{
				const InputSettings* input = out ? Settings() : nullptr;
				if (!input)
					return 0;
				*out = input->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <float InputSettings::*Member>
		int32_t SetSensitivity(const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value))
					return 0;
				InputSettings* input = Settings();
				if (!input)
					return 0;
				input->*Member = std::clamp(*value, kMinSensitivity, kMaxSensitivity);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedGetInvertY(int32_t* out) noexcept
		{
			try
			{
				const InputSettings* input = out ? Settings() : nullptr;
				if (!input)
					return 0;
				*out = input->invertY ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedSetInvertY(int32_t value) noexcept
		{
			try
			{
				InputSettings* input = Settings();
				if (!input)
					return 0;
				input->invertY = value != 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# InputSettings.Save: writes every player setting (keys,
		         sensitivity, volumes) to UserSettings.json.
		 *********************************************************************/
		int32_t ManagedSave() noexcept
		{
			try
			{
				UserSettings* settings = GetUserSettings();
				return settings && UserSettingsLoader::Save(*settings) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename Fn>
		void Add(const char* name, Fn* function)
		{
			mono_add_internal_call(name, reinterpret_cast<const void*>(function));
		}
	}

	void RegisterInputSettingsBindings()
	{
		Add("Crucible.InputSettings::GetForwardKeyInternal", &GetKey<&InputSettings::forwardKey>);
		Add("Crucible.InputSettings::SetForwardKeyInternal", &SetKey<&InputSettings::forwardKey>);
		Add("Crucible.InputSettings::GetBackKeyInternal", &GetKey<&InputSettings::backKey>);
		Add("Crucible.InputSettings::SetBackKeyInternal", &SetKey<&InputSettings::backKey>);
		Add("Crucible.InputSettings::GetLeftKeyInternal", &GetKey<&InputSettings::leftKey>);
		Add("Crucible.InputSettings::SetLeftKeyInternal", &SetKey<&InputSettings::leftKey>);
		Add("Crucible.InputSettings::GetRightKeyInternal", &GetKey<&InputSettings::rightKey>);
		Add("Crucible.InputSettings::SetRightKeyInternal", &SetKey<&InputSettings::rightKey>);
		Add("Crucible.InputSettings::GetJumpKeyInternal", &GetKey<&InputSettings::jumpKey>);
		Add("Crucible.InputSettings::SetJumpKeyInternal", &SetKey<&InputSettings::jumpKey>);
		Add("Crucible.InputSettings::GetJumpButtonInternal", &GetKey<&InputSettings::jumpButton>);
		Add("Crucible.InputSettings::SetJumpButtonInternal", &SetKey<&InputSettings::jumpButton>);
		Add("Crucible.InputSettings::ResetKeysInternal", &ManagedResetKeys);

		Add("Crucible.InputSettings::GetMouseSensitivityInternal", &GetSensitivity<&InputSettings::mouseSensitivity>);
		Add("Crucible.InputSettings::SetMouseSensitivityInternal", &SetSensitivity<&InputSettings::mouseSensitivity>);
		Add("Crucible.InputSettings::GetStickSensitivityInternal", &GetSensitivity<&InputSettings::stickSensitivity>);
		Add("Crucible.InputSettings::SetStickSensitivityInternal", &SetSensitivity<&InputSettings::stickSensitivity>);
		Add("Crucible.InputSettings::GetInvertYInternal", &ManagedGetInvertY);
		Add("Crucible.InputSettings::SetInvertYInternal", &ManagedSetInvertY);

		Add("Crucible.InputSettings::SaveInternal", &ManagedSave);
	}
}
#endif
