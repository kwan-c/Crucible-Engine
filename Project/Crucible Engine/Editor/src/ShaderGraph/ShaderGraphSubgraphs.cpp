/*!
* @file			ShaderGraphSubgraphs.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			17 November 2026
*
* @brief		Implementation - see ShaderGraphSubgraphs.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ShaderGraph/ShaderGraphSubgraphs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace Crucible::ShaderGraph {

	namespace {

		constexpr size_t kMaxDepth = 16;   // sub-graphs inside sub-graphs, at most this deep
		constexpr std::array<ParameterType, 6> kAllTypes{ ParameterType::Float, ParameterType::Vec2, ParameterType::Vec3,
		                                                  ParameterType::Vec4, ParameterType::Colour, ParameterType::Texture };

		struct Library {
			std::map<std::string, std::unique_ptr<SubgraphEntry>> entries;   // by upper-case GUID
			uint64_t version = 1;
		};

		Library& TheLibrary() {
			static Library library;
			return library;
		}

		std::string Upper(std::string_view text) {
			std::string upper(text);
			for (char& c : upper) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
			return upper;
		}

		bool TypeFromName(std::string_view name, ParameterType& out) {
			for (const ParameterType type : kAllTypes) {
				if (name == ParameterTypeName(type)) {
					out = type;
					return true;
				}
			}
			return false;
		}

		// The rows SubgraphInput, SubgraphOutput and _Pass have, one per type a pin can carry.
		struct FixedTypes {
			std::array<NodeType, 6> input, output, pass;
		};

		const FixedTypes& Fixed() {
			static const FixedTypes fixed = [] {
				FixedTypes made;
				for (size_t i = 0; i < kAllTypes.size(); ++i) {
					const ValueType type = ValueTypeOf(kAllTypes[i]);
					const bool colour = kAllTypes[i] == ParameterType::Colour;
					made.input[i]  = { kSubgraphInputNode, "Sub-graph", "One of the sub-graph's inputs: what the node using it is given.",
					                   {}, { { "out", type, "" } } };
					made.output[i] = { kSubgraphOutputNode, "Sub-graph", "One of the sub-graph's outputs: what the node using it gives back.",
					                   { { "in", type, {}, nullptr, colour } }, {} };
					made.pass[i]   = { kPassNode, "Sub-graph", "A sub-graph's input or output, checked against its type.",
					                   { { "in", type, {}, nullptr, colour } }, { { "out", type, "{in}" } } };
				}
				return made;
			}();
			return fixed;
		}

		size_t IndexOf(ParameterType type) {
			return static_cast<size_t>(std::find(kAllTypes.begin(), kAllTypes.end(), type) - kAllTypes.begin());
		}

		const Parameter* FindIn(const std::vector<Parameter>& list, std::string_view name) {
			const auto found = std::find_if(list.begin(), list.end(), [&](const Parameter& p) { return p.name == name; });
			return found != list.end() ? &*found : nullptr;
		}

		std::string SettingOf(const Node& node, const char* key) {
			const auto found = node.settings.find(key);
			return found != node.settings.end() ? found->second : std::string();
		}

		// Why a sub-graph cannot be used, or "" if it can.
		std::string Check(const Graph& graph) {
			if (graph.domain != kSubgraphDomain) { return "it is a shader graph, not a sub-graph"; }
			if (!graph.parameters.empty()) {
				return "it has parameters; a sub-graph is given everything through its inputs (make it a parameter of the graph that uses it)";
			}
			if (graph.outputs.empty()) { return "it has no outputs"; }
			for (const std::vector<Parameter>* list : { &graph.inputs, &graph.outputs }) {
				const char* what = list == &graph.inputs ? "input" : "output";
				std::set<std::string> names;
				for (const Parameter& pin : *list) {
					if (!IsIdentifier(pin.name)) { return std::string(what) + " \"" + pin.name + "\" needs another name: a letter or '_', then letters, digits and '_'"; }
					if (!names.insert(pin.name).second) { return std::string("two ") + what + "s are called \"" + pin.name + "\""; }
				}
			}
			for (const Parameter& output : graph.outputs) {
				if (output.type == ParameterType::Texture) { return "output \"" + output.name + "\" is a texture; a sub-graph gives back numbers, vectors and colours"; }
			}
			std::set<std::string> given;   // outputs that have a node giving them
			for (const Node& node : graph.nodes) {
				const std::string at = "node " + std::to_string(node.id) + " (" + node.type + ")";
				if (node.type == kSubgraphOutputNode && !given.insert(SettingOf(node, kSubgraphOutputSetting)).second) {
					return "two nodes give output \"" + SettingOf(node, kSubgraphOutputSetting) + "\": an output is given by one node";
				}
				if (node.type == kOutputNode) { return at + ": a sub-graph has SubgraphOutput nodes, not an Output node"; }
				if (node.type == kParameterNode) { return at + ": a sub-graph reads no parameters; give it an input instead"; }
				if (node.type == kSubgraphInputNode && !FindIn(graph.inputs, SettingOf(node, kSubgraphInputSetting))) {
					return at + " reads input \"" + SettingOf(node, kSubgraphInputSetting) + "\", which the sub-graph does not have";
				}
				if (node.type == kSubgraphOutputNode && !FindIn(graph.outputs, SettingOf(node, kSubgraphOutputSetting))) {
					return at + " gives output \"" + SettingOf(node, kSubgraphOutputSetting) + "\", which the sub-graph does not have";
				}
			}
			return {};
		}

		void Add(std::string_view guid, std::string_view label, Graph graph, std::string error) {
			auto entry = std::make_unique<SubgraphEntry>();
			entry->guid = Upper(guid);
			entry->label = std::string(label);
			entry->graph = std::move(graph);
			entry->error = error.empty() ? Check(entry->graph) : std::move(error);

			// The node that uses it: its pins, named by strings the entry keeps.
			entry->names.push_back(entry->label);
			entry->type = { kSubgraphNode, "Sub-graph", entry->names.back().c_str(), {}, {} };
			if (entry->error.empty()) {
				for (const Parameter& input : entry->graph.inputs) {
					entry->names.push_back(input.name);
					entry->type.inputs.push_back({ entry->names.back().c_str(), ValueTypeOf(input.type), input.defaultValue, nullptr,
					                               input.type == ParameterType::Colour });
				}
				for (const Parameter& output : entry->graph.outputs) {
					entry->names.push_back(output.name);
					entry->type.outputs.push_back({ entry->names.back().c_str(), ValueTypeOf(output.type), "" });
				}
			}

			Library& library = TheLibrary();
			library.entries[entry->guid] = std::move(entry);
			++library.version;
		}

	}   // anonymous namespace

	ValueType ValueTypeOf(ParameterType type) {
		switch (type) {
		case ParameterType::Vec2:    return ValueType::Float2;
		case ParameterType::Vec3:
		case ParameterType::Colour:  return ValueType::Float3;
		case ParameterType::Vec4:    return ValueType::Float4;
		case ParameterType::Texture: return ValueType::Texture;
		default:                     return ValueType::Float;
		}
	}

	// ---- The library ---------------------------------------------------------------------------

	void RegisterSubgraph(std::string_view guid, std::string_view label, std::string_view fileText) {
		Graph graph;
		std::string error;
		if (!Load(fileText, graph, error)) { error = "the file does not load (" + error + ")"; }
		Add(guid, label, std::move(graph), std::move(error));
	}

	void RegisterSubgraph(std::string_view guid, std::string_view label, Graph graph) {
		Add(guid, label, std::move(graph), {});
	}

	void UnregisterSubgraph(std::string_view guid) {
		Library& library = TheLibrary();
		if (library.entries.erase(Upper(guid)) != 0) { ++library.version; }
	}

	void ClearSubgraphs() {
		Library& library = TheLibrary();
		if (!library.entries.empty()) {
			library.entries.clear();
			++library.version;
		}
	}

	const SubgraphEntry* FindSubgraph(std::string_view guid) {
		const Library& library = TheLibrary();
		const auto found = library.entries.find(Upper(guid));
		return found != library.entries.end() ? found->second.get() : nullptr;
	}

	std::vector<const SubgraphEntry*> ListSubgraphs() {
		std::vector<const SubgraphEntry*> list;
		for (const auto& [guid, entry] : TheLibrary().entries) { list.push_back(entry.get()); }
		std::sort(list.begin(), list.end(), [](const SubgraphEntry* a, const SubgraphEntry* b) { return a->label < b->label; });
		return list;
	}

	uint64_t SubgraphsVersion() {
		return TheLibrary().version;
	}

	// ---- A node's pins ---------------------------------------------------------------------------

	const NodeType* TypeOfNode(const Graph& graph, const Node& node) {
		if (node.type == kSubgraphNode) {
			const SubgraphEntry* entry = FindSubgraph(SettingOf(node, kSubgraphSetting));
			return entry && entry->error.empty() ? &entry->type : nullptr;
		}
		if (node.type == kSubgraphInputNode || node.type == kSubgraphOutputNode) {
			const bool input = node.type == kSubgraphInputNode;
			const Parameter* pin = input ? FindIn(graph.inputs, SettingOf(node, kSubgraphInputSetting))
			                             : FindIn(graph.outputs, SettingOf(node, kSubgraphOutputSetting));
			if (!pin) { return nullptr; }
			return &(input ? Fixed().input : Fixed().output)[IndexOf(pin->type)];
		}
		if (node.type == kPassNode) {
			ParameterType type = ParameterType::Float;
			return TypeFromName(SettingOf(node, "type"), type) ? &Fixed().pass[IndexOf(type)] : nullptr;
		}
		return FindNodeType(node.type);
	}

	// ---- Expanding ---------------------------------------------------------------------------------

	Flattened Flatten(const Graph& source) {
		Flattened out;
		out.graph = source;
		Graph& graph = out.graph;

		// New ids: past every id the graph has, nodes and comments alike.
		uint32_t next = (std::max)(graph.nextId, 1u);
		for (const Node& node : graph.nodes) { next = (std::max)(next, node.id + 1); }
		for (const Comment& comment : graph.comments) { next = (std::max)(next, comment.id + 1); }

		// For a Subgraph node made by expanding: the sub-graphs it is inside, to catch one using itself.
		std::unordered_map<uint32_t, std::vector<std::string>> inside;

		struct Source {
			uint32_t    node = 0;
			std::string pin;
		};

		for (;;) {
			const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const Node& node) {
				return node.type == kSubgraphNode && out.failed.count(node.id) == 0;
			});
			if (found == graph.nodes.end()) { break; }
			const Node node = *found;   // a copy: graph.nodes is about to grow
			const uint32_t id = node.id;
			const bool own = out.origin.count(id) == 0;   // one of the original graph's own nodes
			const uint32_t outer = own ? id : out.origin[id];

			const std::string guid = SettingOf(node, kSubgraphSetting);
			const SubgraphEntry* entry = guid.empty() ? nullptr : FindSubgraph(guid);
			if (!entry) {
				std::string why = guid.empty() ? "no sub-graph is chosen for it" : "its sub-graph is not in Assets (deleted, or not imported yet)";
				if (const std::string label = SettingOf(node, kSubgraphLabelSetting); !guid.empty() && !label.empty()) {
					why = label + " is not in Assets (deleted, or not imported yet)";
					for (const SubgraphEntry* other : ListSubgraphs()) {
						if (other->label != label) { continue; }
						// The same name under another GUID: most likely the file was moved without its .meta.
						why = "a " + label + " is in Assets, but not the one this node was made with (its GUID differs: was the file moved without "
						      "its .meta?). Right-click the node and choose Use sub-graph to point it there";
						break;
					}
				}
				out.failed[id] = why;
				continue;
			}
			if (!entry->error.empty()) {
				out.failed[id] = entry->label + " cannot be used: " + entry->error;
				continue;
			}
			std::vector<std::string> chain = inside[id];
			if (std::find(chain.begin(), chain.end(), entry->guid) != chain.end()) {
				out.failed[id] = entry->label + " uses itself, through the sub-graphs inside it";
				continue;
			}
			if (chain.size() >= kMaxDepth) {
				out.failed[id] = "sub-graphs inside sub-graphs go more than " + std::to_string(kMaxDepth) + " deep here";
				continue;
			}
			chain.push_back(entry->guid);

			// A link into a pin the sub-graph does not have (any more): say so rather than guess.
			std::string stale;
			for (const Link& link : graph.links) {
				if (link.toNode == id && entry->type.FindInput(link.toPin) < 0) { stale = "a link goes into \"" + link.toPin + "\", and " + entry->label + " has no such input"; }
				if (link.fromNode == id && entry->type.FindOutput(link.fromPin) < 0) { stale = "a link comes out of \"" + link.fromPin + "\", and " + entry->label + " has no such output"; }
			}
			if (!stale.empty()) {
				out.failed[id] = stale;
				continue;
			}

			const Graph& sub = entry->graph;
			const auto make = [&](Node made) {
				made.id = next++;
				out.origin[made.id] = outer;
				out.from[made.id] = entry->label;
				if (made.type == kSubgraphNode) { inside[made.id] = chain; }
				graph.nodes.push_back(std::move(made));
				return graph.nodes.back().id;
			};

			// Its nodes, under new ids - but for its inputs and outputs, which are wired up below.
			std::unordered_map<uint32_t, uint32_t> ids;
			for (const Node& node2 : sub.nodes) {
				if (node2.type == kSubgraphInputNode || node2.type == kSubgraphOutputNode) { continue; }
				ids[node2.id] = make(node2);
			}

			// Each number input: a _Pass node of its type, given what was linked into the Subgraph
			// node's pin or, with nothing linked, the node's constant (or the input's default).
			// A texture input is wired straight to what was linked into it.
			std::unordered_map<std::string, Source> inputs;
			for (const Parameter& input : sub.inputs) {
				Source linked;
				if (const Link* link = graph.FindLinkInto(id, input.name)) { linked = { link->fromNode, link->fromPin }; }
				if (input.type == ParameterType::Texture) {
					if (linked.node != 0) { inputs[input.name] = linked; }
					continue;
				}
				Node pass;
				pass.type = kPassNode;
				pass.settings["type"] = ParameterTypeName(input.type);
				const auto constant = node.inputs.find(input.name);
				pass.inputs["in"] = constant != node.inputs.end() ? constant->second : input.defaultValue;
				if (own) {
					// A constant typed on the user's own node: the live preview reads it as that
					// node's (ShaderGraphGenerator.h, Options::liveConstants), so it changes at once.
					pass.settings["liveNode"] = std::to_string(id);
					pass.settings["livePin"] = input.name;
				}
				const uint32_t passId = make(std::move(pass));
				out.pinOf[passId] = input.name;
				if (linked.node != 0) { graph.links.push_back({ linked.node, linked.pin, passId, "in" }); }
				inputs[input.name] = { passId, "out" };
			}

			// Its links, with its inputs and outputs replaced by what they stand for.
			std::unordered_map<std::string, Source> outputs;   // what each output is given
			for (const Link& link : sub.links) {
				const Node* a = sub.FindNode(link.fromNode);
				const Node* b = sub.FindNode(link.toNode);
				if (!a || !b || a->type == kSubgraphOutputNode || b->type == kSubgraphInputNode) { continue; }   // nothing to wire
				Source from{ 0, link.fromPin };
				if (a->type == kSubgraphInputNode) {
					const auto given = inputs.find(SettingOf(*a, kSubgraphInputSetting));
					if (given == inputs.end()) { continue; }   // a texture input with nothing linked: its reader says it needs one
					from = given->second;
				} else {
					from.node = ids[a->id];
				}
				if (b->type == kSubgraphOutputNode) {
					outputs[SettingOf(*b, kSubgraphOutputSetting)] = from;
					continue;
				}
				graph.links.push_back({ from.node, from.pin, ids[b->id], link.toPin });
			}

			// Each output: a _Pass node of its type, given what the sub-graph gives it, and read by
			// whatever read the Subgraph node's pin.
			for (const Parameter& output : sub.outputs) {
				Node pass;
				pass.type = kPassNode;
				pass.settings["type"] = ParameterTypeName(output.type);
				// With nothing linked into it, an output gives the constant typed on its node.
				for (const Node& given : sub.nodes) {
					if (given.type == kSubgraphOutputNode && SettingOf(given, kSubgraphOutputSetting) == output.name) { pass.inputs = given.inputs; }
				}
				const uint32_t passId = make(std::move(pass));
				out.pinOf[passId] = output.name;
				if (const auto given = outputs.find(output.name); given != outputs.end()) {
					graph.links.push_back({ given->second.node, given->second.pin, passId, "in" });
				}
				for (Link& link : graph.links) {
					if (link.fromNode == id && link.fromPin == output.name) {
						link.fromNode = passId;
						link.fromPin = "out";
					}
				}
			}

			// The Subgraph node is now its contents.
			std::erase_if(graph.links, [&](const Link& link) { return link.toNode == id || link.fromNode == id; });
			std::erase_if(graph.nodes, [&](const Node& each) { return each.id == id; });
		}

		graph.nextId = next;
		return out;
	}

	std::string SubgraphProblem(const Graph& subgraph) {
		return Check(subgraph);
	}

	Graph SubgraphAsSurface(const Graph& subgraph, size_t previewOutput) {
		Graph surface = subgraph;
		surface.domain = kSurfaceDomain;
		surface.inputs.clear();
		surface.outputs.clear();
		uint32_t next = (std::max)(surface.nextId, 1u);
		for (const Node& node : surface.nodes) { next = (std::max)(next, node.id + 1); }
		for (const Comment& comment : surface.comments) { next = (std::max)(next, comment.id + 1); }

		uint32_t previewed = 0;   // the node the previewed output became
		for (Node& node : surface.nodes) {
			if (node.type == kSubgraphInputNode) {
				const Parameter* input = FindIn(subgraph.inputs, SettingOf(node, kSubgraphInputSetting));
				if (!input) { continue; }   // left as it is: SubgraphProblem says why
				if (input->type == ParameterType::Texture) {
					// A texture parameter of its own, showing what its default names.
					Parameter texture = *input;
					texture.name = "_in_" + input->name;
					if (!surface.FindParameter(texture.name)) { surface.parameters.push_back(texture); }
					node.type = kParameterNode;
					node.settings = { { kParameterSetting, texture.name } };
				} else {
					node.type = kPassNode;
					node.settings = { { "type", ParameterTypeName(input->type) } };
					node.inputs = { { "in", input->defaultValue } };
				}
			} else if (node.type == kSubgraphOutputNode) {
				const std::string name = SettingOf(node, kSubgraphOutputSetting);
				const Parameter* output = FindIn(subgraph.outputs, name);
				if (!output) { continue; }
				if (previewOutput < subgraph.outputs.size() && subgraph.outputs[previewOutput].name == name && previewed == 0) { previewed = node.id; }
				node.type = kPassNode;
				node.settings = { { "type", ParameterTypeName(output->type) }, { "subgraphOutput", "1" } };
			}
		}

		// The Output, right of everything, with the previewed output as its albedo.
		const uint32_t output = next++;
		surface.nodes.push_back({ output, kOutputNode, 0.0f, 0.0f, {}, {} });
		if (previewed != 0) {
			const ParameterType type = subgraph.outputs[previewOutput].type;
			if (type == ParameterType::Vec2) {
				const uint32_t widen = next++;
				surface.nodes.push_back({ widen, "AppendZ", 0.0f, 0.0f, {}, {} });
				surface.links.push_back({ previewed, "out", widen, "xy" });
				surface.links.push_back({ widen, "out", output, "albedo" });
			} else if (type == ParameterType::Vec4) {
				const uint32_t split = next++;
				surface.nodes.push_back({ split, "Split", 0.0f, 0.0f, {}, {} });
				surface.links.push_back({ previewed, "out", split, "in" });
				surface.links.push_back({ split, "xyz", output, "albedo" });
			} else {
				surface.links.push_back({ previewed, "out", output, "albedo" });   // a float is widened
			}
		}
		surface.nextId = next;
		return surface;
	}

}
