/*************************************************************************
* \file:         UserSettings.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          27 October 2026
* \version       1.0
* \brief
*                UserSettings: what the PLAYER can change from an options
*                menu (volumes, look sensitivity, invert Y), and
*                UserSettingsLoader, which reads and writes them.
*
* \details
*                EngineConfig.json is the TEAM's file: it is in Assets, in
*                git, and holds project defaults. UserSettings.json is the
*                PLAYER's: it sits next to the .exe, never reaches git, and
*                one player's choices never change anyone else's game.
*                Unity splits it the same way (Project Settings versus
*                PlayerPrefs).
*
*                It is the same file Core already uses to remember the
*                window mode (EngineConfigLoader::SaveWindowState). That
*                part lives in per-app sections ("editor", "game"); these
*                settings are top-level sections every app shares:
*
*                    {
*                        "audio": { "masterVolume": 1.0, "musicVolume": 1.0, "sfxVolume": 1.0 },
*                        "input": { "mouseSensitivity": 1.0, "stickSensitivity": 1.0, "invertY": false },
*                        "game":  { "graphics": { "fullScreen": true, "maximized": false } }
*                    }
*
*                Each writer only touches its own keys, so neither wipes
*                the other's. Delete the file to go back to the defaults
*                below.
*
*                Adding a setting: a field here, then one Get line in Load
*                and one Set line in Save (UserSettings.cpp).
*
**************************************************************************/
#pragma once

// STL
#include <filesystem>

// Crucible engine
#include "EngineExport.h"

namespace Crucible {

    // The options menu's Audio page: one slider per AudioBus (Audio/AudioBuses.h).
    // 0 = silent, 1 = full. ECSAudioSystem applies them; a change shows up the
    // next frame, so a slider can write here directly while it is dragged.
    struct AudioSettings {
        float masterVolume = 1.0f;
        float musicVolume  = 1.0f;
        float sfxVolume    = 1.0f;
    };  // AUDIOSETTINGS STRUCT

    // The options menu's Controls page. These MULTIPLY the designer's feel set
    // on the CameraOrbit component (m_sensitivity, m_stickSpeed), so 1.0 is
    // "as designed" and 2.0 turns twice as fast. Kept apart from the component
    // on purpose: a designer retuning the camera then still changes every
    // player's camera, instead of being overwritten by a saved number.
    struct InputSettings {
        float mouseSensitivity = 1.0f;
        float stickSensitivity = 1.0f;
        bool  invertY          = false;     // up on the mouse / right stick looks DOWN
    };  // INPUTSETTINGS STRUCT

    struct UserSettings {
        AudioSettings audio;
        InputSettings input;
    };  // USERSETTINGS STRUCT

    class ENGINE_API UserSettingsLoader {

    public:

        /*! \brief
            Reads UserSettings.json. Never fails: a missing file, a missing
            key or a value of the wrong type gives that setting's default,
            and out-of-range numbers are clamped (a hand-edited file must
            not make the game silent forever or the camera spin).
        */
        static UserSettings Load();

        /*! \brief
            Writes every setting into UserSettings.json, keeping the keys
            it does not own (the window mode). Returns false, with a log
            line, if the file cannot be written.
        */
        static bool Save(const UserSettings& settings);

        // Where the file is: next to the running .exe, the same path as
        // EngineConfigLoader::UserSettingsFile(). Empty if that is unknown.
        static std::filesystem::path File();

    };  // USERSETTINGSLOADER CLASS

}   // NAMESPACE CRUCIBLE
