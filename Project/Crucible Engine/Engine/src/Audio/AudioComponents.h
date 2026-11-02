/*************************************************************************
* \file:         AudioComponents.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          2 October 2026
* \version       1.3
* \brief
*                The audio components the designer and gameplay touch:
*                AudioSource and AudioListener.
*
* \details
*                AudioSource follows Unity's field for field where it
*                fits. Play, Stop and PlayOneShot only leave a request
*                that ECSAudioSystem carries out the same frame, so the
*                component stays plain data.
*
* \history
*    -v1.0       AudioSource
*    -v1.1       AudioListener
*    -v1.2       rolloff mode: logarithmic or linear
*    -v1.3       PlayOneShot(clip, volumeScale): a one-shot of any clip,
*                without changing m_clip (Unity's PlayOneShot(clip)),
*                for C# scripting
*
**************************************************************************/
#pragma once

// STL
#include <array>
#include <cstdint>

// Crucible engine
#include "Audio/AudioBuses.h"
#include "Audio/AudioSystem.h"
#include "ECS/Components/ComponentKind.h"

// ---------------------------------------------------------------------------
// The audio components: what gameplay and the designer actually touch.
//
//     AudioSource& gun = ...;     // on the mech, clip = the gunshot
//     gun.PlayOneShot();          // on Fire: overlaps the last shot instead of cutting it off
//
//     AudioSource                 makes sound, from where its entity is
//     AudioListener               the ears - one per scene, on the mech for now
//
// Unity's AudioSource, field for field where it fits. The settings are saved
// with the scene and shown in the Inspector; m_isPlaying is written back by
// ECSAudioSystem each frame; Play, Stop and PlayOneShot only leave a request
// that ECSAudioSystem carries out the same frame, so the component stays
// plain data the way every ECS component must.
// ---------------------------------------------------------------------------

namespace Crucible {

    // What Play() and Stop() asked for since the last frame. Only the last
    // call counts, so Stop() then Play() in one frame restarts, as in Unity.
    enum class AudioRequest : std::uint8_t {

        None,
        Play,
        Stop

    };  // AUDIOREQUEST ENUM

    struct AudioSource {

        // --- settings: saved with the scene, edited in the Inspector ---

        std::uint64_t     m_clip         = 0;                                     // the audio file's id, from its .meta. 0 = no clip
        AudioClipLoadType m_loadType     = AudioClipLoadType::DecompressOnLoad;   // Streaming for music
        AudioBus          m_bus          = AudioBus::SFX;                         // Unity's "Output"
        bool              m_playOnAwake  = true;                                  // Unity's default: plays when the scene starts
        bool              m_loop         = false;
        float             m_volume       = 1.0f;                                  // 0..1
        float             m_pitch        = 1.0f;                                  // 1 normal, 2 an octave up and twice as fast
        float             m_spatialBlend = 0.0f;                                  // 0 = 2D, 1 = 3D from this entity's position
        float             m_minDistance  = 1.0f;                                  // full volume closer than this (3D only)
        float             m_maxDistance  = 500.0f;                                // Logarithmic: stops getting quieter here. Linear: silent here
        AudioRolloffMode  m_rolloffMode  = AudioRolloffMode::Logarithmic;         // Unity's "Volume Rolloff". Linear to fade to silence

        // --- result: written by ECSAudioSystem, read it, never write it ---

        // The Play() voice, as in Unity: one-shots do not count.
        bool              m_isPlaying    = false;

        // --- Unity's methods ---

        // Starts the clip from the beginning, cutting off the last Play().
        void Play()        { m_request = AudioRequest::Play; }

        // Stops the Play() voice and every one-shot still sounding.
        void Stop()        { m_request = AudioRequest::Stop; }

        // Plays the clip once more on top of whatever is playing - the
        // gunshot. Never loops. Each call is one shot, even several a frame.
        void PlayOneShot() { if (m_pendingOneShots < 255) ++m_pendingOneShots; }

        // Unity's PlayOneShot(clip, volumeScale): plays another clip once on
        // top, WITHOUT changing m_clip - the gun's reload click from the same
        // source as its shots. clip is an audio asset id (0 is ignored);
        // volumeScale multiplies m_volume. Up to kMaxClipShots a frame; more
        // are dropped. Note: changing m_volume while one plays resets it to
        // m_volume (the live change goes to every voice, scale not kept).
        static constexpr std::uint8_t kMaxClipShots = 8;
        void PlayOneShot(const std::uint64_t& clip, const float& volumeScale = 1.0f) {
            if (clip == 0 || m_pendingClipShotCount >= kMaxClipShots) return;
            m_pendingClipShots[m_pendingClipShotCount]       = clip;
            m_pendingClipShotVolumes[m_pendingClipShotCount] = volumeScale > 0.0f ? volumeScale : 0.0f;
            ++m_pendingClipShotCount;
        }

        // --- pending requests: ECSAudioSystem reads and clears these ---
        // Not registered for reflection, so never saved and never shown.

        AudioRequest      m_request         = AudioRequest::None;
        std::uint8_t      m_pendingOneShots = 0;

        // PlayOneShot(clip, volumeScale) requests, in order.
        std::array<std::uint64_t, kMaxClipShots> m_pendingClipShots{};
        std::array<float, kMaxClipShots>         m_pendingClipShotVolumes{};
        std::uint8_t                             m_pendingClipShotCount = 0;

    };  // AUDIOSOURCE STRUCT

    /*
        Unity's AudioListener: the ears. Sounds are placed relative to this
        entity's world position and facing (local -Z is forward, as for lights).
        It has nothing to set - being on an entity is the whole job. One per
        scene: with none, the ears follow the Renderer's camera (the camera is
        not an entity yet, so it cannot carry this), and with several only the
        first is used and ECSAudioSystem warns once.
    */
    struct AudioListener {};

    CRUCIBLE_COMPONENT(AudioSource, AudioSource)
    CRUCIBLE_COMPONENT(AudioListener, AudioListener)

}   // NAMESPACE CRUCIBLE
