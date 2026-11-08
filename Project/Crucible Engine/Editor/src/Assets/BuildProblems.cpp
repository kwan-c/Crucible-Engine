/*!
* @file     BuildProblems.cpp
* @brief    See BuildProblems.h.
*/
#include "Assets/BuildProblems.h"
#include "AssetBuildReport.h"   // Tools/Common
#include "Diagnostics/Logger.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <system_error>
#include <vector>

namespace Crucible::BuildProblems
{
	namespace
	{
		struct Entry
		{
			std::string asset;     // as given, for messages
			std::string message;
		};

		// Keyed by the lower-case path: Windows paths are not case sensitive.
		std::map<std::string, Entry> s_assets;
		std::filesystem::path s_buildDir;   // where the reports are; empty until LoadBuildReports
		bool s_scriptsCompiling = false;
		int  s_scriptErrors = 0;

		std::string Key(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::replace(text.begin(), text.end(), '\\', '/');
			return text;
		}

		// Takes a fixed asset out of the build's reports, so the editor does not block Play
		// for it again after a restart (the next build rewrites the reports anyway).
		void RemoveFromReports(std::string const& key)
		{
			if (s_buildDir.empty()) return;
			std::error_code ec;
			for (auto const& file : std::filesystem::directory_iterator(AssetBuildReport::Folder(s_buildDir), ec)) {
				if (!file.is_regular_file(ec) || file.path().extension() != ".txt") continue;
				std::vector<AssetBuildReport::Problem> problems = AssetBuildReport::Read(file.path());
				auto const end = std::remove_if(problems.begin(), problems.end(),
					[&](AssetBuildReport::Problem const& p) { return Key(p.asset) == key; });
				if (end == problems.end()) continue;
				problems.erase(end, problems.end());
				AssetBuildReport::Write(file.path(), problems);
			}
		}
	}

	void LoadBuildReports(std::filesystem::path const& buildDir)
	{
		s_buildDir = buildDir;
		for (AssetBuildReport::Problem const& p : AssetBuildReport::ReadAll(buildDir)) {
			s_assets[Key(p.asset)] = { p.asset, p.message };
			CRU_USER_ERROR("[Build] {} failed to compile: {}", p.asset, p.message);
		}
		if (!s_assets.empty()) {
			CRU_USER_ERROR("[Build] {} asset(s) failed to compile on the last build. Play is blocked until they are "
			               "fixed (re-export or remove them; the editor recompiles them when they change).", s_assets.size());
		}
	}

	void SetAssetFailed(std::string const& asset, std::string const& message)
	{
		s_assets[Key(asset)] = { asset, message };
	}

	void ClearAsset(std::string const& asset)
	{
		std::string const key = Key(asset);
		if (s_assets.erase(key) == 0) return;
		RemoveFromReports(key);
		if (s_assets.empty()) CRU_USER_INFO("[Build] All asset compile errors are fixed.");
	}

	void PruneAssets(std::function<bool(std::string const&)> const& exists)
	{
		std::vector<std::string> gone;
		for (auto const& [key, entry] : s_assets) {
			// "Assets (all models)" and the like are not files: only a rebuild clears them.
			if (entry.asset.rfind("Assets (", 0) == 0) continue;
			if (!exists(entry.asset)) gone.push_back(entry.asset);
		}
		for (std::string const& asset : gone) ClearAsset(asset);
	}

	std::size_t AssetProblemCount() { return s_assets.size(); }

	void SetScriptState(bool compiling, int errorCount)
	{
		s_scriptsCompiling = compiling;
		s_scriptErrors = errorCount;
	}

	std::string PlayBlockReason()
	{
		std::vector<std::string> parts;
		if (!s_assets.empty()) {
			std::string text = std::to_string(s_assets.size()) + " asset(s) failed to compile (";
			std::size_t shown = 0;
			for (auto const& [key, entry] : s_assets) {
				if (shown == 3) { text += ", ..."; break; }
				text += (shown++ ? ", " : "") + entry.asset;
			}
			parts.push_back(text + ")");
		}
		if (s_scriptErrors > 0) parts.push_back("scripts have " + std::to_string(s_scriptErrors) + " compile error(s)");
		else if (s_scriptsCompiling) parts.push_back("scripts are still compiling - try again in a moment");

		if (parts.empty()) return {};
		std::string reason = parts[0];
		for (std::size_t i = 1; i < parts.size(); ++i) reason += " and " + parts[i];
		return reason + ". Fix them (see the Console), then press Play again.";
	}
}
