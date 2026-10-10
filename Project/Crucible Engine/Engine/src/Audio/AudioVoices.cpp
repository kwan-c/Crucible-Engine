/*************************************************************************
* \file:         AudioVoices.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          1 October 2026
* \version       1.2
* \brief
*                Voices, one playing instance of a clip: play, stop,
*                pause, volume, pitch and 3D position. Also bus volumes
*                and the listener.
*
* \details
*                Each voice starts paused, is set up, then unpaused, so it
*                never makes a sound with the wrong settings. A voice
*                whose sound has already ended is not an error, so FMOD's
*                invalid-handle result is not logged.
*
* \history
*    -v1.0       first version
*    -v1.1       SetPaused (Abhishek)
*    -v1.2       linear rolloff
*
**************************************************************************/
#include "AudioSystem.h"

// STL
#include <algorithm>    // std::clamp

// Crucible engine
#include "Audio/AudioInternal.h"
#include "Diagnostics/Logger.h"

namespace {

    using namespace Crucible;

    // Our vec3 and FMOD's vector hold the same three floats; this is the one
    // place one becomes the other.
    FMOD_VECTOR ToFmod(const Math::vec3& v) { return FMOD_VECTOR{ v.x, v.y, v.z }; }

    // Unity keeps volume and spatialBlend in 0..1. FMOD accepts more - a
    // volume of 3 amplifies and distorts - so values are clamped on the way in.
    float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    // ------------------------------------------------------------------- //
    // PLAYING
    // ------------------------------------------------------------------- //

    /*!
        \brief
        Starts one voice of a clip, set up from desc before it makes a sound.
        \return
        The voice, or an invalid handle if the clip is stale or FMOD refused.
    */
    AudioVoice Audio::Play(const AudioClip& clip, const AudioPlayDesc& desc) {

        Impl& impl = *m_impl;
        Impl::Clip* slot = impl.Slot(clip);
        if (slot == nullptr) return AudioVoice{};

        FMOD::ChannelGroup* bus = impl.Bus(desc.m_bus);
        if (bus == nullptr) {
            CRU_ERROR("Audio::Play: \"{}\" asked for a bus that does not exist", slot->m_path);
            return AudioVoice{};
        }

        // A stream has one read position in its file, so a second voice takes
        // it over from the first. Usually that is a mistake - the clip wanted
        // DecompressOnLoad - so say so rather than letting the music cut out.
        if (slot->m_loadType == AudioClipLoadType::Streaming && slot->m_streamVoice != nullptr) {
            bool playing = false;
            if (Impl::Check(slot->m_streamVoice->isPlaying(&playing), "Audio::Play: Channel::isPlaying") && playing)
                CRU_WARN("Audio::Play: \"{}\" is Streaming and already playing - the new voice takes over from the old one", slot->m_path);
        }

        // PAUSED FIRST -------------------------------------------------------- //
        // Started paused, set up, and only then unpaused. Started unpaused, the
        // first few milliseconds would play at full volume from the origin
        // before the settings below land - a click, or a shot from the wrong side.
        FMOD::Channel* channel = nullptr;
        if (!Impl::Check(impl.m_system->playSound(slot->m_sound, bus, true, &channel), "Audio::Play: System::playSound"))
            return AudioVoice{};

        const FMOD_VECTOR position = ToFmod(desc.m_position);
        const FMOD_VECTOR velocity = FMOD_VECTOR{ 0.0f, 0.0f, 0.0f };   // no Doppler yet

        Impl::Check(channel->setVolume(Clamp01(desc.m_volume)),                            "Audio::Play: Channel::setVolume");
        Impl::Check(channel->setPitch(desc.m_pitch),                                       "Audio::Play: Channel::setPitch");
        // Loop and rolloff in one call: setMode only changes the groups of flags it is given,
        // so the FMOD_3D the clip was created with is left as it is. INVERSE is FMOD's default
        // and Unity's Logarithmic; it is still set explicitly so a reused channel cannot keep
        // the previous voice's Linear.
        const FMOD_MODE loop    = desc.m_loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;
        const FMOD_MODE rolloff = desc.m_rolloffMode == AudioRolloffMode::Linear ? FMOD_3D_LINEARROLLOFF : FMOD_3D_INVERSEROLLOFF;
        Impl::Check(channel->setMode(loop | rolloff),                                      "Audio::Play: Channel::setMode");
        Impl::Check(channel->set3DLevel(Clamp01(desc.m_spatialBlend)),                     "Audio::Play: Channel::set3DLevel");
        Impl::Check(channel->set3DAttributes(&position, &velocity),                        "Audio::Play: Channel::set3DAttributes");
        Impl::Check(channel->set3DMinMaxDistance(desc.m_minDistance, desc.m_maxDistance),  "Audio::Play: Channel::set3DMinMaxDistance");
        Impl::Check(channel->setPaused(false),                                             "Audio::Play: Channel::setPaused");

        if (slot->m_loadType == AudioClipLoadType::Streaming) slot->m_streamVoice = channel;

        return AudioVoice{ channel };

    }   // Audio::Play()

