#pragma once

// FMOD
#include <fmod.hpp>

// STL
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Crucible engine
#include "Audio/AudioSystem.h"

// ---------------------------------------------------------------------------
// Engine-private: the inside of Crucible::Audio.
//
// AudioSystem.h promises Game that it will never show it an FMOD header, and
// keeps that promise by declaring `struct Impl;` and holding a pointer to it.
// This file is where Impl actually gets defined, so every FMOD object the
// system owns lives here.
//
// Only Audio/*.cpp may include it. The day something outside Audio/ needs it,
// that is a sign the class is missing a function, not that this header should
// travel.
// ---------------------------------------------------------------------------

namespace Crucible {

    struct Audio::Impl {

        // --- FMOD ----------------------------------------------------------- //
        // Plain pointers, not smart ones: FMOD objects are freed by calling
        // release() on them, not by delete, and the order matters (sounds,
        // then our ChannelGroups, then the system). Shutdown does that by
        // hand, so declaration order here is not load-bearing the way it is
        // in PhysicsInternal.h.

        FMOD::System* m_system = nullptr;

        // One ChannelGroup per bus, indexed by AudioBus. [Master] is FMOD's own
        // master group, which FMOD owns and frees itself; the others are ours.
        // The {} sets every entry to nullptr until Init fills them in.
        std::array<FMOD::ChannelGroup*, kAudioBusCount> m_buses {};

        bool m_initialised    = false;
        bool m_comInitialised = false;   // our CoInitializeEx succeeded, so Shutdown owes Windows a CoUninitialize

        // --- CLIPS ---------------------------------------------------------- //

        struct Clip {

            FMOD::Sound*      m_sound       = nullptr;
            std::string       m_path;                                    // only for logs: "Audio/gunshot.wav" reads better than "clip 3"
            AudioClipLoadType m_loadType    = AudioClipLoadType::DecompressOnLoad;
            FMOD::Channel*    m_streamVoice = nullptr;                   // Streaming only: its latest voice, so Play can warn before taking it over
            std::uint32_t     m_generation  = 1;                         // rises each time the slot is reused
            bool              m_inUse       = false;

        };  // CLIP STRUCT

        std::vector<Clip>          m_clips;              // slot 0 is never handed out, so index 0 can mean "no clip"
        std::vector<std::uint32_t> m_freeClipSlots;      // unloaded slots, reused before the vector grows

        // --- HELPERS THE .CPP FILES SHARE ----------------------------------- //

        // The clip behind a handle, or null if the handle is stale. The
        // generation must match, which is what stops a reused slot answering
        // to a handle from the clip that used to live there.
        Clip* Slot(const AudioClip& clip) {
            if (!m_initialised || !clip.IsValid() || clip.m_index >= m_clips.size()) return nullptr;
            Clip& slot = m_clips[clip.m_index];
            return (slot.m_inUse && slot.m_generation == clip.m_generation) ? &slot : nullptr;
        }

        // The const version, written once by borrowing the non-const one.
        const Clip* Slot(const AudioClip& clip) const {
            return const_cast<Impl*>(this)->Slot(clip);
        }

        // The ChannelGroup behind a bus, or null for a value outside the enum.
        FMOD::ChannelGroup* Bus(const AudioBus& bus) const {
            const auto index = static_cast<std::size_t>(bus);
            return index < kAudioBusCount ? m_buses[index] : nullptr;
        }

        // AudioVoice stores FMOD's Channel* untyped so AudioSystem.h needs no
        // FMOD include. This is the one place it gets its type back.
        static FMOD::Channel* ToChannel(const AudioVoice& voice) {
            return static_cast<FMOD::Channel*>(voice.m_channel);
        }

        // Every FMOD call goes through this. True on FMOD_OK; otherwise logs
        // `what` with FMOD's own explanation and returns false. A voice that
        // has ended or been stolen answers INVALID_HANDLE or CHANNEL_STOLEN:
        // that is normal, so those two return false without a log line.
        // Defined in AudioSystem.cpp, the only file that includes
        // fmod_errors.h.
        static bool Check(FMOD_RESULT result, const char* what);

        // Frees whatever exists - sounds, then our ChannelGroups, then FMOD,
        // then COM - and resets every member. Copes with a half-built state,
        // which is why Init can call it when a step fails part way, and
        // Shutdown calls it for the normal case.
        void ReleaseAll();

    };  // AUDIO::IMPL STRUCT

}   // NAMESPACE CRUCIBLE
