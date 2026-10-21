/*!
* @file			FatalError.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			22 October 2026
*
* @brief		Telling the person at the screen that the program cannot run.
*
* Release builds are linked with /SUBSYSTEM:WINDOWS and have no console, so a log line alone
* is invisible to a player. ShowFatalError logs the problem AND shows it in a message box.
*
* This is for "the program cannot start/continue, here is why" (no Assets folder, no Vulkan
* GPU, a startup exception) followed by a normal return from main. It does not exit by itself.
* CRU_FATAL is different: it aborts on the spot, for engine bugs.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <string_view>

#include "EngineExport.h"

namespace Crucible {

	// Logs `message` at Error level (which flushes the log), then shows it in a message box
	// titled `title` with the log file's location added. Without Windows it prints to stderr.
	// Both strings are UTF-8. Blocks until the box is closed.
	ENGINE_API void ShowFatalError(std::string_view title, std::string_view message);

}
