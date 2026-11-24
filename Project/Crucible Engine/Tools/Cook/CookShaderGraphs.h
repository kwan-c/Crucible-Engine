/*!
* @file			CookShaderGraphs.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			17 November 2026
*
* @brief		Cook's shader work: compiling every shader graph into the shader the game loads,
*				and checking that every material's shader has compiled code.
*
* A shader graph (Assets/.../<Name>.shadergraph) is a mesh shader the Editor builds out of nodes.
* A material names it by its asset GUID ("shader": "7C3E91B4D2A65F08" in a .mat), and the game
* loads Resources/Shaders/<GUID>.spv and <GUID>.params.json by that name - it has no compiler and
* never reads a graph. This file makes those two files from each graph, the same way the Editor
* does when a graph is saved (ShaderGraph::Generate, then slangc with the build's flags), so a
* clean checkout and a packaged game have every graph's shader without the Editor having run:
*
*   1. Every .shadergraph in Assets that has a .meta (its GUID) is generated and compiled. The
*      outputs are written only when their bytes change; the compiler's own cache (ShaderCompiler,
*      shared with the Editor when Cook runs inside it) makes an unchanged graph cost a hash.
*   2. A graph that has a problem that stops its shader, or does not compile, has its outputs
*      REMOVED: the game then draws its materials with the error material instead of an old
*      version nobody can see in the Editor any more. Reported as a warning.
*   3. Outputs whose graph is gone from Assets are removed too (with their .meta).
*   4. (CheckMaterialShaders) Every .mat that names a shader must find <name>.spv in
*      Resources/Shaders, or it would draw with the error material: reported as missing.
*
* Without slangc on the machine nothing is compiled or removed: the outputs already there are
* kept, with a warning that they may be out of date (and a missing entry for a graph that has
* none).
*
* Used by Cook::Run (the Cook target, File -> Cook Resources, File -> Package Game) and alone by
* the build's ShaderGraphs target (CrucibleCook --shader-graphs), which runs before Game is built.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "Cook.h"

#include <filesystem>

namespace Crucible::Cook {

	/// Compiles every shader graph in `assets` into `resources`/Shaders (steps 1-3 above).
	void CookShaderGraphs(std::filesystem::path const& assets, std::filesystem::path const& resources,
	                      ShaderOptions const& options, Report& report);

	/// Step 4 above: every material's shader has compiled code in `resources`/Shaders.
	void CheckMaterialShaders(std::filesystem::path const& assets, std::filesystem::path const& resources, Report& report);

}
