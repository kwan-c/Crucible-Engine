/*!
* @file     BuildProblems.h
* @brief    What is stopping Play in the editor: assets that failed to compile and
*           C# scripts with compile errors.
*
* The everyday build no longer stops on a bad model or image (the compilers list it in a
* report instead, see Tools/Common/AssetBuildReport.h), so the editor always opens. This
* keeps the list of problems and gives PlayMode its Play guard:
*   - On startup, LoadBuildReports() reads the build's reports and logs each problem.
*   - The Asset Browser and Texture Importer compile assets in the editor too: a failure
*     adds the asset (SetAssetFailed), a success removes it (ClearAsset), also from the
*     report, so a fixed asset stays fixed after the editor restarts.
*   - The script hot reload reports its state every frame (SetScriptState).
*   - PlayBlockReason() is "" when Play is allowed, else why not.
*
* Main thread only.
*/
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

namespace Crucible::BuildProblems
{
	// Reads <buildDir>/AssetBuildReports and logs each problem to the Console.
	void LoadBuildReports(std::filesystem::path const& buildDir);

	// asset: the source's virtual path in Assets, e.g. "Models/Rock.glb".
	void SetAssetFailed(std::string const& asset, std::string const& message);
	void ClearAsset(std::string const& asset);

	// Drops problems for assets that no longer exist (exists(virtualPath) == false).
	void PruneAssets(std::function<bool(std::string const&)> const& exists);

	std::size_t AssetProblemCount();

	// From ScriptHotReload, every frame.
	void SetScriptState(bool compiling, int errorCount);

	// "" when Play is allowed; otherwise one sentence for the Console and the Play button.
	std::string PlayBlockReason();
}
