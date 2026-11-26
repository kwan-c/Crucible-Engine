#pragma once

#include <filesystem>

#include "EngineConfig.h"
#include "EngineExport.h"

namespace Crucible
{
	// What SaveWindowState writes into an app's section of UserSettings.json.
	struct SavedWindowState
	{
		bool          fullScreen            = false;
		bool          maximized             = false;
		std::uint32_t windowWidth           = 0;   // the windowed size; 0 = leave the file's as it is
		std::uint32_t windowHeight          = 0;
		std::uint32_t fullscreenWidth       = 0;   // 0 = borderless (the monitor's own resolution)
		std::uint32_t fullscreenHeight      = 0;
		std::uint32_t fullscreenRefreshRate = 0;
	};

	class ENGINE_API EngineConfigLoader
	{
	public:

		// Reads one config file. Throws std::runtime_error if the file is missing or malformed.
		static EngineConfig Load(const std::filesystem::path& path);

		// The config both executables start from: finds Assets/Config/EngineConfig.json (same
		// search as the AssetManager), reads the shared sections, then layers the "editor" or
		// "game" section on top for `app`. Never throws: on any problem it logs a warning and
		// returns the defaults from EngineConfig.h.
		static EngineConfig LoadForApp(AppKind app);

		// Where LoadForApp looks for the file, or an empty path if no Assets folder was found.
		static std::filesystem::path FindConfigFile();

		// Writes the SHARED settings in `config` into the file at `path`. Only the keys this loader
		// knows are written, into the existing file, so the "editor" / "game" override sections
		// and any keys it doesn't know survive. Pass values from Load(), not from a Core's
		// GetConfig() - that one already has an app's overrides merged in.
		// Returns false (and logs) if the file exists but can't be parsed, or can't be written.
		static bool Save(const EngineConfig& config, const std::filesystem::path& path);

		// The overrides in `app`'s section of the file ("editor" / "game"), for editing.
		// Throws std::runtime_error like Load() if the file is malformed.
		static AppOverrides LoadOverrides(AppKind app, const std::filesystem::path& path);

		// Writes `overrides` into `app`'s section: set values are written, unset ones removed so
		// the shared value applies again. Keys in that section the struct doesn't cover are kept.
		static bool SaveOverrides(AppKind app, const AppOverrides& overrides, const std::filesystem::path& path);

		// ---- Per-user window state ------------------------------------------------------------ //
		//
		// UserSettings.json, in the program's data folder (Systems/UserDataPath.h): beside the
		// Editor's .exe, %LOCALAPPDATA%\<game name> for the game. Not in Assets, so one person's
		// fullscreen doesn't end up in everyone's EngineConfig.json through git. Same layout as an app
		// section of EngineConfig.json:
		//     { "editor": { "graphics": { "fullScreen": false, "maximized": true } }, "game": { ... } }
		// LoadForApp layers `app`'s section on top of everything else. Delete the file to go back
		// to the project defaults.
		static std::filesystem::path UserSettingsFile();

		// Writes `app`'s window mode, windowed size and fullscreen resolution into
		// UserSettings.json (other apps' sections and unknown keys are kept). Core calls this when
		// any of them changes and at shutdown.
		static bool SaveWindowState(AppKind app, const SavedWindowState& state);
	};
}