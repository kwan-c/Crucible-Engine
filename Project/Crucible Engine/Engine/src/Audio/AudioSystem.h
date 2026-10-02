#pragma once

// STL
#include <cstdint>
#include <string>

// Crucible engine
#include "EngineExport.h"
#include "Math/Math.h"
#include "Audio/AudioBuses.h"

// ---------------------------------------------------------------------------
// Crucible::Audio
//
// FMOD keeps its own world of loaded sounds and playing channels. The ECS
// keeps the game world. This class wraps FMOD and deliberately knows nothing
// about entities - it deals in handles, and ECSAudioSystem turns AudioSource
// components into calls on it. The same split as Physics and ECSPhysicsSystem.
//
// The Unity names, and what they are here:
//
//   Unity                 here
//   -----                 ----
//   AudioClip             AudioClip       a loaded sound file, held by handle
//   AudioSource.Play()    Play()          returns an AudioVoice: one playing instance
//   AudioListener         SetListener()   the ears, usually the camera
//   AudioMixerGroup       AudioBus        a volume knob, see AudioBuses.h
//
// One frame:
//
//   during the frame   Play(), Stop(), SetPosition(), SetListener()...
//   Update()           once, after all of that: FMOD applies the changes,
//                      works out 3D panning, and frees voices that ended
//
// No FMOD header is included here on purpose. Game should build without
// FMOD's include path, swapping FMOD one day should not touch Game, and an
// exported class with std:: members warns C4251 at /W4. So everything FMOD
// lives behind Impl, in AudioInternal.h and the .cpp files.
// ---------------------------------------------------------------------------

namespace Crucible {

    // ------------------------------------------------------------------- //
    // HANDLES AND DESCRIPTIONS
    // ------------------------------------------------------------------- //

    /*
        A loaded sound. The sound itself lives inside FMOD, so you never hold
        a pointer to it - you hold this. Index 0 is never handed out, so a
        default-constructed handle means "no clip". The generation rises when
        a slot is reused, so a handle to an unloaded clip stops matching
        instead of playing whatever was loaded into its slot afterwards.
    */
    struct AudioClip {

        std::uint32_t m_index      = 0;
        std::uint32_t m_generation = 0;

        bool IsValid() const { return m_index != 0; }

    };  // AUDIOCLIP STRUCT

    /*
        One playing instance of a clip: the same gunshot fired twice is two
        voices. Unity keeps this hidden inside AudioSource; here Play hands it
        back so you can stop it, move it or change its volume.

        It wraps FMOD's own channel handle, stored untyped so this header needs
        no FMOD include. FMOD keeps that handle safe to use after the sound
        ends - calls on it just do nothing - so unlike AudioClip there is no
        slot and no generation. IsValid only means "this came from a Play";
        IsPlaying is how you ask whether it is still sounding.
    */
    struct AudioVoice {

        void* m_channel = nullptr;

        bool IsValid() const { return m_channel != nullptr; }

    };  // AUDIOVOICE STRUCT

    // Unity's AudioClip "Load Type" import setting, with the two M1 needs.
    enum class AudioClipLoadType : std::uint8_t {

        DecompressOnLoad,   // the whole file decoded into memory at load. Starts instantly, plays many at once. Short effects
        Streaming           // read from disk while it plays. Tiny memory, but only one voice at a time. Music

    };  // AUDIOCLIPLOADTYPE ENUM

    /*
        Everything Play needs, in one place, so a call reads Play(clip, desc)
        rather than eight bare numbers. The fields are AudioSource's, with
        Unity's defaults.
    */
    struct AudioPlayDesc {

        AudioBus   m_bus          = AudioBus::SFX;                  // Unity's "Output" mixer group
        float      m_volume       = 1.0f;                           // 0 silent, 1 as recorded. Clamped to 0..1
        float      m_pitch        = 1.0f;                           // 1 normal, 2 an octave up and twice as fast
        bool       m_loop         = false;                          // decided here, not at load, so one clip can do both
        float      m_spatialBlend = 0.0f;                           // 0 = 2D, same in both ears (music). 1 = 3D, placed in the world
        Math::vec3 m_position     = Math::vec3(0.0f, 0.0f, 0.0f);   // world position in metres. Ignored at spatialBlend 0
        float      m_minDistance  = 1.0f;                           // full volume closer than this
        float      m_maxDistance  = 500.0f;                         // stops getting quieter further than this

    };  // AUDIOPLAYDESC STRUCT

