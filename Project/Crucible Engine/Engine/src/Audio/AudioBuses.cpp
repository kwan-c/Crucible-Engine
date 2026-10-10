/*************************************************************************
* \file:         AudioBuses.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          29 September 2026
* \version       1.0
* \brief
*                ToString for AudioBus, for log messages.
*
* \details
*                A value outside the enum returns "unknown" rather than
*                crashing.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "AudioBuses.h"

namespace Crucible {

    const char* ToString(const AudioBus& bus) {

        switch (bus) {
            case AudioBus::Master:  return "Master";
            case AudioBus::Music:   return "Music";
            case AudioBus::SFX:     return "SFX";
            case AudioBus::Count:   break;
        }

        return "unknown";       // a bus that is not in the enum, so the log says so instead of crashing

    }   // ToString()

}   // NAMESPACE CRUCIBLE
