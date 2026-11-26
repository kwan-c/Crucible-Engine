/*************************************************************************
* \file:         QualitySettingsBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          25 November 2026
* \version       1.0
* \brief         Internal calls behind the static C# QualitySettings class
*                (ManagedScripts/Runtime/QualitySettings.cs): the player's
*                graphics quality - preset, anti-aliasing, shadows, ambient
*                occlusion, bloom, volumetric fog, VSync, frame-rate limit.
*
* \details
*    Every change goes through Core::SetGraphicsSettings, which stores it in
*    Core's live UserSettings and applies it to the renderer straight away
*    (it shows next frame), and works out the preset again: a single setting
*    that no longer matches the preset makes it Custom. Nothing is written to
*    UserSettings.json until SaveInternal (C# Save) - the same contract as
*    InputSettings and the audio volumes.
*
*    In the editor a change still applies (so an options menu can be tested
*    in Play), and Stop puts the Scene View panel's own values back.
*
*    Enums cross as int, in the C++ enum order (Configuration/UserSettings.h),
*    which the C# enums copy. Out-of-range values are refused (return 0).
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Configuration/UserSettings.h"
#include "Core.h"
#include <cmath>
#if CRUCIBLE_WITH_MONO
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		/*********************************************************************
		 \brief  Copies the current settings, lets `edit` change the copy,
		         then hands it to Core (stores + applies). `edit` returns
		         false to refuse the change.
		 *********************************************************************/
		template <typename Edit>
		int32_t Change(Edit&& edit) noexcept
		{
			try
			{
				Core* core = CurrentCore();
				if (!core)
					return 0;
				GraphicsSettings settings = core->GetGraphicsSettings();
				if (!edit(settings))
					return 0;
				core->SetGraphicsSettings(settings);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Writes `read(settings)` to *out.
		 *********************************************************************/
		template <typename Read>
		int32_t Query(int32_t* out, Read&& read) noexcept
		{
			try
			{
				const Core* core = CurrentCore();
				if (!out || !core)
					return 0;
				*out = static_cast<int32_t>(read(core->GetGraphicsSettings()));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Preset
		// ======================================================================

		int32_t ManagedGetQuality(int32_t* out) noexcept
		{
			return Query(out, [](const GraphicsSettings& s) { return s.quality; });
		}

		/*********************************************************************
		 \brief  C# QualitySettings.Level (set): every setting to that
		         preset. Custom is refused - it is what the settings become,
		         not one that can be picked.
		 *********************************************************************/
		int32_t ManagedSetQuality(int32_t value) noexcept
		{
			if (value < 0 || value >= static_cast<int32_t>(QualityLevel::Custom))
				return 0;
			return Change([value](GraphicsSettings& s) {
				s = ApplyPreset(s, static_cast<QualityLevel>(value));   // VSync and the frame-rate limit kept
				return true;
			});
		}

		// ======================================================================
		// Individual settings
		// ======================================================================

		int32_t ManagedGetAntiAliasing(int32_t* out) noexcept
		{
			return Query(out, [](const GraphicsSettings& s) { return s.antiAliasing; });
		}

		int32_t ManagedSetAntiAliasing(int32_t value) noexcept
		{
			if (value < 0 || value > static_cast<int32_t>(AntiAliasing::MSAA4x))
				return 0;
			return Change([value](GraphicsSettings& s) {
				s.antiAliasing = static_cast<AntiAliasing>(value);
				return true;
			});
		}

		int32_t ManagedGetShadowQuality(int32_t* out) noexcept
		{
			return Query(out, [](const GraphicsSettings& s) { return s.shadowQuality; });
		}

		int32_t ManagedSetShadowQuality(int32_t value) noexcept
		{
			if (value < 0 || value > static_cast<int32_t>(ShadowQuality::Ultra))
				return 0;
			return Change([value](GraphicsSettings& s) {
				s.shadowQuality = static_cast<ShadowQuality>(value);
				return true;
			});
		}

		template <bool GraphicsSettings::*Member>
		int32_t GetFlag(int32_t* out) noexcept
		{
			return Query(out, [](const GraphicsSettings& s) { return s.*Member ? 1 : 0; });
		}

		template <bool GraphicsSettings::*Member>
		int32_t SetFlag(int32_t value) noexcept
		{
			return Change([value](GraphicsSettings& s) {
				s.*Member = value != 0;
				return true;
			});
		}

		/*********************************************************************
		 \brief  C# QualitySettings.FrameRateLimit: -1 = the team's
		         targetFPS, 0 = unlimited, else clamped to 15..1000.
		 *********************************************************************/
		int32_t ManagedGetFrameRateLimit(int32_t* out) noexcept
		{
			return Query(out, [](const GraphicsSettings& s) { return s.frameRateLimit; });
		}

		int32_t ManagedSetFrameRateLimit(int32_t value) noexcept
		{
			return Change([value](GraphicsSettings& s) {
				s.frameRateLimit = ClampFrameRateLimit(value);
				return true;
			});
		}

		/*********************************************************************
		 \brief  Float settings (brightness, FOV offset): `clamp` makes the
		         value valid. NaN / infinity are refused.
		 *********************************************************************/
		template <float GraphicsSettings::*Member>
		int32_t GetFloat(float* out) noexcept
		{
			try
			{
				const Core* core = CurrentCore();
				if (!out || !core)
					return 0;
				*out = core->GetGraphicsSettings().*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <float GraphicsSettings::*Member, float (*Clamp)(float)>
		int32_t SetFloat(float value) noexcept
		{
			if (!std::isfinite(value))
				return 0;
			return Change([value](GraphicsSettings& s) {
				s.*Member = Clamp(value);
				return true;
			});
		}

		float BrightnessClamp(float v) { return ClampBrightness(v); }
		float FovOffsetClamp(float v) { return ClampFovOffset(v); }

		// ======================================================================
		// Save / revert / defaults
		// ======================================================================

		/*********************************************************************
		 \brief  C# QualitySettings.Save: writes every player setting
		         (graphics, keys, volumes) to UserSettings.json.
		 *********************************************************************/
		int32_t ManagedSave() noexcept
		{
			try
			{
				const Core* core = CurrentCore();
				return core && core->SaveUserSettings() ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# QualitySettings.Revert: back to the graphics settings in
		         UserSettings.json (what was last saved). Other settings are
		         left alone.
		 *********************************************************************/
		int32_t ManagedRevert() noexcept
		{
			return Change([](GraphicsSettings& s) {
				s = UserSettingsLoader::Load().graphics;
				return true;
			});
		}

		/*********************************************************************
		 \brief  C# QualitySettings.ResetToDefaults: the default preset
		         (High). Not saved until Save.
		 *********************************************************************/
		int32_t ManagedResetToDefaults() noexcept
		{
			return Change([](GraphicsSettings& s) {
				s = GraphicsSettings{};
				return true;
			});
		}

		template <typename Fn>
		void Add(const char* name, Fn* function)
		{
			mono_add_internal_call(name, reinterpret_cast<const void*>(function));
		}
	}

	void RegisterQualitySettingsBindings()
	{
		Add("Crucible.QualitySettings::GetLevelInternal", &ManagedGetQuality);
		Add("Crucible.QualitySettings::SetLevelInternal", &ManagedSetQuality);
		Add("Crucible.QualitySettings::GetAntiAliasingInternal", &ManagedGetAntiAliasing);
		Add("Crucible.QualitySettings::SetAntiAliasingInternal", &ManagedSetAntiAliasing);
		Add("Crucible.QualitySettings::GetShadowQualityInternal", &ManagedGetShadowQuality);
		Add("Crucible.QualitySettings::SetShadowQualityInternal", &ManagedSetShadowQuality);
		Add("Crucible.QualitySettings::GetAmbientOcclusionInternal", &GetFlag<&GraphicsSettings::ambientOcclusion>);
		Add("Crucible.QualitySettings::SetAmbientOcclusionInternal", &SetFlag<&GraphicsSettings::ambientOcclusion>);
		Add("Crucible.QualitySettings::GetBloomInternal", &GetFlag<&GraphicsSettings::bloom>);
		Add("Crucible.QualitySettings::SetBloomInternal", &SetFlag<&GraphicsSettings::bloom>);
		Add("Crucible.QualitySettings::GetVolumetricFogInternal", &GetFlag<&GraphicsSettings::volumetricFog>);
		Add("Crucible.QualitySettings::SetVolumetricFogInternal", &SetFlag<&GraphicsSettings::volumetricFog>);
		Add("Crucible.QualitySettings::GetVSyncInternal", &GetFlag<&GraphicsSettings::vsync>);
		Add("Crucible.QualitySettings::SetVSyncInternal", &SetFlag<&GraphicsSettings::vsync>);
		Add("Crucible.QualitySettings::GetFrameRateLimitInternal", &ManagedGetFrameRateLimit);
		Add("Crucible.QualitySettings::SetFrameRateLimitInternal", &ManagedSetFrameRateLimit);
		Add("Crucible.QualitySettings::GetBrightnessInternal", &GetFloat<&GraphicsSettings::brightness>);
		Add("Crucible.QualitySettings::SetBrightnessInternal", &SetFloat<&GraphicsSettings::brightness, &BrightnessClamp>);
		Add("Crucible.QualitySettings::GetFieldOfViewOffsetInternal", &GetFloat<&GraphicsSettings::fovOffset>);
		Add("Crucible.QualitySettings::SetFieldOfViewOffsetInternal", &SetFloat<&GraphicsSettings::fovOffset, &FovOffsetClamp>);
		Add("Crucible.QualitySettings::GetVignetteInternal", &GetFlag<&GraphicsSettings::vignette>);
		Add("Crucible.QualitySettings::SetVignetteInternal", &SetFlag<&GraphicsSettings::vignette>);
		Add("Crucible.QualitySettings::GetFilmGrainInternal", &GetFlag<&GraphicsSettings::filmGrain>);
		Add("Crucible.QualitySettings::SetFilmGrainInternal", &SetFlag<&GraphicsSettings::filmGrain>);
		Add("Crucible.QualitySettings::GetChromaticAberrationInternal", &GetFlag<&GraphicsSettings::chromaticAberration>);
		Add("Crucible.QualitySettings::SetChromaticAberrationInternal", &SetFlag<&GraphicsSettings::chromaticAberration>);
		Add("Crucible.QualitySettings::SaveInternal", &ManagedSave);
		Add("Crucible.QualitySettings::RevertInternal", &ManagedRevert);
		Add("Crucible.QualitySettings::ResetToDefaultsInternal", &ManagedResetToDefaults);
	}
}
#endif
