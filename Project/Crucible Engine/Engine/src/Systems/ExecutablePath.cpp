/*!
* @file			ExecutablePath.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			22 October 2026
*
* @brief		Looks up the running .exe's folder once (see ExecutablePath.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "Systems/ExecutablePath.h"

#include <string>
#include <system_error>

#ifdef _WIN32
// Only for GetModuleFileNameW. Keep windows.h from dragging in half the OS and from defining
// min and max as macros.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Crucible {

	namespace {
		std::filesystem::path LookUpExecutableDirectory()
		{
#ifdef _WIN32
			// GetModuleFileNameW(nullptr, ...) gives the process's .exe, not Engine.dll. It cuts the
			// path off without failing when the buffer is too small (the return value then equals
			// the buffer size), so grow the buffer until the whole path fits - OneDrive folders
			// can easily pass the old 260-character MAX_PATH.
			std::wstring buffer(MAX_PATH, L'\0');
			for (;;)
			{
				const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (length == 0)
					break;                                                // the call failed
				if (length < buffer.size())
				{
					buffer.resize(length);
					return std::filesystem::path(buffer).parent_path();   // drop the .exe's own name
				}
				if (buffer.size() >= 32768)
					break;                                                // longer than any path Windows allows
				buffer.resize(buffer.size() * 2);
			}
#else
			// Linux: the running executable is always linked here.
			std::error_code linkEc;
			const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", linkEc);
			if (!linkEc)
				return exe.parent_path();
#endif
			// The OS could not tell us: fall back to the working directory.
			std::error_code ec;
			const std::filesystem::path cwd = std::filesystem::current_path(ec);
			return ec ? std::filesystem::path{} : cwd;
		}
	}

	const std::filesystem::path& ExecutableDirectory()
	{
		// Built once, on first call (thread-safe). Defined here, not inline in the header, so every
		// module shares this one copy.
		static const std::filesystem::path directory = LookUpExecutableDirectory();
		return directory;
	}

}
