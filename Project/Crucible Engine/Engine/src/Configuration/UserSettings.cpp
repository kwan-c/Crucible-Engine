/*************************************************************************
* \file:         UserSettings.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          27 October 2026
* \version       1.1
* \brief
*                Reads and writes the player's settings in
*                UserSettings.json (see UserSettings.h).
*
* \details
*                Goes through JsonWrapper, whose Get returns the fallback
*                for a missing key or a wrong type. That is the behaviour a
*                player's file needs: a half-broken file loads with
*                defaults for the broken parts instead of refusing to load.
*
* \history
*    -v1.0       volumes, look sensitivity, invert Y
*    -v1.1       keybinds under "input/keys", by key name
*                (Tan Chun Kwang Xavier)
*    -v1.2       graphics quality under "quality", enums by name; the
*                presets (GraphicsPreset, MatchQualityLevel)
*                (Tan Chun Kwang Xavier)
*
**************************************************************************/
#include "UserSettings.h"

// STL
#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <system_error>

// Crucible engine
#include "AssetPipe/AssetPaths.h"
#include "Configuration/EngineConfigLoader.h"
#include "Diagnostics/Logger.h"
#include "Input/KeyChord.h"     // KeyCodeFromString
#include "Json/JsonWrapper.h"

namespace Crucible {

    namespace {

        // How far a sensitivity multiplier may go. Not 0: a camera that cannot
        // turn looks like a bug, not a setting. 10x is already unplayable, so
        // anything beyond it is a typo in the file.
        constexpr float kMinSensitivity = 0.1f;
        constexpr float kMaxSensitivity = 10.0f;

        // The float as the double a person would type: 0.3f goes in as 0.3, not as
        // 0.30000001192092896 (the float's exact value, which is what a plain widening
        // to double writes). std::format gives a float's shortest exact text.
        double Tidy(float value) {
            return std::stod(std::format("{}", value));
        }

        // A keybind saved by name ("W", "Space", "Unknown" = unbound). A missing key
        // or a name that is no key (a typo in a hand-edited file) keeps the default.
        KeyCode ReadKey(const JsonWrapper& doc, const std::string& path, KeyCode fallback) {
            const std::string name = doc.Get<std::string>(path, std::string{});
            if (name.empty()) return fallback;
            const KeyCode key = KeyCodeFromString(name);
            if (key == KeyCode::Unknown && name != KeyCodeToString(KeyCode::Unknown)) {
                CRU_WARN("[UserSettings] '{}' is '{}', which is not a key - keeping {}", path, name, KeyCodeToString(fallback));
                return fallback;
            }
            return key;
        }

        // Saved names of the graphics enums, in enum order (UserSettings.h).
        constexpr std::array<const char*, 5> kQualityNames   = { "Low", "Medium", "High", "Ultra", "Custom" };
        constexpr std::array<const char*, 4> kAntiAliasNames = { "Off", "FXAA", "MSAA2x", "MSAA4x" };
        constexpr std::array<const char*, 5> kShadowNames    = { "Off", "Low", "Medium", "High", "Ultra" };

        template <typename Enum, std::size_t N>
        std::string EnumName(const std::array<const char*, N>& names, Enum value) {
            const auto index = static_cast<std::size_t>(value);
            return index < N ? names[index] : names[0];
        }

        // An enum saved by name. A missing key keeps the default; a name that is no
        // value (a typo in a hand-edited file) keeps it too, with a warning.
        template <typename Enum, std::size_t N>
        Enum ReadEnum(const JsonWrapper& doc, const std::string& path, const std::array<const char*, N>& names, Enum fallback) {
            const std::string name = doc.Get<std::string>(path, std::string{});
            if (name.empty()) return fallback;
            for (std::size_t i = 0; i < N; ++i)
                if (name == names[i]) return static_cast<Enum>(i);
            CRU_WARN("[UserSettings] '{}' is '{}', which is not one of its values - keeping {}", path, name, EnumName(names, fallback));
            return fallback;
        }

    }   // ANONYMOUS NAMESPACE

    GraphicsSettings GraphicsPreset(QualityLevel level) {
        GraphicsSettings s{};   // High
        switch (level) {
        case QualityLevel::Low:
            s.antiAliasing     = AntiAliasing::FXAA;
            s.shadowQuality    = ShadowQuality::Low;
            s.ambientOcclusion = false;
            s.bloom            = true;
            s.volumetricFog    = false;
            break;
        case QualityLevel::Medium:
            s.antiAliasing     = AntiAliasing::MSAA2x;
            s.shadowQuality    = ShadowQuality::Medium;
            s.ambientOcclusion = true;
            s.bloom            = true;
            s.volumetricFog    = false;
            break;
        case QualityLevel::Ultra:
            s.antiAliasing     = AntiAliasing::MSAA4x;
            s.shadowQuality    = ShadowQuality::Ultra;
            s.ambientOcclusion = true;
            s.bloom            = true;
            s.volumetricFog    = true;
            break;
        case QualityLevel::High:
        case QualityLevel::Custom:
            level = QualityLevel::High;
            break;
        }
        s.quality = level;
        return s;
    }   // GraphicsPreset()

    QualityLevel MatchQualityLevel(const GraphicsSettings& settings) {
        for (QualityLevel level : { QualityLevel::Low, QualityLevel::Medium, QualityLevel::High, QualityLevel::Ultra }) {
            GraphicsSettings candidate = settings;
            candidate.quality = level;
            if (candidate == GraphicsPreset(level)) return level;
        }
        return QualityLevel::Custom;
    }   // MatchQualityLevel()

    std::filesystem::path UserSettingsLoader::File() {
        // One place decides where the file is, so the window state and these
        // settings can never end up in two different files.
        return EngineConfigLoader::UserSettingsFile();
    }   // UserSettingsLoader::File()

