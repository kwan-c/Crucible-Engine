/*!
* @file			ShaderGraphEdit.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Implementation - see ShaderGraphEdit.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ShaderGraph/ShaderGraphEdit.h"

#include "ShaderGraph/ShaderGraphGenerator.h"
#include "ShaderGraph/ShaderGraphNodes.h"

#include <algorithm>
#include <set>
#include <unordered_map>

namespace Crucible::ShaderGraph {

	namespace {

		bool Contains(const std::vector<uint32_t>& ids, uint32_t id) {
			return std::find(ids.begin(), ids.end(), id) != ids.end();
		}

		// The problems the generator finds on one node, as text: what Connect compares before
		// and after a link to see whether the link is the cause of one.
		std::set<std::string> ProblemsOn(const Graph& graph, uint32_t node) {
			std::set<std::string> found;
			for (const Problem& problem : Generate(graph).problems) {
				if (problem.node == node) { found.insert(problem.pin + "\n" + problem.message); }
			}
			return found;
		}

	}   // anonymous namespace

	Graph NewGraph() {
		Graph graph;
		graph.AddNode(kOutputNode, 400.0f, 100.0f);
		return graph;
	}

	uint32_t OutputNodeOf(const Graph& graph) {
		for (const Node& node : graph.nodes) {
			if (node.type == kOutputNode) { return node.id; }
		}
		return 0;
	}

	uint32_t AddNode(Graph& graph, std::string_view type, float x, float y, std::string_view parameter) {
		if (!FindNodeType(type)) { return 0; }
		if (type == kOutputNode && OutputNodeOf(graph) != 0) { return 0; }
		const uint32_t id = graph.AddNode(std::string(type), x, y);
		if (type == kParameterNode && !parameter.empty()) {
			graph.FindNode(id)->settings[kParameterSetting] = std::string(parameter);
		}
		return id;
	}

	size_t DeleteNodes(Graph& graph, const std::vector<uint32_t>& ids) {
		const uint32_t output = OutputNodeOf(graph);
		const auto going = [&](uint32_t id) { return id != output && Contains(ids, id); };
		const size_t before = graph.nodes.size();
		std::erase_if(graph.nodes, [&](const Node& node) { return going(node.id); });
		std::erase_if(graph.links, [&](const Link& link) { return going(link.fromNode) || going(link.toNode); });
		return before - graph.nodes.size();
	}

	bool DeleteLinkInto(Graph& graph, uint32_t toNode, std::string_view toPin) {
		return std::erase_if(graph.links, [&](const Link& link) { return link.toNode == toNode && link.toPin == toPin; }) != 0;
	}

	bool Connect(Graph& graph, uint32_t fromNode, std::string_view fromPin, uint32_t toNode, std::string_view toPin, std::string& why) {
		const Node* from = graph.FindNode(fromNode);
		const Node* to = graph.FindNode(toNode);
		const NodeType* fromType = from ? FindNodeType(from->type) : nullptr;
		const NodeType* toType = to ? FindNodeType(to->type) : nullptr;
		if (!fromType || !toType || fromType->FindOutput(fromPin) < 0 || toType->FindInput(toPin) < 0) {
			why = "One of those pins is not there.";
			return false;
		}
		if (fromNode == toNode) {
			why = "A node cannot feed itself.";
			return false;
		}

		// Try it on a copy: the link is refused if it brings a problem to either of its nodes
		// that was not there without it (a loop, or types that do not fit).
		Graph without = graph;
		DeleteLinkInto(without, toNode, toPin);
		Graph with = without;
		with.links.push_back({ fromNode, std::string(fromPin), toNode, std::string(toPin) });

		for (const uint32_t node : { toNode, fromNode }) {
			const std::set<std::string> before = ProblemsOn(without, node);
			for (const std::string& problem : ProblemsOn(with, node)) {
				if (before.count(problem) != 0) { continue; }
				const size_t split = problem.find('\n');
				const std::string pin = problem.substr(0, split);
				std::string message = problem.substr(split + 1);
				if (!message.empty()) { message[0] = static_cast<char>(message[0] >= 'a' && message[0] <= 'z' ? message[0] - 'a' + 'A' : message[0]); }
				why = (pin.empty() || pin == toPin ? std::string() : "\"" + pin + "\": ") + message + ".";
				return false;
			}
		}

		graph = std::move(with);
		return true;
	}

	Clipboard Copy(const Graph& graph, const std::vector<uint32_t>& ids) {
		Clipboard clipboard;
		const uint32_t output = OutputNodeOf(graph);
		std::vector<uint32_t> copied;
		for (const Node& node : graph.nodes) {
			if (node.id != output && Contains(ids, node.id)) {
				clipboard.nodes.push_back(node);
				copied.push_back(node.id);
			}
		}
		for (const Link& link : graph.links) {
			if (Contains(copied, link.fromNode) && Contains(copied, link.toNode)) { clipboard.links.push_back(link); }
		}
		return clipboard;
	}

	std::vector<uint32_t> Paste(Graph& graph, const Clipboard& clipboard, float x, float y) {
		std::vector<uint32_t> pasted;
		if (clipboard.Empty()) { return pasted; }

		float left = clipboard.nodes.front().x, top = clipboard.nodes.front().y;
		for (const Node& node : clipboard.nodes) {
			left = (std::min)(left, node.x);
			top = (std::min)(top, node.y);
		}

		std::unordered_map<uint32_t, uint32_t> newId;
		for (const Node& original : clipboard.nodes) {
			if (original.type == kOutputNode) { continue; }   // never two
			Node node = original;
			node.id = (std::max)(graph.nextId, 1u);
			node.x = x + (original.x - left);
			node.y = y + (original.y - top);
			graph.nextId = node.id + 1;
			newId[original.id] = node.id;
			pasted.push_back(node.id);
			graph.nodes.push_back(std::move(node));
		}
		for (const Link& link : clipboard.links) {
			const auto from = newId.find(link.fromNode);
			const auto to = newId.find(link.toNode);
			if (from != newId.end() && to != newId.end()) { graph.links.push_back({ from->second, link.fromPin, to->second, link.toPin }); }
		}
		return pasted;
	}

	std::string UniqueParameterName(const Graph& graph, std::string_view wanted) {
		const std::string base = wanted.empty() ? std::string("parameter") : std::string(wanted);
		if (!graph.FindParameter(base)) { return base; }
		for (int number = 2;; ++number) {
			const std::string name = base + std::to_string(number);
			if (!graph.FindParameter(name)) { return name; }
		}
	}

	size_t AddParameter(Graph& graph, ParameterType type) {
		Parameter parameter;
		parameter.type = type;
		switch (type) {
		case ParameterType::Colour:  parameter.name = UniqueParameterName(graph, "colour"); parameter.defaultValue = { 1.0f, 1.0f, 1.0f, 1.0f }; break;
		case ParameterType::Texture: parameter.name = UniqueParameterName(graph, "texture"); break;
		case ParameterType::Float:   parameter.name = UniqueParameterName(graph, "value"); break;
		default:                     parameter.name = UniqueParameterName(graph, "vector"); break;
		}
		graph.parameters.push_back(std::move(parameter));
		return graph.parameters.size() - 1;
	}

	bool RenameParameter(Graph& graph, size_t index, std::string_view name, std::string& why) {
		if (index >= graph.parameters.size()) {
			why = "That parameter is not there.";
			return false;
		}
		const std::string old = graph.parameters[index].name;
		if (name == old) { return true; }
		if (!IsIdentifier(name)) {
			why = "A name is a letter or '_', then letters, digits and '_' (no spaces).";
			return false;
		}
		if (graph.FindParameter(name)) {
			why = "Another parameter is already called " + std::string(name) + ".";
			return false;
		}
		graph.parameters[index].name = std::string(name);
		for (Node& node : graph.nodes) {
			if (node.type != kParameterNode) { continue; }
			const auto setting = node.settings.find(kParameterSetting);
			if (setting != node.settings.end() && setting->second == old) { setting->second = std::string(name); }
		}
		return true;
	}

	void DeleteParameter(Graph& graph, size_t index) {
		if (index >= graph.parameters.size()) { return; }
		const std::string name = graph.parameters[index].name;
		std::vector<uint32_t> readers;
		for (const Node& node : graph.nodes) {
			if (node.type != kParameterNode) { continue; }
			const auto setting = node.settings.find(kParameterSetting);
			if (setting != node.settings.end() && setting->second == name) { readers.push_back(node.id); }
		}
		DeleteNodes(graph, readers);
		graph.parameters.erase(graph.parameters.begin() + static_cast<std::ptrdiff_t>(index));
	}

	bool MoveParameter(Graph& graph, size_t index, int by) {
		if (index >= graph.parameters.size() || (by != -1 && by != 1)) { return false; }
		if ((by < 0 && index == 0) || (by > 0 && index + 1 >= graph.parameters.size())) { return false; }
		std::swap(graph.parameters[index], graph.parameters[by < 0 ? index - 1 : index + 1]);
		return true;
	}

}