    // ------------------------------------------------------------------- //
    // THE SYSTEM
    // ------------------------------------------------------------------- //

    class ENGINE_API Audio {

    public:

        Audio();
        ~Audio();

        Audio(const Audio&)            = delete;   // one owner. Copying would mean two objects releasing one FMOD system
        Audio& operator=(const Audio&) = delete;

        // --- lifecycle ---

        // Starts FMOD and makes one ChannelGroup per bus. Returns false and
        // logs if FMOD will not start - no sound device, a missing fmod.dll -
        // and the game carries on silent: every call below is a safe no-op
        // while IsInitialised is false.
        bool Init();

        // Releases every clip, then the buses, then FMOD itself. Safe to call
        // twice, and the destructor calls it for you.
        void Shutdown();

        bool IsInitialised() const;

        // Once per frame, after everything else this frame has called. Changes
        // to voices only reach the speakers here, and finished voices are
        // only freed here. ECSAudioSystem calls it; nobody else should.
        void Update();

        // --- clips (Unity's AudioClip) ---

        // Loads a file from disk. `path` is a real file path, not a virtual
        // asset path - turning one into the other is the caller's job.
        // Invalid handle, with the reason logged, if the file will not load.
        AudioClip LoadClip(const std::string& path, AudioClipLoadType loadType = AudioClipLoadType::DecompressOnLoad);

        // Stops every voice still playing it, then frees it. Handles to it
        // stay invalid afterwards rather than becoming someone else's clip.
        void UnloadClip(const AudioClip& clip);

        // Unity's AudioClip.length: how long one play-through lasts, in
        // seconds. 0 for a stale or invalid handle.
        float GetClipLength(const AudioClip& clip) const;

        // --- voices (Unity's AudioSource.Play and friends) ---

        // Starts one voice. Keep the handle to control it later, or ignore it
        // for a fire-and-forget shot. Invalid handle if the clip is stale or
        // FMOD is out of voices. A Streaming clip has one read position, so
        // playing it again takes over from the voice already playing it.
        AudioVoice Play(const AudioClip& clip, const AudioPlayDesc& desc = AudioPlayDesc{});

        void Stop(const AudioVoice& voice);

        // False once the sound has ended or been stopped. A looping voice
        // stays true until Stop.
        bool IsPlaying(const AudioVoice& voice) const;

        void SetVolume(const AudioVoice& voice, float volume);         // 0..1, like AudioPlayDesc::m_volume
        void SetPitch(const AudioVoice& voice, float pitch);
        void SetPosition(const AudioVoice& voice, const Math::vec3& position);   // for a sound that follows something

        // --- the listener (Unity's AudioListener) ---

        // Where the ears are and which way they face. Usually the camera's
        // world transform, set every frame. `forward` and `up` are normalised
        // for you, but they must be at right angles - the camera's own pair
        // always is - or left and right come out wrong.
        void SetListener(const Math::vec3& position, const Math::vec3& forward, const Math::vec3& up);

        // --- buses (Unity's AudioMixerGroup volume) ---

        // What an options-menu slider calls. 0..1, clamped. Master scales
        // everything, including what Music and SFX already scaled.
        void  SetBusVolume(const AudioBus& bus, float volume);
        float GetBusVolume(const AudioBus& bus) const;

    private:

        // Everything FMOD hides in here, defined in AudioInternal.h. A raw
        // pointer rather than a unique_ptr for the same reason as Physics: an
        // exported class with a std:: member warns C4251. The destructor
        // deletes it.
        struct Impl;
        Impl* m_impl = nullptr;

    };  // AUDIO CLASS

}   // NAMESPACE CRUCIBLE
