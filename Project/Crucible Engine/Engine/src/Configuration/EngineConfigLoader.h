#pragma once

#include <filesystem>

#include "EngineConfig.h"
#include "EngineExport.h"

namespace Crucible
{
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
	};
}