/*!
* @file			ExecutablePath.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			22 October 2026
*
* @brief		Where the running .exe lives.
*
* Use this, not the working directory, for anything that belongs next to the program: a
* debugger, a shortcut or a double-click can each start it in a different working directory,
* but the .exe's own folder never changes. The asset search (AssetPaths.h) and the log file
* (Logger.h) both start from here.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <filesystem>

#include "EngineExport.h"

namespace Crucible {

	// The folder holding the running .exe (Editor.exe / the game's .exe - not Engine.dll), as an
	// absolute path. Looked up once and cached. Falls back to the working directory if the OS
	// cannot say, and is empty only if even that fails.
	ENGINE_API const std::filesystem::path& ExecutableDirectory();

}
