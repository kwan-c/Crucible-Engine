/*!
* @file			ShaderGraphSubgraphs.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			17 November 2026
*
* @brief		Sub-graphs: a group of nodes saved as a file of its own and used in shader graphs as
*				one node.
*
* A sub-graph is a .shadersubgraph file: the .shadergraph format with "domain": "subgraph". In
* place of parameters and an Output node it has
*   - "inputs": named values it is given - float to float4, colour, or a texture. Inside, a
*     SubgraphInput node (setting "input": the name) reads one;
*   - "outputs": named values it gives back - float to float4 or colour, never a texture. Inside,
*     a SubgraphOutput node (setting "output": the name) takes one.
* Both lists are written like parameters (name, type, default). A sub-graph has no parameters of
* its own, reads none (no Parameter nodes) and has no Output node: anything a material should set
* is a parameter of the graph that uses it, wired into one of its inputs.
*
* In a shader graph a sub-graph is a Subgraph node (setting "subgraph": the file's asset GUID, so
* it survives a rename or a move), with an input pin per input and an output pin per output. An
* input pin with nothing linked is a constant like any other: the node's own, or the input's
* default. A texture input must be linked.
*
* When a shader is generated (ShaderGraphGenerator.h), every Subgraph node is first EXPANDED in
* place: the sub-graph's nodes are copied in under new ids and wired to what the node was wired
* to (Flatten, below). So the generator, its type rules and its "any" pins work on sub-graphs
* unchanged. Each input and output passes through a node of its declared type, so what a pin is
* given is checked against what the sub-graph declares. Anything wrong inside is reported on the
* Subgraph node. A sub-graph may use other sub-graphs, but not itself, directly or not.
*
* The LIBRARY below is where generation finds sub-graphs by GUID. Whoever generates fills it: the
* Editor from the asset index (ShaderGraphAsset.h, SyncSubgraphs), Cook from the Assets folder,
* the tests by hand. One library per program; frame thread (or one thread) only.
*
* No engine, no Vulkan: std and the graph's own files only.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "ShaderGraph/ShaderGraph.h"
#include "ShaderGraph/ShaderGraphNodes.h"

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Crucible::ShaderGraph {

	/// What a sub-graph's file name ends in.
	inline constexpr const char* kSubgraphExtension = ".shadersubgraph";

	/// In a shader graph: a sub-graph used as one node. Its "subgraph" setting is the file's GUID.
	inline constexpr const char* kSubgraphNode    = "Subgraph";
	inline constexpr const char* kSubgraphSetting = "subgraph";
	/// Its sub-graph's file name when it was last pointed at one ("RimGlow.shadersubgraph"): only a hint,
	/// for saying so when the GUID is lost (a file moved without its .meta gets a new one).
	inline constexpr const char* kSubgraphLabelSetting = "label";
	/// In a sub-graph: reads one of its inputs ("input" setting: the input's name).
	inline constexpr const char* kSubgraphInputNode   = "SubgraphInput";
	inline constexpr const char* kSubgraphInputSetting = "input";
	/// In a sub-graph: gives one of its outputs ("output" setting: the output's name).
	inline constexpr const char* kSubgraphOutputNode   = "SubgraphOutput";
	inline constexpr const char* kSubgraphOutputSetting = "output";
	/// Made by Flatten only, never saved: passes a value through, checked against its "type"
	/// setting (a parameter type's name: "float", "vec3", "colour" ...).
	inline constexpr const char* kPassNode = "_Pass";

	/// The type a parameter (or a sub-graph's input or output) of this kind carries.
	ValueType ValueTypeOf(ParameterType type);

	/// A sub-graph in the library.
	struct SubgraphEntry {
		std::string guid;    ///< what a Subgraph node's "subgraph" setting says: 16 hex digits
		std::string label;   ///< its file name, "Glow.shadersubgraph": what menus and messages call it
		Graph       graph;
		std::string error;   ///< why it cannot be used ("" = it can): a file that does not load, bad inputs ...
		NodeType    type;    ///< the node that uses it: one input pin per input, one output pin per output
		std::deque<std::string> names;   ///< where `type`'s pin names live
	};

	/*********************************************************************
	 \brief  Adds a sub-graph to the library from its file's text, or
	         replaces the one with that GUID. A file that does not load or
	         is not a usable sub-graph is added all the same, with
	         SubgraphEntry::error saying why, so that a graph using it says so.
	 *********************************************************************/
	void RegisterSubgraph(std::string_view guid, std::string_view label, std::string_view fileText);

	/// The same, for a graph already loaded (the tests).
	void RegisterSubgraph(std::string_view guid, std::string_view label, Graph graph);

	void UnregisterSubgraph(std::string_view guid);
	void ClearSubgraphs();

	/// The sub-graph with this GUID (any case), or null. Valid until the library next changes.
	const SubgraphEntry* FindSubgraph(std::string_view guid);

	/// Every sub-graph, sorted by label. Valid until the library next changes.
	std::vector<const SubgraphEntry*> ListSubgraphs();

	/// A number that changes whenever the library does: whoever caches generated shaders
	/// generates again when it changes.
	uint64_t SubgraphsVersion();

	/*********************************************************************
	 \brief  The pins of a node in `graph`: its row of the node table; for
	         a Subgraph node, its sub-graph's; for a SubgraphInput or
	         SubgraphOutput node, one pin of the type of the sub-graph's
	         input or output it names.
	 \return null for a node whose pins are not known: not in the table, a
	         sub-graph that is not in the library or cannot be used, an input
	         or output the sub-graph does not have.
	 *********************************************************************/
	const NodeType* TypeOfNode(const Graph& graph, const Node& node);

	/// A graph with its Subgraph nodes expanded, and where each node that came from one came from.
	struct Flattened {
		Graph graph;
		/// A node made by expanding: the Subgraph node of the ORIGINAL graph it is part of.
		std::unordered_map<uint32_t, uint32_t> origin;
		/// A node made by expanding: the sub-graph it came straight from ("Glow.shadersubgraph").
		std::unordered_map<uint32_t, std::string> from;
		/// A _Pass node for an input or output: that input's or output's name (what its pin is called
		/// on the Subgraph node).
		std::unordered_map<uint32_t, std::string> pinOf;
		/// A Subgraph node that could not be expanded, and why. It stays in the graph.
		std::unordered_map<uint32_t, std::string> failed;
	};

	/// Expands every Subgraph node of `graph`, sub-graphs inside sub-graphs too (see the file comment).
	Flattened Flatten(const Graph& graph);

	/// Why a sub-graph cannot be used ("" if it can): what SubgraphEntry::error says for it.
	std::string SubgraphProblem(const Graph& subgraph);

	/*********************************************************************
	 \brief  A sub-graph as a shader graph of its own, to check it and to
	         preview it while it is edited (ShaderGraphGenerator.h does this
	         for a graph whose domain is "subgraph"). Node ids are kept, so
	         what the generator says is about the sub-graph's own nodes:
	           - a number input reads its default; a texture input, a
	             texture parameter of its own ("_in_<name>": its default);
	           - each output passes through a node of its declared type that
	             counts as used (setting "subgraphOutput"), so a problem on
	             the way to any output stops it;
	           - output `previewOutput` goes into the Output's albedo (a vec2
	             with z = 0, a vec4's xyz), if there is one.
	 *********************************************************************/
	Graph SubgraphAsSurface(const Graph& subgraph, size_t previewOutput);

}
