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
