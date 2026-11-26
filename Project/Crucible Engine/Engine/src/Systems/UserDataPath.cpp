/*!
* @file			UserDataPath.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			26 November 2026
*
* @brief		Picks the folder the program writes its own files to (see UserDataPath.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "Systems/UserDataPath.h"

#include <cstdlib>
#include <mutex>
#include <string>
#include <system_error>

#include "Systems/ExecutablePath.h"

#ifdef _WIN32
// Only for SHGetKnownFolderPath. Keep windows.h from dragging in half the OS and from defining
// min and max as macros.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>   // SHGetKnownFolderPath, FOLDERID_LocalAppData (shell32 + ole32)
#endif

namespace Crucible {

	namespace {
		std::mutex            g_mutex;
		std::filesystem::path g_directory;   // empty = next to the .exe

		// C:\Users\<name>\AppData\Local, or empty if Windows cannot say.
		std::filesystem::path LocalAppData()
		{
#ifdef _WIN32
			PWSTR folder = nullptr;
			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder)) && folder)
			{
				std::filesystem::path result(folder);
				CoTaskMemFree(folder);
				return result;
			}
			CoTaskMemFree(folder);   // safe on nullptr

			// Fall back to the environment variable (set on every normal Windows login).
			wchar_t* value = nullptr;
			std::size_t length = 0;
			if (_wdupenv_s(&value, &length, L"LOCALAPPDATA") == 0 && value)
			{
				std::filesystem::path result(value);
				std::free(value);
				return result;
			}
			return {};
#else
			// Linux: the XDG data folder, the nearest match.
			if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data)
				return data;
			if (const char* home = std::getenv("HOME"); home && *home)
				return std::filesystem::path(home) / ".local" / "share";
			return {};
#endif
		}
	}

	std::filesystem::path UserDataDirectory()
	{
		std::scoped_lock lock(g_mutex);
		return g_directory.empty() ? ExecutableDirectory() : g_directory;
	}

	bool UseLocalAppDataFolder(std::string_view folderName)
	{
		if (folderName.empty())
			return false;

		const std::filesystem::path base = LocalAppData();
		if (base.empty())
			return false;

		const std::filesystem::path directory = base / std::filesystem::path(std::string(folderName));
		std::error_code ec;
		std::filesystem::create_directories(directory, ec);
		if (ec || !std::filesystem::is_directory(directory, ec))
			return false;

		std::scoped_lock lock(g_mutex);
		g_directory = directory;
		return true;
	}

}