    UserSettings UserSettingsLoader::Load() {

        // Starts as the defaults in UserSettings.h; each Get below only
        // replaces a value the file actually has.
        UserSettings settings{};

        const std::filesystem::path path = File();
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec)) {
            // Normal on a first run: Core saves the defaults straight after.
            CRU_INFO("[UserSettings] no file yet - using defaults");
            return settings;
        }

        JsonWrapper doc;
        if (!doc.ReadFromFile(path)) {
            // Save would then rewrite it from scratch, so say so now.
            CRU_WARN("[UserSettings] '{}' could not be read - using defaults", PathToUtf8(path));
            return settings;
        }

        AudioSettings& audio = settings.audio;
        audio.masterVolume = std::clamp(doc.Get("audio/masterVolume", audio.masterVolume), 0.0f, 1.0f);
        audio.musicVolume  = std::clamp(doc.Get("audio/musicVolume",  audio.musicVolume),  0.0f, 1.0f);
        audio.sfxVolume    = std::clamp(doc.Get("audio/sfxVolume",    audio.sfxVolume),    0.0f, 1.0f);

        InputSettings& input = settings.input;
        input.mouseSensitivity = std::clamp(doc.Get("input/mouseSensitivity", input.mouseSensitivity), kMinSensitivity, kMaxSensitivity);
        input.stickSensitivity = std::clamp(doc.Get("input/stickSensitivity", input.stickSensitivity), kMinSensitivity, kMaxSensitivity);
        input.invertY          = doc.Get("input/invertY", input.invertY);
        input.forwardKey       = ReadKey(doc, "input/keys/forward",    input.forwardKey);
        input.backKey          = ReadKey(doc, "input/keys/back",       input.backKey);
        input.leftKey          = ReadKey(doc, "input/keys/left",       input.leftKey);
        input.rightKey         = ReadKey(doc, "input/keys/right",      input.rightKey);
        input.jumpKey          = ReadKey(doc, "input/keys/jump",       input.jumpKey);
        input.jumpButton       = ReadKey(doc, "input/keys/jumpButton", input.jumpButton);

        GraphicsSettings& graphics = settings.graphics;
        graphics.antiAliasing     = ReadEnum(doc, "quality/antiAliasing",  kAntiAliasNames, graphics.antiAliasing);
        graphics.shadowQuality    = ReadEnum(doc, "quality/shadowQuality", kShadowNames,    graphics.shadowQuality);
        graphics.ambientOcclusion = doc.Get("quality/ambientOcclusion", graphics.ambientOcclusion);
        graphics.bloom            = doc.Get("quality/bloom",            graphics.bloom);
        graphics.volumetricFog    = doc.Get("quality/volumetricFog",    graphics.volumetricFog);
        // The saved level is only a label: worked out again from the settings, so a
        // hand-edited file can never say "High" over settings that are not High.
        graphics.quality          = MatchQualityLevel(graphics);

        CRU_INFO("[UserSettings] loaded '{}'", PathToUtf8(path));
        return settings;
    }   // UserSettingsLoader::Load()

    bool UserSettingsLoader::Save(const UserSettings& settings) {

        const std::filesystem::path path = File();
        if (path.empty()) {
            CRU_WARN("[UserSettings] no folder to save UserSettings.json in");
            return false;
        }

        // Start from the file as it is, so the window-mode sections that
        // EngineConfigLoader::SaveWindowState wrote survive. A broken file is
        // machine-written, so starting it over loses nothing a person typed.
        JsonWrapper doc;
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !doc.ReadFromFile(path)) {
            CRU_WARN("[UserSettings] '{}' could not be parsed - rewriting it", PathToUtf8(path));
            doc.CreateEmpty();
        }

        // Same keys and layout as Load reads.
        doc.Set("audio/masterVolume", Tidy(settings.audio.masterVolume));
        doc.Set("audio/musicVolume",  Tidy(settings.audio.musicVolume));
        doc.Set("audio/sfxVolume",    Tidy(settings.audio.sfxVolume));

        doc.Set("input/mouseSensitivity", Tidy(settings.input.mouseSensitivity));
        doc.Set("input/stickSensitivity", Tidy(settings.input.stickSensitivity));
        doc.Set("input/invertY",          settings.input.invertY);
        doc.Set("input/keys/forward",    std::string(KeyCodeToString(settings.input.forwardKey)));
        doc.Set("input/keys/back",       std::string(KeyCodeToString(settings.input.backKey)));
        doc.Set("input/keys/left",       std::string(KeyCodeToString(settings.input.leftKey)));
        doc.Set("input/keys/right",      std::string(KeyCodeToString(settings.input.rightKey)));
        doc.Set("input/keys/jump",       std::string(KeyCodeToString(settings.input.jumpKey)));
        doc.Set("input/keys/jumpButton", std::string(KeyCodeToString(settings.input.jumpButton)));

        doc.Set("quality/level",            EnumName(kQualityNames,   settings.graphics.quality));
        doc.Set("quality/antiAliasing",     EnumName(kAntiAliasNames, settings.graphics.antiAliasing));
        doc.Set("quality/shadowQuality",    EnumName(kShadowNames,    settings.graphics.shadowQuality));
        doc.Set("quality/ambientOcclusion", settings.graphics.ambientOcclusion);
        doc.Set("quality/bloom",            settings.graphics.bloom);
        doc.Set("quality/volumetricFog",    settings.graphics.volumetricFog);

        if (!doc.SaveToFile(path)) {
            CRU_WARN("[UserSettings] could not write '{}'", PathToUtf8(path));
            return false;
        }
        return true;
    }   // UserSettingsLoader::Save()

}   // NAMESPACE CRUCIBLE
