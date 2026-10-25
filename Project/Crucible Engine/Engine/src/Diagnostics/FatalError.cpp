/*!
* @file			FatalError.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			22 October 2026
*
* @brief		Logs a fatal startup problem and shows it in a message box (see FatalError.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "Diagnostics/FatalError.h"
#include "Diagnostics/Logger.h"
#include "AssetPipe/AssetPaths.h"   // PathToUtf8

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <iostream>
#endif

namespace Crucible {

	namespace {
#ifdef _WIN32
		// UTF-8 -> UTF-16 for the Windows "W" API.
		std::wstring Widen(std::string_view text)
		{
			if (text.empty()) return {};
			const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
			return wide;
		}
#endif
	}

	void ShowFatalError(std::string_view title, std::string_view message)
	{
		CRU_ERROR("{}: {}", title, message);   // Error level flushes, so the log has it even if we die next

		std::string text(message);
		const std::filesystem::path logFile = Debug::LogFilePath();
		if (!logFile.empty())
			text += "\n\nMore detail is in the log file:\n" + PathToUtf8(logFile);

#ifdef _WIN32
		// No owner window: one may not exist yet, or be the thing that failed. MB_SETFOREGROUND so
		// the box is not hidden behind a half-created game window.
		MessageBoxW(nullptr, Widen(text).c_str(), Widen(title).c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#else
		std::cerr << title << ": " << text << '\n';
#endif
	}

}
