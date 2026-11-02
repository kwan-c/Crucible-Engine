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

	namespace {
		// An installed game has no Assets folder, only Resources (filled by Cook): look for it
		// the same way FindAssetRoot looks for Assets - upward from the .exe, then the working directory.
		std::filesystem::path FindInstalledResources()
		{
			constexpr int MAX_SEARCH_DEPTH = 4;
			std::error_code ec;
			const std::filesystem::path starts[] = { ExecutableDirectory(), std::filesystem::current_path(ec) };
			for (const std::filesystem::path& start : starts)
			{
				std::filesystem::path dir = start;
				for (int depth = 0; !dir.empty() && depth < MAX_SEARCH_DEPTH; ++depth)
				{
					const std::filesystem::path candidate = dir / "Resources";
					if (std::filesystem::is_directory(candidate, ec))
						return std::filesystem::weakly_canonical(candidate, ec);
					if (dir.parent_path() == dir)
						break;
					dir = dir.parent_path();
				}
			}
			return {};
		}
	}

	const std::filesystem::path& AssetRoot()
	{
		// A function-local static is built exactly once, on first call, even if two threads
		// ask at the same time - so the search (and its log line) happens once per run.
		static const std::filesystem::path root = []
		{
			std::filesystem::path found = AssetManager::FindAssetRoot({});
			if (!found.empty())
			{
				CRU_INFO("[AssetPaths] Assets folder: {}", PathToUtf8(found));
				return found;
			}

			// No Assets: an installed game. Everything it reads is in Resources.
			found = FindInstalledResources();
			if (found.empty())
				CRU_WARN("[AssetPaths] No 'Assets' or 'Resources' folder found (checked CRUCIBLE_ASSET_ROOT, then upward from the executable and the working directory)");
			else
				CRU_INFO("[AssetPaths] No Assets folder - running from the cooked Resources folder: {}", PathToUtf8(found));
			return found;
		}();
		return root;
	}

	bool IsCookedRun()
	{
		return !AssetRoot().empty() && AssetRoot().filename() == "Resources";
	}

	bool RequireAssetRoot()
	{
		if (!AssetRoot().empty())
			return true;

		std::error_code ec;
		const std::filesystem::path cwd = std::filesystem::current_path(ec);

		std::string message =
			"The 'Assets' folder (or, for an installed game, 'Resources') could not be found, so the program cannot start.\n\n"
			"Looked in:\n"
			"  - the CRUCIBLE_ASSET_ROOT environment variable (not set, or not a folder)\n"
			"  - " + PathToUtf8(ExecutableDirectory()) + " and the folders above it\n";
		if (!ec && cwd != ExecutableDirectory())
			message += "  - " + PathToUtf8(cwd) + " and the folders above it\n";
		message +=
			"\nPut the Resources folder next to the program, or set CRUCIBLE_ASSET_ROOT "
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

	const std::filesystem::path& ResourceRoot()
	{
		static const std::filesystem::path root = []
		{
			if (IsCookedRun())
				return AssetRoot();   // installed game: Resources is the only folder

			std::filesystem::path found;
			if (!AssetRoot().empty())
			{
				std::error_code ec;
				const std::filesystem::path candidate = AssetRoot().parent_path() / "Resources";
				if (std::filesystem::is_directory(candidate, ec))
					found = candidate;
			}
			if (found.empty())
				CRU_WARN("[AssetPaths] No 'Resources' folder next to the Assets folder - compiled files (models, fonts, shaders) will be missing");
			else
				CRU_INFO("[AssetPaths] Resources folder: {}", PathToUtf8(found));
			return found;
		}();
		return root;
	}

	std::filesystem::path ResourcePath(std::string_view relative)
	{
		const std::filesystem::path& root = ResourceRoot();
		return root.empty() ? std::filesystem::path(relative) : root / relative;
	}

}
