/*************************************************************************
* \file:         UserSettings.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          27 October 2026
* \version       1.1
* \brief
*                UserSettings: what the PLAYER can change from an options
*                menu (volumes, look sensitivity, invert Y, keybinds), and
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
*                        "input": { "mouseSensitivity": 1.0, "stickSensitivity": 1.0, "invertY": false,
*                                   "keys": { "forward": "W", "back": "S", "left": "A", "right": "D",
*                                             "jump": "Space", "jumpButton": "GamepadA" } },
*                        "quality": { "level": "High", "antiAliasing": "MSAA4x", "shadowQuality": "High",
*                                     "ambientOcclusion": true, "bloom": true, "volumetricFog": true,
*                                     "vsync": true, "frameRateLimit": -1,
*                                     "brightness": 1.0, "fovOffset": 0.0, "vignette": true,
*                                     "filmGrain": true, "chromaticAberration": true },
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
* \history
*    -v1.0       volumes, look sensitivity, invert Y
*    -v1.1       the mech's keybinds (InputSettings::forwardKey ..
*                jumpButton), saved by key name; ECSPlayerControllerSystem
*                reads them instead of hardcoded WASD / Space / A
*                (Tan Chun Kwang Xavier)
*    -v1.2       graphics quality (GraphicsSettings: preset, anti-aliasing,
*                shadows, AO, bloom, volumetric fog), for C# QualitySettings
*                (Tan Chun Kwang Xavier)
*    -v1.3       VSync and the frame-rate limit (GraphicsSettings::vsync,
*                frameRateLimit); outside the presets (Tan Chun Kwang Xavier)
*    -v1.4       brightness, FOV offset, vignette / film grain / chromatic
*                aberration switches; outside the presets (Tan Chun Kwang Xavier)
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <filesystem>

// Crucible engine
#include "EngineExport.h"
#include "Input/KeyCode.h"

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

        // The mech's keybinds (ECSPlayerControllerSystem). Any KeyCode works, mouse
        // and gamepad buttons included; Unknown = unbound. The gamepad's left stick
        // always walks as well. Saved by name ("W"), so reordering KeyCode.h never
        // changes what a saved file means.
        KeyCode forwardKey = KeyCode::W;
        KeyCode backKey    = KeyCode::S;
        KeyCode leftKey    = KeyCode::A;
        KeyCode rightKey   = KeyCode::D;
        KeyCode jumpKey    = KeyCode::Space;
        KeyCode jumpButton = KeyCode::GamepadA;     // a second jump binding, for the gamepad
    };  // INPUTSETTINGS STRUCT

    // The options menu's Graphics page (C# QualitySettings). Saved by name ("High",
    // "MSAA4x"), so reordering these enums never changes what a saved file means.
    // Core::SetGraphicsSettings turns them into the renderer's PostSettings and
    // ShadowSettings (Core::ApplyGraphicsSettings has the exact numbers).
    enum class QualityLevel : uint8_t {
        Low, Medium, High, Ultra,
        Custom      // the individual settings match no preset
    };

    // MSAA smooths geometry edges; FXAA (a screen pass) also catches the edges
    // shading makes (toon lines, thin highlights), so the MSAA levels keep FXAA on.
    enum class AntiAliasing : uint8_t {
        Off,
        FXAA,
        MSAA2x,     // + FXAA
        MSAA4x      // + FXAA
    };

    // The sun's cascaded shadows: how many cascades and how far they reach.
    enum class ShadowQuality : uint8_t {
        Off, Low, Medium, High, Ultra
    };

    inline constexpr int32_t kFrameRateDefault   = -1;
    inline constexpr int32_t kFrameRateUnlimited = 0;
    inline constexpr int32_t kFrameRateMin       = 15;
    inline constexpr int32_t kFrameRateMax       = 1000;

    // A frame-rate limit made valid: default and unlimited kept, the rest clamped.
    constexpr int32_t ClampFrameRateLimit(int32_t fps) {
        return fps < 0 ? kFrameRateDefault : fps == 0 ? kFrameRateUnlimited
             : fps < kFrameRateMin ? kFrameRateMin : fps > kFrameRateMax ? kFrameRateMax : fps;
    }

    inline constexpr float kBrightnessMin = 0.5f;
    inline constexpr float kBrightnessMax = 1.5f;
    inline constexpr float kFovOffsetMin  = -10.0f;
    inline constexpr float kFovOffsetMax  = 30.0f;

    // Brightness / FOV offset made valid (NaN -> the default).
    constexpr float ClampBrightness(float v) {
        return !(v == v) ? 1.0f : v < kBrightnessMin ? kBrightnessMin : v > kBrightnessMax ? kBrightnessMax : v;
    }
    constexpr float ClampFovOffset(float v) {
        return !(v == v) ? 0.0f : v < kFovOffsetMin ? kFovOffsetMin : v > kFovOffsetMax ? kFovOffsetMax : v;
    }

    struct GraphicsSettings {
        // Defaults = the High preset, which is what the renderer drew before these
        // settings existed (RenderTypes.h's PostSettings / ShadowSettings defaults).
        QualityLevel  quality          = QualityLevel::High;
        AntiAliasing  antiAliasing     = AntiAliasing::MSAA4x;
        ShadowQuality shadowQuality    = ShadowQuality::High;
        bool          ambientOcclusion = true;
        bool          bloom            = true;
        bool          volumetricFog    = true;

        // Display preferences, NOT part of the presets: changing them never makes
        // the level Custom, and picking a level never changes them.
        // VSync on = no tearing, the frame rate follows the monitor (FIFO). Off =
        // MAILBOX where the GPU has it (no tearing either), else IMMEDIATE (may tear).
        bool          vsync            = true;
        // Frames per second at most: kFrameRateDefault = the team's EngineConfig
        // targetFPS, kFrameRateUnlimited = no limit, otherwise kFrameRateMin..Max.
        int32_t       frameRateLimit   = kFrameRateDefault;

        // The player's view, also outside the presets. Applied on top of the scene's
        // Skybox and cameras, so every level keeps its own look relative to the others.
        float         brightness          = 1.0f;   // x the scene's exposure, kBrightnessMin..Max
        float         fovOffset           = 0.0f;   // degrees added to every camera, kFovOffsetMin..Max
        bool          vignette            = true;   // false = off even where a scene uses them
        bool          filmGrain           = true;
        bool          chromaticAberration = true;

        bool operator==(const GraphicsSettings&) const = default;
    };  // GRAPHICSSETTINGS STRUCT

    // The settings a preset stands for. Custom gives the defaults (High). Display and
    // view settings (VSync .. chromaticAberration) are the defaults; ApplyPreset keeps the player's own.
    ENGINE_API GraphicsSettings GraphicsPreset(QualityLevel level);

    // `settings` with every quality setting changed to `level`'s; display and
    // view settings (VSync .. chromaticAberration) stay as they are.
    ENGINE_API GraphicsSettings ApplyPreset(const GraphicsSettings& settings, QualityLevel level);

    // The preset `settings` matches (only the preset fields compared), or Custom.
    ENGINE_API QualityLevel MatchQualityLevel(const GraphicsSettings& settings);

    struct UserSettings {
        AudioSettings    audio;
        InputSettings    input;
        GraphicsSettings graphics;
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
