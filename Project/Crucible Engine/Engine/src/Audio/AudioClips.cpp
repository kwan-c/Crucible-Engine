#include "AudioSystem.h"

// STL
#include <format>

// Crucible engine
#include "Audio/AudioInternal.h"
#include "Diagnostics/Logger.h"

namespace Crucible {

    /*!
        \brief
        Loads a sound file into FMOD and hands back a handle to it.
        \param path
        A real file path. Virtual asset paths are resolved by the caller.
        \param loadType
        DecompressOnLoad for short effects, Streaming for music.
        \return
        An invalid handle, with the reason logged, if the file will not load.
    */
    AudioClip Audio::LoadClip(const std::string& path, AudioClipLoadType loadType) {

        Impl& impl = *m_impl;
        if (!impl.m_initialised) return AudioClip{};

        if (path.empty()) {
            CRU_ERROR("Audio::LoadClip: empty path");
            return AudioClip{};
        }

        // Every clip is created 3D, music included. spatialBlend is chosen per
        // Play, and FMOD cannot place a sound in the world unless it was
        // created 3D - while a 3D sound at spatialBlend 0 plays exactly like a
        // 2D one. So 3D always, and Play decides how much of it to use.
        const FMOD_MODE mode = FMOD_3D | (loadType == AudioClipLoadType::Streaming ? FMOD_CREATESTREAM : FMOD_CREATESAMPLE);

        FMOD::Sound* sound = nullptr;
        if (!Impl::Check(impl.m_system->createSound(path.c_str(), mode, nullptr, &sound),
                         std::format("Audio::LoadClip(\"{}\")", path).c_str()))
            return AudioClip{};

        // A free slot if there is one, so unloading and reloading does not
        // grow the list forever. Its generation was already raised when the
        // last clip in it was unloaded.
        std::uint32_t index = 0;
        if (!impl.m_freeClipSlots.empty()) {
            index = impl.m_freeClipSlots.back();
            impl.m_freeClipSlots.pop_back();
        }
        else {
            index = static_cast<std::uint32_t>(impl.m_clips.size());
            impl.m_clips.emplace_back();
        }

        Impl::Clip& slot = impl.m_clips[index];
        slot.m_sound       = sound;
        slot.m_path        = path;
        slot.m_loadType    = loadType;
        slot.m_streamVoice = nullptr;
        slot.m_inUse       = true;

        return AudioClip{ index, slot.m_generation };

    }   // Audio::LoadClip()

    /*!
        \brief
        Stops every voice playing the clip and frees it. Stale and invalid
        handles do nothing.
    */
    void Audio::UnloadClip(const AudioClip& clip) {

        Impl& impl = *m_impl;
        Impl::Clip* slot = impl.Slot(clip);
        if (slot == nullptr) return;

        // release() stops any voice still playing this sound before freeing
        // it, so no voice is left reading memory that is gone.
        Impl::Check(slot->m_sound->release(), std::format("Audio::UnloadClip(\"{}\")", slot->m_path).c_str());

        slot->m_sound       = nullptr;
        slot->m_path.clear();
        slot->m_streamVoice = nullptr;
        slot->m_inUse       = false;
        ++slot->m_generation;                       // every handle to the old clip stops matching here
        impl.m_freeClipSlots.push_back(clip.m_index);

    }   // Audio::UnloadClip()

    float Audio::GetClipLength(const AudioClip& clip) const {

        const Impl::Clip* slot = m_impl->Slot(clip);
        if (slot == nullptr) return 0.0f;

        // FMOD answers in whole milliseconds; a stream reads this from the
        // file's header, so it is cheap for music too.
        unsigned int milliseconds = 0;
        if (!Impl::Check(slot->m_sound->getLength(&milliseconds, FMOD_TIMEUNIT_MS), "Audio::GetClipLength: Sound::getLength"))
            return 0.0f;

        return static_cast<float>(milliseconds) / 1000.0f;

    }   // Audio::GetClipLength()

}   // NAMESPACE CRUCIBLE
