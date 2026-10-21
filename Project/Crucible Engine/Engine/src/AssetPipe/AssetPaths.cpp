/*!
* @file			AssetPaths.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			22 October 2026
*
* @brief		Finds the Assets folder once and caches it (see AssetPaths.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "AssetPipe/AssetPaths.h"
#include "AssetPipe/Assetmanager.h"
#include "Diagnostics/Logger.h"
#include "Diagnostics/FatalError.h"
#include "Systems/ExecutablePath.h"

#include <system_error>

namespace Crucible {

	const std::filesystem::path& AssetRoot()
	{
		// A function-local static is built exactly once, on first call, even if two threads
		// ask at the same time - so the search (and its log line) happens once per run.
		static const std::filesystem::path root = []
		{
			std::filesystem::path found = AssetManager::FindAssetRoot({});
			if (found.empty())
				CRU_WARN("[AssetPaths] No 'Assets' folder found (checked CRUCIBLE_ASSET_ROOT, then upward from the executable and the working directory)");
			else
				CRU_INFO("[AssetPaths] Assets folder: {}", PathToUtf8(found));
			return found;
		}();
		return root;
	}

	bool RequireAssetRoot()
	{
		if (!AssetRoot().empty())
			return true;

		std::error_code ec;
		const std::filesystem::path cwd = std::filesystem::current_path(ec);

		std::string message =
			"The 'Assets' folder could not be found, so the program cannot start.\n\n"
			"Looked in:\n"
			"  - the CRUCIBLE_ASSET_ROOT environment variable (not set, or not a folder)\n"
			"  - " + PathToUtf8(ExecutableDirectory()) + " and the folders above it\n";
		if (!ec && cwd != ExecutableDirectory())
			message += "  - " + PathToUtf8(cwd) + " and the folders above it\n";
		message +=
			"\nPut the Assets folder next to the program, or set CRUCIBLE_ASSET_ROOT "
			"to its full path.";

		ShowFatalError("Assets folder not found", message);
		return false;
	}

	std::string PathToUtf8(const std::filesystem::path& path)
	{
		const std::u8string text = path.generic_u8string();
		return std::string(reinterpret_cast<const char*>(text.data()), text.size());
	}

	std::filesystem::path AssetPath(std::string_view relative)
	{
		const std::filesystem::path& root = AssetRoot();
		return root.empty() ? std::filesystem::path(relative) : root / relative;
	}

}
