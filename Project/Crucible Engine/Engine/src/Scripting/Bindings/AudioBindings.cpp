/*************************************************************************
* \file:         AudioBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         Internal calls behind the C# AudioSource class
*                (ManagedScripts/Runtime/AudioSource.cs).
*
* \details
*    Bindings only: everything goes through the AudioSource component
*    (Audio/AudioComponents.h, Hugo's), exactly as gameplay C++ would.
*    Play / Stop / PlayOneShot leave a request on the component that
*    ECSAudioSystem carries out later in the same frame (it runs in
*    PreRender, after scripts). Volume and pitch changes reach a sound that
*    is already playing; the other settings apply from the next Play.
*
*    Same shape as TransformBindings: ResolveEntity, then Read (getters) or
*    Write (setters) the component, return 1 on success, 0 on failure.
*    Bools cross as int, the 64-bit clip id as two int halves.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "Audio/AudioComponents.h"
#include "AssetPipe/Assetmanager.h"
#include "Configuration/UserSettings.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#if CRUCIBLE_WITH_MONO
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		/*********************************************************************
		 \brief  The entity's AudioSource for reading, or nullptr (entity gone,
		         not Play, or no AudioSource).
		 *********************************************************************/
		const AudioSource* ReadSource(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Read<AudioSource>(entity);
		}

		/*********************************************************************
		 \brief  The entity's AudioSource for writing, or nullptr.
		 *********************************************************************/
		AudioSource* WriteSource(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Write<AudioSource>(entity);
		}

		// ======================================================================
		// Play / Stop / PlayOneShot / IsPlaying
		// ======================================================================

		/*********************************************************************
		 \brief  C# AudioSource.Play / Stop / PlayOneShot (action 0 / 1 / 2).
		 *********************************************************************/
		int32_t ManagedAudioRequest(int32_t index, int32_t generation, int32_t action) noexcept
		{
			try
			{
				AudioSource* source = WriteSource(index, generation);
				if (!source)
					return 0;
				switch (action)
				{
				case 0:
					source->Play();
					return 1;
				case 1:
					source->Stop();
					return 1;
				case 2:
					source->PlayOneShot();
					return 1;
				default:
					return 0;
				}
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# AudioSource.PlayOneShot(clip, volumeScale): queues another
		         clip on the component (AudioSource::PlayOneShot(clip, scale)),
		         without changing its Clip.
		 \return 0 if the clip isn't an audio asset, the scale is NaN, or
		         the frame's queue (AudioSource::kMaxClipShots) is full.
		 *********************************************************************/
		int32_t ManagedAudioPlayOneShotClip(int32_t index, int32_t generation, int32_t low, int32_t high,
		                                    float volumeScale) noexcept
		{
			try
			{
				const uint64_t clip =
				    (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | static_cast<uint32_t>(low);
				if (!std::isfinite(volumeScale) || !EntityContext::IsAudioAsset(clip))
					return 0;
				AudioSource* source = WriteSource(index, generation);
				if (!source || source->m_pendingClipShotCount >= AudioSource::kMaxClipShots)
					return 0;
				source->PlayOneShot(clip, volumeScale);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# AudioSource.IsPlaying: the Play() voice (not one-shots),
		         as ECSAudioSystem last reported it.
		 *********************************************************************/
		int32_t ManagedAudioIsPlaying(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const AudioSource* source = out ? ReadSource(index, generation) : nullptr;
				if (!source)
					return 0;
				*out = source->m_isPlaying ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Float settings
		// ======================================================================

		/*********************************************************************
		 \brief  Getter for one float setting of AudioSource.
		 *********************************************************************/
		template <float AudioSource::*Member>
		int32_t GetFloat(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				const AudioSource* source = out ? ReadSource(index, generation) : nullptr;
				if (!source)
					return 0;
				*out = source->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Setter for one float setting, clamped to [Min, Max] (given as
		         whole numbers because float template arguments need C++20
		         everywhere). NaN/infinity is rejected.
		 *********************************************************************/
		template <float AudioSource::*Member, int Min, int Max>
		int32_t SetFloat(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value))
					return 0;
				AudioSource* source = WriteSource(index, generation);
				if (!source)
					return 0;
				source->*Member = std::clamp(*value, static_cast<float>(Min), static_cast<float>(Max));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		constexpr int NoLimit = std::numeric_limits<int>::max();

		// ======================================================================
		// Loop and Clip
		// ======================================================================

		int32_t ManagedAudioGetLoop(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const AudioSource* source = out ? ReadSource(index, generation) : nullptr;
				if (!source)
					return 0;
				*out = source->m_loop ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedAudioSetLoop(int32_t index, int32_t generation, int32_t loop) noexcept
		{
			try
			{
				AudioSource* source = WriteSource(index, generation);
				if (!source)
					return 0;
				source->m_loop = loop != 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# AudioSource.Clip (get): the clip's asset guid as two halves.
		 *********************************************************************/
		int32_t ManagedAudioGetClip(int32_t index, int32_t generation, int32_t* low, int32_t* high) noexcept
		{
			try
			{
				const AudioSource* source = (low && high) ? ReadSource(index, generation) : nullptr;
				if (!source)
					return 0;
				*low = static_cast<int32_t>(static_cast<uint32_t>(source->m_clip));
				*high = static_cast<int32_t>(static_cast<uint32_t>(source->m_clip >> 32));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# AudioSource.Clip (set). 0 clears it; anything else must be
		         an audio asset. Used from the next Play / PlayOneShot.
		 *********************************************************************/
		int32_t ManagedAudioSetClip(int32_t index, int32_t generation, int32_t low, int32_t high) noexcept
		{
			try
			{
				const uint64_t clip =
				    (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | static_cast<uint32_t>(low);
				if (clip != 0 && !EntityContext::IsAudioAsset(clip))
					return 0;
				AudioSource* source = WriteSource(index, generation);
				if (!source)
					return 0;
				source->m_clip = clip;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# AudioClip.Name: the audio file's name ("cannon5.wav"), or
		         null if the asset is gone.
		 *********************************************************************/
		MonoString* ManagedAudioClipGetName(int32_t low, int32_t high) noexcept
		{
			try
			{
				AssetManager* assets = EntityContext::GetAssets();
				const uint64_t clip =
				    (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | static_cast<uint32_t>(low);
				const AssetEntry* entry = assets ? assets->FindByGuid(xresource::instance_guid{clip}) : nullptr;
				if (!entry)
					return nullptr;
				std::wstring wide;
				if (!ScriptingDetail::ToUtf16(std::filesystem::path(entry->virtualPath).filename().string(), wide))
					return nullptr;
				return mono_string_new_utf16(mono_domain_get(), reinterpret_cast<const mono_unichar2*>(wide.data()),
				                             static_cast<int32_t>(wide.size()));
			}
			catch (...)
			{
				return nullptr;
			}
		}

		// ======================================================================
		// C# Audio (static): bus volumes and PlayClipAtPoint
		// ======================================================================

		// C# AudioBus (Audio.cs) passes these numbers.
		static_assert(static_cast<int>(AudioBus::Master) == 0 && static_cast<int>(AudioBus::Music) == 1 &&
		                  static_cast<int>(AudioBus::SFX) == 2,
		              "AudioBuses.h changed - update the AudioBus enum in ManagedScripts/Runtime/Audio.cs");

		// Core's UserSettings (SetUserSettings). ECSAudioSystem reads its bus
		// volumes every frame, so writing here is heard on the next frame.
		thread_local UserSettings* userSettings = nullptr;

		/*********************************************************************
		 \brief  The AudioSettings slot for a bus (0 Master, 1 Music, 2 SFX -
		         AudioBus order), or nullptr.
		 *********************************************************************/
		float* BusVolume(int32_t bus)
		{
			if (!userSettings)
				return nullptr;
			switch (static_cast<AudioBus>(bus))
			{
			case AudioBus::Master:
				return &userSettings->audio.masterVolume;
			case AudioBus::Music:
				return &userSettings->audio.musicVolume;
			case AudioBus::SFX:
				return &userSettings->audio.sfxVolume;
			default:
				return nullptr;
			}
		}

		/*********************************************************************
		 \brief  C# Audio.GetBusVolume / MasterVolume etc. (get).
		 *********************************************************************/
		int32_t ManagedAudioGetBusVolume(int32_t bus, float* out) noexcept
		{
			try
			{
				const float* volume = out ? BusVolume(bus) : nullptr;
				if (!volume)
					return 0;
				*out = *volume;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Audio.SetBusVolume / MasterVolume etc. (set), clamped 0..1.
		         Not saved to disk until Audio.SaveSettings.
		 *********************************************************************/
		int32_t ManagedAudioSetBusVolume(int32_t bus, float volume) noexcept
		{
			try
			{
				float* slot = BusVolume(bus);
				if (!slot || !std::isfinite(volume))
					return 0;
				*slot = std::clamp(volume, 0.0f, 1.0f);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Audio.SaveSettings: writes every player setting (volumes
		         included) to UserSettings.json, as Core::SaveUserSettings does.
		 *********************************************************************/
		int32_t ManagedAudioSaveSettings() noexcept
		{
			try
			{
				return userSettings && UserSettingsLoader::Save(*userSettings) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Audio.PlayClipAtPoint. Unity's approach: a temporary
		         "One Shot Audio" entity with an AudioSource (Play On Awake, 3D)
		         at the position, destroyed once the sound has finished
		         (EntityContext::UpdateOneShotAudio). ECSAudioSystem plays it
		         later this frame, so pause, Stop and the bus volumes all apply
		         as for any other AudioSource.
		 \return 1 with the temporary entity's (index, generation), else 0.
		 *********************************************************************/
		int32_t ManagedAudioPlayClipAtPoint(int32_t low, int32_t high, const ScriptingDetail::ScriptVec3* position,
		                                    float volume, int32_t* outIndex, int32_t* outGeneration) noexcept
		{
			try
			{
				if (!position || !outIndex || !outGeneration || !std::isfinite(volume) || !std::isfinite(position->x) ||
				    !std::isfinite(position->y) || !std::isfinite(position->z))
					return 0;
				*outIndex = 0;
				*outGeneration = 0;
				const uint64_t clip =
				    (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | static_cast<uint32_t>(low);
				World* world = EntityContext::GetWorld();
				if (!world || !EntityContext::IsAudioAsset(clip) || !EntityContext::InPlaySession())
					return 0;

				const Entity entity = world->CreateNamedEntity("One Shot Audio");
				if (Transform* transform = world->Write<Transform>(entity))
					transform->positions = {position->x, position->y, position->z};
				AudioSource source;
				source.m_clip = clip;
				source.m_bus = AudioBus::SFX;
				source.m_playOnAwake = true;
				source.m_loop = false;
				source.m_volume = std::clamp(volume, 0.0f, 1.0f);
				source.m_spatialBlend = 1.0f; // heard from the position
				world->AddComponent<AudioSource>(entity, source);

				EntityContext::TrackOneShotAudio(entity);
				*outIndex = static_cast<int32_t>(entity.index);
				*outGeneration = static_cast<int32_t>(entity.generation);
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

	void SetUserSettings(UserSettings* settings)
	{
		userSettings = settings;
	}

	UserSettings* GetUserSettings()
	{
		return userSettings;
	}

	void RegisterAudioBindings()
	{
		Add("Crucible.Audio::GetBusVolumeInternal", &ManagedAudioGetBusVolume);
		Add("Crucible.Audio::SetBusVolumeInternal", &ManagedAudioSetBusVolume);
		Add("Crucible.Audio::SaveSettingsInternal", &ManagedAudioSaveSettings);
		Add("Crucible.Audio::PlayClipAtPointInternal", &ManagedAudioPlayClipAtPoint);

		Add("Crucible.AudioSource::RequestInternal", &ManagedAudioRequest);
		Add("Crucible.AudioSource::PlayOneShotClipInternal", &ManagedAudioPlayOneShotClip);
		Add("Crucible.AudioSource::IsPlayingInternal", &ManagedAudioIsPlaying);

		Add("Crucible.AudioSource::GetVolumeInternal", &GetFloat<&AudioSource::m_volume>);
		Add("Crucible.AudioSource::SetVolumeInternal", &SetFloat<&AudioSource::m_volume, 0, 1>);
		Add("Crucible.AudioSource::GetPitchInternal", &GetFloat<&AudioSource::m_pitch>);
		Add("Crucible.AudioSource::SetPitchInternal", &SetFloat<&AudioSource::m_pitch, 0, NoLimit>);
		Add("Crucible.AudioSource::GetSpatialBlendInternal", &GetFloat<&AudioSource::m_spatialBlend>);
		Add("Crucible.AudioSource::SetSpatialBlendInternal", &SetFloat<&AudioSource::m_spatialBlend, 0, 1>);
		Add("Crucible.AudioSource::GetMinDistanceInternal", &GetFloat<&AudioSource::m_minDistance>);
		Add("Crucible.AudioSource::SetMinDistanceInternal", &SetFloat<&AudioSource::m_minDistance, 0, NoLimit>);
		Add("Crucible.AudioSource::GetMaxDistanceInternal", &GetFloat<&AudioSource::m_maxDistance>);
		Add("Crucible.AudioSource::SetMaxDistanceInternal", &SetFloat<&AudioSource::m_maxDistance, 0, NoLimit>);

		Add("Crucible.AudioSource::GetLoopInternal", &ManagedAudioGetLoop);
		Add("Crucible.AudioSource::SetLoopInternal", &ManagedAudioSetLoop);
		Add("Crucible.AudioSource::GetClipInternal", &ManagedAudioGetClip);
		Add("Crucible.AudioSource::SetClipInternal", &ManagedAudioSetClip);
		Add("Crucible.AudioClip::GetNameInternal", &ManagedAudioClipGetName);
	}
}
#endif
