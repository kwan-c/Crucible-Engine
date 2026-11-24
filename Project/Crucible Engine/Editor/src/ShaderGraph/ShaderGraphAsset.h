/*!
* @file			ShaderGraphAsset.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Shader graphs as assets: finding a graph's file from the shader name a
*				material uses, and turning that file into a shader.
*
* A shader graph is a file in Assets ending in .shadergraph. A material uses one the way it
* uses any mesh shader, by name ("shader" in its .mat) - and a graph's shader name is its
* asset GUID, the 16 hex digits in the .meta beside it. So the file can be renamed or moved
* and its materials still find it, and a graph can never be confused with a hand-written
* shader in Assets/Shaders, which goes by its file name.
*
* The graph's own data and the generator know nothing of the engine (ShaderGraph.h,
* ShaderGraphGenerator.h); this file is where they meet the asset index and the Console.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Crucible {

	class AssetManager;

	/// What a shader graph's file name ends in.
	inline constexpr const char* kShaderGraphExtension = ".shadergraph";

	struct ShaderGraphFile {
		std::string           shaderName;    ///< what a material's "shader" says to use it: the asset's GUID, 16 hex digits
		std::string           label;         ///< its file name, "Ember.shadergraph": what messages and menus call it
		std::string           virtualPath;   ///< where it is under Assets, "ShaderGraphs/Ember.shadergraph"
		std::filesystem::path file;          ///< the same, as a path on this machine
	};

	/*********************************************************************
	 \brief  The shader graph a material shader's name stands for.
	 \return false if `shaderName` is not the GUID of a .shadergraph file
	         the asset index knows (so: an ordinary, hand-written shader).
	 *********************************************************************/
	bool FindShaderGraph(const AssetManager& assets, std::string_view shaderName, ShaderGraphFile& out);

	/// Whether the graph's file has a node that reads one of the material's built-in controls
	/// (ShaderGraph::IsMaterialFactorNode). False too for a file that cannot be read.
	bool ShaderGraphReadsMaterialFactors(const AssetManager& assets, const ShaderGraphFile& graph);

	/// Every shader graph in Assets, sorted by label.
	std::vector<ShaderGraphFile> ListShaderGraphs(const AssetManager& assets);

	/*********************************************************************
	 \brief  Brings the sub-graph library (ShaderGraphSubgraphs.h) in line
	         with the .shadersubgraph files in Assets: adds new ones,
	         reads changed ones again, drops deleted ones. Looks at most
	         twice a second unless `force`. Call every frame (the shader hot
	         reload layer does), and before generating.
	 *********************************************************************/
	void SyncSubgraphs(const AssetManager& assets, bool force = false);

	/*********************************************************************
	 \brief  Reads a graph's file, checks it and writes its shader
	         (ShaderGraph::Generate). Each problem found goes to the
	         Console with the node it is on: as an error if it stops the
	         shader, as a warning if it is on a node the Output does not
	         use.
	 \param  shaderName  The graph, by the name a material uses.
	 \param  source      The whole shader, Slang.
	 \param  paramsJson  Its parameter list, the text of a .params.json.
	 \param  error       Why there is no shader, in a few words.
	 \return false if the file cannot be read, is not a graph, or has a
	         problem that stops the shader.
	 *********************************************************************/
	bool GenerateShaderGraph(const AssetManager& assets, std::string_view shaderName,
	                         std::string& source, std::string& paramsJson, std::string& error);

}
