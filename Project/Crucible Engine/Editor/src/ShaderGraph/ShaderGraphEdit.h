/*!
* @file			ShaderGraphEdit.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		The edits a shader graph editor makes, as plain functions on the graph:
*				add and delete nodes, link pins, copy and paste, manage parameters.
*
* Nothing here draws or knows about ImGui, so every edit can be tested on its own
* (Tools/ShaderGraphTests). ShaderGraphView and the Shader Graph panel call these and nothing
* else changes a graph's nodes, links or parameters - apart from dragging a node about and
* typing a constant into a pin.
*
* The rules the edits keep:
*   - a graph has exactly one Output node: it cannot be deleted, copied or added twice;
*   - an input pin takes one link: linking into it replaces the link that was there;
*   - a link that would make a loop, or that does not fit the pin (ShaderGraphNodes.h has the
*     type rules), is refused and the reason given;
*   - deleting a node deletes its links; deleting a parameter deletes the nodes that read it;
*     renaming a parameter renames it in those nodes.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "ShaderGraph/ShaderGraph.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Crucible::ShaderGraph {

	/// A graph to start from: one Output node.
	Graph NewGraph();

	/// The graph's Output node's id, or 0 if it has none.
	uint32_t OutputNodeOf(const Graph& graph);

	/*********************************************************************
	 \brief  Adds a node of `type` at (x, y).
	 \param  parameter  For a Parameter node: the parameter it reads.
	 \return Its id; 0 if `type` is not in the node table, or is a second
	         Output.
	 *********************************************************************/
	uint32_t AddNode(Graph& graph, std::string_view type, float x, float y, std::string_view parameter = {});

	/// Removes these nodes and every link to or from them. The Output node stays. Returns how many went.
	size_t DeleteNodes(Graph& graph, const std::vector<uint32_t>& ids);

	/// Removes the link into this input pin. False if there was none.
	bool DeleteLinkInto(Graph& graph, uint32_t toNode, std::string_view toPin);

	/*********************************************************************
	 \brief  Puts a Reroute node at (x, y) on the link into this input pin:
	         the link then runs from its output into the Reroute, and from
	         the Reroute into the pin.
	 \param  why  If refused: the reason, as one sentence for the user.
	 \return The Reroute's id; 0, and the graph untouched, if there is no
	         such link or a Reroute cannot carry it (a texture).
	 *********************************************************************/
	uint32_t InsertReroute(Graph& graph, uint32_t toNode, std::string_view toPin, float x, float y, std::string& why);

	// ---- Comment boxes -------------------------------------------------------------------------

	/// Removes these comment boxes (never the nodes in them). Returns how many went.
	size_t DeleteComments(Graph& graph, const std::vector<uint32_t>& ids);

	/*********************************************************************
	 \brief  Links an output pin to an input pin, replacing whatever was
	         linked into that input.
	 \param  why  If refused: the reason, as one sentence for the user.
	 \return false, and the graph untouched, if a pin is not there, the
	         link would make a loop, or the types do not fit.
	 *********************************************************************/
	bool Connect(Graph& graph, uint32_t fromNode, std::string_view fromPin, uint32_t toNode, std::string_view toPin, std::string& why);

	// ---- Copy and paste ------------------------------------------------------------------------

	/// Nodes lifted out of a graph, with the links that run between them.
	struct Clipboard {
		std::vector<Node> nodes;
		std::vector<Link> links;
		bool Empty() const { return nodes.empty(); }
	};

	/// Copies these nodes (never the Output) and the links between them.
	Clipboard Copy(const Graph& graph, const std::vector<uint32_t>& ids);

	/*********************************************************************
	 \brief  Adds the clipboard's nodes to the graph under new ids, moved
	         so that their top-left corner is at (x, y).
	 \return The new nodes' ids, in the clipboard's order. A Parameter node
	         whose parameter this graph does not have is pasted all the
	         same (and shows as a problem until one is chosen).
	 *********************************************************************/
	std::vector<uint32_t> Paste(Graph& graph, const Clipboard& clipboard, float x, float y);

	// ---- Parameters ----------------------------------------------------------------------------

	/// `wanted` if no parameter has that name, else `wanted` with a number after it. Never empty.
	std::string UniqueParameterName(const Graph& graph, std::string_view wanted);

	/// Adds a parameter of `type` under a new name and returns its place in Graph::parameters.
	size_t AddParameter(Graph& graph, ParameterType type);

	/*********************************************************************
	 \brief  Renames a parameter, in the list and in every node reading it.
	 \return false, and why, if the name cannot be used (not letters,
	         digits and '_'; or another parameter has it).
	 \note   Materials keep their values BY NAME (.mat files): after a
	         rename they use the new parameter's default until set again.
	 *********************************************************************/
	bool RenameParameter(Graph& graph, size_t index, std::string_view name, std::string& why);

	/// Removes a parameter and the Parameter nodes that read it (with their links).
	void DeleteParameter(Graph& graph, size_t index);

	/// Moves a parameter one place up (-1) or down (+1) the list. False if it cannot move that way.
	bool MoveParameter(Graph& graph, size_t index, int by);

}
