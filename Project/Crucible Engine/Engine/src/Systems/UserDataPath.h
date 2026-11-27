/*!
* @file			UserDataPath.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			26 November 2026
*
* @brief		Where the running program writes its own files: settings, caches and the log.
*
* The installed game lives in Program Files, which normal users cannot write to, so the game
* keeps these files in its own folder under Local AppData instead:
*
*   C:\Users\<name>\AppData\Local\<game name>\
*       UserSettings.json, crucible.log, Cache\PipelineCache.bin
*
* The game picks that folder with UseLocalAppDataFolder() as the first thing in main(). Any
* program that never calls it (the Editor, Cook, tests) keeps writing next to its .exe, as
* before. Read-only data (Resources, Mono, Managed) is still found next to the .exe.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <filesystem>
#include <string_view>

#include "EngineExport.h"

namespace Crucible {

	// The folder this program writes its own files to. ExecutableDirectory() until
	// UseLocalAppDataFolder() succeeds.
	ENGINE_API std::filesystem::path UserDataDirectory();

	// Makes UserDataDirectory() %LOCALAPPDATA%\<folderName> (the game passes GameInfo::Name), creating it if
	// it does not exist. Call it before anything is logged or loaded: the log file, settings and
	// caches each read UserDataDirectory() once when they open. Returns false, and leaves the
	// folder next to the .exe, if Local AppData cannot be found or the folder cannot be made.
	ENGINE_API bool UseLocalAppDataFolder(std::string_view folderName);

}
