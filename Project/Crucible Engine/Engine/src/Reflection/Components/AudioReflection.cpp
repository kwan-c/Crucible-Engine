/*************************************************************************
  \file       AudioReflection.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
  \brief
    RTTR registration for Audio.
  \details
    Playback flags for an entity's audio source.

    One RTTR_REGISTRATION block per .cpp is legal and is the intended way to
    split registration up: the macro defines a fixed-name struct inside an
    anonymous namespace, so each translation unit gets its own copy. What is
    NOT legal is two blocks in the same .cpp - which is what happens if one
    is ever put in a header. Never put RTTR_REGISTRATION in a header.

    Property names are what the scene file uses as JSON keys. Renaming one
    breaks every scene saved with the old name, so treat them as a file
    format, not as code.
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h
  \see
    Reflection/MathReflection.cpp, Reflection/Serialization.h, Scene/EntityIO.h
  \history
    - 1.0 (1 October 2026): Split out of the old single registration file
      ReflectionComponentRegistration.cpp, one file per component, following
      the layout of the reference engine.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"
#include "Resources/ResourceManager.h"

RTTR_REGISTRATION
{
    using namespace Crucible;
    using namespace Crucible::Math;

    rttr::registration::enumeration<AudioBus>("AudioBus")
    (
        rttr::value("Master", AudioBus::Master),
        rttr::value("Music", AudioBus::Music),
        rttr::value("SFX", AudioBus::SFX)
        );
    
    rttr::registration::enumeration<AudioClipLoadType>("AudioClipLoadType")
    (
        rttr::value("DecompressOnLoad", AudioClipLoadType::DecompressOnLoad),
        rttr::value("Streaming", AudioClipLoadType::Streaming)
        );

    rttr::registration::enumeration<AudioRolloffMode>("AudioRolloffMode")
    (
        rttr::value("Logarithmic", AudioRolloffMode::Logarithmic),
        rttr::value("Linear", AudioRolloffMode::Linear)
        );
    
    rttr::registration::class_<AudioSource>("AudioSource")
    .constructor<>()
    .property("clip", &AudioSource::m_clip)
    (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Audio))
    .property("loadType", &AudioSource::m_loadType)
    .property("output", &AudioSource::m_bus)
    .property("playOnAwake", &AudioSource::m_playOnAwake)
    .property("loop", &AudioSource::m_loop)
    .property("volume", &AudioSource::m_volume)
    (rttr::metadata(MetaDataTypes::SLIDER, vec2{ 0.0f, 1.0f }))
    .property("pitch", &AudioSource::m_pitch)
    (rttr::metadata(MetaDataTypes::SLIDER, vec2{ 0.0f, 3.0f }))      // FMOD cannot play backwards, so no negative pitch
    .property("spatialBlend", &AudioSource::m_spatialBlend)
    (rttr::metadata(MetaDataTypes::SLIDER, vec2{ 0.0f, 1.0f }))
    .property("minDistance", &AudioSource::m_minDistance)
    .property("maxDistance", &AudioSource::m_maxDistance)
    .property("rolloffMode", &AudioSource::m_rolloffMode)
    .property("isPlaying", &AudioSource::m_isPlaying)
    (rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
        rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true))
        ;
    
    rttr::registration::class_<AudioListener>("AudioListener")
    .constructor<>()
    ;
}
