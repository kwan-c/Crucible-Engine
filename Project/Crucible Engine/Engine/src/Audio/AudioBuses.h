/*************************************************************************
* \file:         AudioBuses.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          29 September 2026
* \version       1.0
* \brief
*                AudioBus: the volume groups (Master, Music, SFX), Unity's
*                AudioMixerGroup.
*
* \details
*                Every sound plays through one bus, and turning a bus down
*                turns down everything in it, which is what the options
*                menu's sliders use. AudioSystem.cpp gives each bus an
*                FMOD ChannelGroup at startup, so adding a bus is one line
*                here.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstddef>
#include <cstdint>

// Crucible engine
#include "EngineExport.h"

// ---------------------------------------------------------------------------
// Audio buses.
//
// A bus is one volume knob that a group of sounds plays through. Unity calls
// it an AudioMixerGroup; FMOD calls it a ChannelGroup. Every sound plays into
// exactly one bus, and turning a bus down turns down everything playing
// through it - which is how an options menu gets a Music slider and an SFX
// slider without touching a single sound.
//
//       Music --+
//               +--> Master --> speakers
//       SFX ----+
//
// The list lives in its own file for the same reason PhysicsLayers does: it is
// the designer's vocabulary, not the audio programmer's. AudioSystem.cpp gives
// every entry an FMOD ChannelGroup at startup, so adding a bus is one line here
// and one line in ToString.
//
// Three for M1. UI comes back the day there is a UI sound.
// ---------------------------------------------------------------------------

namespace Crucible {

    enum class AudioBus : std::uint8_t {

        Master = 0,     // FMOD's own master group, which always exists. Everything ends up here
        Music,          // the battle theme: streamed from disk, looping, one track at a time
        SFX,            // gunshots, impacts, footsteps: short, held in memory, many at once

        Count           // keep last: how many buses there are, used to size the bus array in AudioInternal.h

    };  // AUDIOBUS ENUM

    // How many real buses exist, as a plain number for sizing arrays.
    inline constexpr std::size_t kAudioBusCount = static_cast<std::size_t>(AudioBus::Count);

    // For logs, and later for the editor's dropdown.
    ENGINE_API const char* ToString(const AudioBus& bus);

}   // NAMESPACE CRUCIBLE
