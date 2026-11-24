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
#include "ShaderGraph/ShaderGraphSubgraphs.h"

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
		const bool subgraph = graph.domain == kSubgraphDomain;
		if (type == kSubgraphInputNode || type == kSubgraphOutputNode) {
			// Only in a sub-graph, and only for one of its inputs or outputs (named by `parameter`).
			const bool input = type == kSubgraphInputNode;
			const std::vector<Parameter>& list = input ? graph.inputs : graph.outputs;
			if (!subgraph || std::none_of(list.begin(), list.end(), [&](const Parameter& p) { return p.name == parameter; })) { return 0; }
			const uint32_t id = graph.AddNode(std::string(type), x, y);
			graph.FindNode(id)->settings[input ? kSubgraphInputSetting : kSubgraphOutputSetting] = std::string(parameter);
			return id;
		}
		if (subgraph && (type == kOutputNode || type == kParameterNode)) { return 0; }   // a sub-graph has neither
		if (type == kSubgraphNode) {
			// A sub-graph, named by its GUID (passed as `parameter`): its pins come from its file.
			if (parameter.empty()) { return 0; }
			const uint32_t id = graph.AddNode(std::string(type), x, y);
			graph.FindNode(id)->settings[kSubgraphSetting] = std::string(parameter);
			if (const SubgraphEntry* entry = FindSubgraph(parameter)) { graph.FindNode(id)->settings[kSubgraphLabelSetting] = entry->label; }
			return id;
		}
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

	uint32_t InsertReroute(Graph& graph, uint32_t toNode, std::string_view toPin, float x, float y, std::string& why) {
		const Link* link = graph.FindLinkInto(toNode, toPin);
		if (!link) {
			why = "There is no link there.";
			return 0;
		}
		// On a copy: if either half is refused, the graph is as it was.
		Graph trial = graph;
		const Link original = *link;
		const uint32_t reroute = AddNode(trial, kRerouteNode, x, y);
		if (reroute == 0 || !Connect(trial, original.fromNode, original.fromPin, reroute, "in", why) ||
		    !Connect(trial, reroute, "out", original.toNode, original.toPin, why)) {
			why = "A reroute point carries numbers and vectors only - not this link. (" + why + ")";
			return 0;
		}
		graph = std::move(trial);
		return reroute;
	}

	bool SetSubgraph(Graph& graph, uint32_t node, std::string_view guid) {
		Node* target = graph.FindNode(node);
		const SubgraphEntry* entry = FindSubgraph(guid);
		if (!target || target->type != kSubgraphNode || !entry) { return false; }
		target->settings[kSubgraphSetting] = entry->guid;
		target->settings[kSubgraphLabelSetting] = entry->label;
		// Links on pins it has no longer go; a constant for an input it has no longer goes too.
		if (entry->error.empty()) {
			std::erase_if(graph.links, [&](const Link& link) {
				return (link.toNode == node && entry->type.FindInput(link.toPin) < 0) || (link.fromNode == node && entry->type.FindOutput(link.fromPin) < 0);
			});
			std::erase_if(target->inputs, [&](const auto& constant) { return entry->type.FindInput(constant.first) < 0; });
		}
		return true;
	}

	size_t DeleteComments(Graph& graph, const std::vector<uint32_t>& ids) {
		return std::erase_if(graph.comments, [&](const Comment& comment) { return Contains(ids, comment.id); });
	}

	bool Connect(Graph& graph, uint32_t fromNode, std::string_view fromPin, uint32_t toNode, std::string_view toPin, std::string& why) {
		const Node* from = graph.FindNode(fromNode);
		const Node* to = graph.FindNode(toNode);
		const NodeType* fromType = from ? TypeOfNode(graph, *from) : nullptr;
		const NodeType* toType = to ? TypeOfNode(graph, *to) : nullptr;
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

	// ---- Parameters, and a sub-graph's inputs and outputs -------------------------------------------

	namespace {

		// One of the three lists, and the node that reads (or gives) an entry of it by name.
		struct ListParts {
			std::vector<Parameter>* list;
			const char*             nodeType;
			const char*             setting;
			const char*             what;
		};

		ListParts PartsOf(Graph& graph, ListKind kind) {
			switch (kind) {
			case ListKind::Inputs:  return { &graph.inputs, kSubgraphInputNode, kSubgraphInputSetting, "input" };
			case ListKind::Outputs: return { &graph.outputs, kSubgraphOutputNode, kSubgraphOutputSetting, "output" };
			default:                return { &graph.parameters, kParameterNode, kParameterSetting, "parameter" };
			}
		}

		bool Named(const std::vector<Parameter>& list, std::string_view name) {
			return std::any_of(list.begin(), list.end(), [&](const Parameter& p) { return p.name == name; });
		}

	}   // anonymous namespace

	std::string UniqueListName(const Graph& graph, ListKind kind, std::string_view wanted) {
		const std::vector<Parameter>& list = *PartsOf(const_cast<Graph&>(graph), kind).list;
		const std::string base = wanted.empty() ? std::string(PartsOf(const_cast<Graph&>(graph), kind).what) : std::string(wanted);
		if (!Named(list, base)) { return base; }
		for (int number = 2;; ++number) {
			const std::string name = base + std::to_string(number);
			if (!Named(list, name)) { return name; }
		}
	}

	size_t AddListEntry(Graph& graph, ListKind kind, ParameterType type) {
		if (kind == ListKind::Outputs && type == ParameterType::Texture) { type = ParameterType::Vec3; }   // a sub-graph gives back no textures
		Parameter entry;
		entry.type = type;
		switch (type) {
		case ParameterType::Colour:  entry.name = UniqueListName(graph, kind, "colour"); entry.defaultValue = { 1.0f, 1.0f, 1.0f, 1.0f }; break;
		case ParameterType::Texture: entry.name = UniqueListName(graph, kind, "texture"); break;
		case ParameterType::Float:   entry.name = UniqueListName(graph, kind, "value"); break;
		default:                     entry.name = UniqueListName(graph, kind, "vector"); break;
		}
		std::vector<Parameter>& list = *PartsOf(graph, kind).list;
		list.push_back(std::move(entry));
		return list.size() - 1;
	}

	bool RenameListEntry(Graph& graph, ListKind kind, size_t index, std::string_view name, std::string& why) {
		const ListParts parts = PartsOf(graph, kind);
		std::vector<Parameter>& list = *parts.list;
		if (index >= list.size()) {
			why = std::string("That ") + parts.what + " is not there.";
			return false;
		}
		const std::string old = list[index].name;
		if (name == old) { return true; }
		if (!IsIdentifier(name)) {
			why = "A name is a letter or '_', then letters, digits and '_' (no spaces).";
			return false;
		}
		if (Named(list, name)) {
			why = std::string("Another ") + parts.what + " is already called " + std::string(name) + ".";
			return false;
		}
		list[index].name = std::string(name);
		for (Node& node : graph.nodes) {
			if (node.type != parts.nodeType) { continue; }
			const auto setting = node.settings.find(parts.setting);
			if (setting != node.settings.end() && setting->second == old) { setting->second = std::string(name); }
		}
		return true;
	}

	void DeleteListEntry(Graph& graph, ListKind kind, size_t index) {
		const ListParts parts = PartsOf(graph, kind);
		std::vector<Parameter>& list = *parts.list;
		if (index >= list.size()) { return; }
		const std::string name = list[index].name;
		std::vector<uint32_t> readers;
		for (const Node& node : graph.nodes) {
			if (node.type != parts.nodeType) { continue; }
			const auto setting = node.settings.find(parts.setting);
			if (setting != node.settings.end() && setting->second == name) { readers.push_back(node.id); }
		}
		DeleteNodes(graph, readers);
		list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
	}

	bool MoveListEntry(Graph& graph, ListKind kind, size_t index, int by) {
		std::vector<Parameter>& list = *PartsOf(graph, kind).list;
		if (index >= list.size() || (by != -1 && by != 1)) { return false; }
		if ((by < 0 && index == 0) || (by > 0 && index + 1 >= list.size())) { return false; }
		std::swap(list[index], list[by < 0 ? index - 1 : index + 1]);
		return true;
	}

	std::string UniqueParameterName(const Graph& graph, std::string_view wanted) { return UniqueListName(graph, ListKind::Parameters, wanted.empty() ? "parameter" : wanted); }
	size_t AddParameter(Graph& graph, ParameterType type) { return AddListEntry(graph, ListKind::Parameters, type); }
	bool RenameParameter(Graph& graph, size_t index, std::string_view name, std::string& why) { return RenameListEntry(graph, ListKind::Parameters, index, name, why); }
	void DeleteParameter(Graph& graph, size_t index) { DeleteListEntry(graph, ListKind::Parameters, index); }
	bool MoveParameter(Graph& graph, size_t index, int by) { return MoveListEntry(graph, ListKind::Parameters, index, by); }

	Graph NewSubgraph() {
		Graph graph;
		graph.domain = kSubgraphDomain;
		graph.outputs.push_back({ "out", ParameterType::Vec3, {}, 0.0f, 1.0f, true, "white" });
		const uint32_t output = graph.AddNode(kSubgraphOutputNode, 400.0f, 100.0f);
		graph.FindNode(output)->settings[kSubgraphOutputSetting] = "out";
		return graph;
	}

}
