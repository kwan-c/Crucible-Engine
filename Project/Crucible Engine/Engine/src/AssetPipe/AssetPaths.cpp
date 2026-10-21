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
				CRU_INFO("[AssetPaths] Assets folder: {}", found.generic_string());
			return found;
		}();
		return root;
	}

	std::filesystem::path AssetPath(std::string_view relative)
	{
		const std::filesystem::path& root = AssetRoot();
		return root.empty() ? std::filesystem::path(relative) : root / relative;
	}

}
