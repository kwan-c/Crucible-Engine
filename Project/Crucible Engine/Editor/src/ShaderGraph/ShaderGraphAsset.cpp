/*!
* @file			ShaderGraphAsset.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Implementation - see ShaderGraphAsset.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ShaderGraph/ShaderGraphAsset.h"

#include "ShaderGraph/ShaderGraph.h"
#include "ShaderGraph/ShaderGraphGenerator.h"
#include "ShaderGraph/ShaderGraphNodes.h"

#include "AssetPipe/Assetmanager.h"
#include "AssetPipe/AssetRegistry.h"
#include "Diagnostics/Logger.h"

#include <algorithm>

namespace Crucible {

	namespace {

		constexpr size_t kGuidDigits = 16;   // AssetRegistry::GuidToString

		ShaderGraphFile FileOf(const AssetEntry& entry) {
			ShaderGraphFile graph;
			graph.shaderName = AssetRegistry::GuidToString(entry.guid);
			graph.virtualPath = entry.virtualPath;
			const size_t slash = entry.virtualPath.find_last_of('/');
			graph.label = slash == std::string::npos ? entry.virtualPath : entry.virtualPath.substr(slash + 1);
			graph.file = std::filesystem::path(entry.absolutePath);
			return graph;
		}

		bool IsShaderGraph(const AssetEntry& entry) {
			return !entry.compiled && entry.extension == kShaderGraphExtension && entry.guid.m_Value != 0;
		}

	}   // anonymous namespace

	bool FindShaderGraph(const AssetManager& assets, std::string_view shaderName, ShaderGraphFile& out) {
		const bool isGuid = shaderName.size() == kGuidDigits && std::all_of(shaderName.begin(), shaderName.end(), [](char c) {
			return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
		});
		if (!isGuid) { return false; }

		const xresource::instance_guid guid = AssetRegistry::GuidFromString(std::string(shaderName));
		const AssetEntry* entry = guid.m_Value != 0 ? assets.FindByGuid(guid) : nullptr;
		if (!entry || !IsShaderGraph(*entry)) { return false; }
		out = FileOf(*entry);
		return true;
	}

	bool ShaderGraphReadsMaterialFactors(const AssetManager& assets, const ShaderGraphFile& graph) {
		std::string text, error;
		ShaderGraph::Graph loaded;
		if (!assets.ReadText(graph.virtualPath, text) || !ShaderGraph::Load(text, loaded, error)) { return false; }
		return std::any_of(loaded.nodes.begin(), loaded.nodes.end(), [](const ShaderGraph::Node& node) { return ShaderGraph::IsMaterialFactorNode(node.type); });
	}

	std::vector<ShaderGraphFile> ListShaderGraphs(const AssetManager& assets) {
		std::vector<ShaderGraphFile> graphs;
		for (const AssetEntry& entry : assets.GetAll()) {
			if (IsShaderGraph(entry)) { graphs.push_back(FileOf(entry)); }
		}
		std::sort(graphs.begin(), graphs.end(), [](const ShaderGraphFile& a, const ShaderGraphFile& b) { return a.label < b.label; });
		return graphs;
	}

	bool GenerateShaderGraph(const AssetManager& assets, std::string_view shaderName,
	                         std::string& source, std::string& paramsJson, std::string& error) {
		ShaderGraphFile file;
		if (!FindShaderGraph(assets, shaderName, file)) {
			error = "its .shadergraph file is no longer in Assets";
			return false;
		}
		std::string text;
		if (!assets.ReadText(file.virtualPath, text)) {
			error = "the file could not be read";
			return false;
		}

		ShaderGraph::Graph graph;
		if (!ShaderGraph::Load(text, graph, error)) { return false; }

		ShaderGraph::Generated generated = ShaderGraph::Generate(graph);
		size_t stopping = 0;
		for (const ShaderGraph::Problem& problem : generated.problems) {
			const std::string line = ShaderGraph::Describe(graph, problem);
			if (problem.stops) {
				++stopping;
				CRU_USER_ERROR("{}: {}", file.label, line);
			} else {
				CRU_USER_WARN("{}: {} (the Output does not use this node, so the shader is still made)", file.label, line);
			}
		}
		if (!generated.ok) {
			error = stopping == 1 ? "1 problem in the graph" : std::to_string(stopping) + " problems in the graph";
			return false;
		}
		source = std::move(generated.source);
		paramsJson = std::move(generated.paramsJson);
		return true;
	}

}
