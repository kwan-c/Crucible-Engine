/*!
* @file			Cook.h
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		Cook: fills the Resources folder with everything the shipped game reads.
*
* The installer packages Resources only, so after a Cook it must hold every file the game
* opens. Compiled files (.cmodel, .cfont, .spv) are already written there by their
* compilers during the build or by the editor; Cook adds the rest:
*
*   1. Copies the plain files the game reads from Assets into the same place in Resources,
*      each with its .meta (so it keeps its GUID): scenes, prefabs, .mat, audio, animator
*      controllers and Config/*.json.
*   2. Removes the copies whose original is gone from Assets, and image copies older Cooks
*      made (never a compiled file).
*   3. Checks every source that needs compiling has its compiled file: a .glb its .cmodel
*      (build CompiledModels), a .ttf/.otf its .cfont (Font Importer -> Compile), an image its
*      .bill or, for "*_skybox" images and .hdr, its .sky (build CompiledTextures).
*   4. Checks every image a scene or prefab uses as a skybox is named "*_skybox", so it is
*      compiled as a sky (otherwise the game would show the default sky there).
*
* Never copies source-only files (.glb, .png/.jpg/.hdr, .ttf, .fontsettings, .slang, .cs, ...).
* Used by the CrucibleCook tool (the "Cook" build target, for the installer) and by the
* editor's File -> Cook Resources. Writes nothing to Assets.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Crucible::Cook {

	struct Report {
		std::size_t copied    = 0;   ///< files copied (new or changed), .meta files not counted
		std::size_t unchanged = 0;   ///< already up to date in Resources
		std::size_t removed   = 0;   ///< copies deleted because the original is gone
		std::vector<std::string> missing;    ///< compiled files the game needs but Resources lacks
		std::vector<std::string> warnings;   ///< everything else worth reading
		std::vector<std::string> errors;     ///< files that could not be copied or removed

		/// True when Resources is complete: nothing failed and nothing is missing.
		bool Ok() const { return errors.empty() && missing.empty(); }
	};

	/// Whether Cook copies this Assets file (virtual path, forward slashes) into Resources.
	bool ShouldCopy(std::string const& virtualPath);

	/// Runs Cook from the Assets folder into the Resources folder (both must exist).
	Report Run(std::filesystem::path const& assets, std::filesystem::path const& resources);

}