    // ------------------------------------------------------------------- //
    // CONTROLLING A VOICE
    // ------------------------------------------------------------------- //
    // Each of these first asks whether audio is running and the voice ever
    // played. A voice that has since ended answers INVALID_HANDLE, which
    // Check swallows: stopping a gunshot that already finished is not an error.

    void Audio::Stop(const AudioVoice& voice) {
        if (!m_impl->m_initialised || !voice.IsValid()) return;
        Impl::Check(Impl::ToChannel(voice)->stop(), "Audio::Stop: Channel::stop");
    }

    bool Audio::IsPlaying(const AudioVoice& voice) const {
        if (!m_impl->m_initialised || !voice.IsValid()) return false;
        bool playing = false;
        return Impl::Check(Impl::ToChannel(voice)->isPlaying(&playing), "Audio::IsPlaying: Channel::isPlaying") && playing;
    }

    void Audio::SetVolume(const AudioVoice& voice, float volume) {
        if (!m_impl->m_initialised || !voice.IsValid()) return;
        Impl::Check(Impl::ToChannel(voice)->setVolume(Clamp01(volume)), "Audio::SetVolume: Channel::setVolume");
    }

    void Audio::SetPitch(const AudioVoice& voice, float pitch) {
        if (!m_impl->m_initialised || !voice.IsValid()) return;
        Impl::Check(Impl::ToChannel(voice)->setPitch(pitch), "Audio::SetPitch: Channel::setPitch");
    }

    void Audio::SetPosition(const AudioVoice& voice, const Math::vec3& position) {
        if (!m_impl->m_initialised || !voice.IsValid()) return;
        const FMOD_VECTOR where    = ToFmod(position);
        const FMOD_VECTOR velocity = FMOD_VECTOR{ 0.0f, 0.0f, 0.0f };
        Impl::Check(Impl::ToChannel(voice)->set3DAttributes(&where, &velocity), "Audio::SetPosition: Channel::set3DAttributes");
    }

    void Audio::SetPaused(const AudioVoice& voice, bool paused) {
        if (!m_impl->m_initialised || !voice.IsValid()) return;
        Impl::Check(Impl::ToChannel(voice)->setPaused(paused), "Audio::SetPaused: Channel::setPaused");
    }

    // ------------------------------------------------------------------- //
    // THE LISTENER AND THE BUSES
    // ------------------------------------------------------------------- //

    /*!
        \brief
        Puts the ears at a position, facing a direction. Listener 0 is the only
        one: FMOD supports split-screen listeners, M1 does not need them.
    */
    void Audio::SetListener(const Math::vec3& position, const Math::vec3& forward, const Math::vec3& up) {

        if (!m_impl->m_initialised) return;

        // Normalised here, the way Raycast normalises its direction, so the
        // caller can pass a camera axis without thinking about its length.
        const FMOD_VECTOR where     = ToFmod(position);
        const FMOD_VECTOR velocity  = FMOD_VECTOR{ 0.0f, 0.0f, 0.0f };
        const FMOD_VECTOR facing    = ToFmod(glm::normalize(forward));
        const FMOD_VECTOR upwards   = ToFmod(glm::normalize(up));

        Impl::Check(m_impl->m_system->set3DListenerAttributes(0, &where, &velocity, &facing, &upwards),
                    "Audio::SetListener: System::set3DListenerAttributes");

    }   // Audio::SetListener()

    void Audio::SetBusVolume(const AudioBus& bus, float volume) {
        FMOD::ChannelGroup* group = m_impl->Bus(bus);
        if (!m_impl->m_initialised || group == nullptr) return;
        Impl::Check(group->setVolume(Clamp01(volume)), "Audio::SetBusVolume: ChannelGroup::setVolume");
    }

    // 1 - a bus's untouched volume - when there is nothing to ask, so an
    // options menu opened before audio started shows full sliders, not empty.
    float Audio::GetBusVolume(const AudioBus& bus) const {
        FMOD::ChannelGroup* group = m_impl->Bus(bus);
        if (!m_impl->m_initialised || group == nullptr) return 1.0f;
        float volume = 1.0f;
        Impl::Check(group->getVolume(&volume), "Audio::GetBusVolume: ChannelGroup::getVolume");
        return volume;
    }

}   // NAMESPACE CRUCIBLE
