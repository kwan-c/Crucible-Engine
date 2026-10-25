/*!
* @file			AssetPaths.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			22 October 2026
*
* @brief		The one place that knows where the Assets folder is.
*
* The folder is searched for once, on first use, with AssetManager::FindAssetRoot's rules
* (CRUCIBLE_ASSET_ROOT, then upward from the .exe, then upward from the working directory),
* and the answer is cached for the rest of the run.
*
* Use this for files needed before Core::Init has created the AssetManager - the Renderer's
* shaders (built in Core's constructor) and EngineConfig.json (read before Core exists).
* Once the AssetManager is up, AssetManager::Resolve() gives the same answer.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "EngineExport.h"

namespace Crucible {

	// The Assets folder (absolute), or an empty path if none was found.
	// Defined in AssetPaths.cpp - not inline - so Engine.dll, Editor.exe and Game.exe all share
	// the one cached value instead of each module keeping its own copy.
	ENGINE_API const std::filesystem::path& AssetRoot();

	// AssetRoot() / relative, e.g. AssetPath("Shaders/DefaultShader.spv").
	// With no Assets folder found this is just `relative`, so a failed open names the file.
	ENGINE_API std::filesystem::path AssetPath(std::string_view relative);

	// For mains, before anything else: true if the Assets folder was found. If not, shows the
	// player what was searched and how to fix it (ShowFatalError), and the main should just
	// return - nothing after it can work without Assets (shaders, config, scenes).
	ENGINE_API bool RequireAssetRoot();

	// A path as UTF-8 text, for messages and logs. Use instead of path.string(): on Windows that
	// throws for folder names the system code page can't represent (easy to hit under OneDrive).
	ENGINE_API std::string PathToUtf8(const std::filesystem::path& path);

}
