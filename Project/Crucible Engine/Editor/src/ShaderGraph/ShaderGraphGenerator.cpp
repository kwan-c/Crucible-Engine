/*!
* @file			ShaderGraphGenerator.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Checks a shader graph and writes its Slang source - see ShaderGraphGenerator.h.
*
* The work is done in the order Generate() reads: the parameters are checked, the nodes and
* links are looked up once (NodeInfo), the nodes are put in an order where each comes after
* the ones it needs (which also finds loops), the ones the Output uses are marked, and the
* types are worked out along that order. If nothing in the way of the Output is wrong, the
* shader is written along the same order.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ShaderGraph/ShaderGraphGenerator.h"

#include "ShaderGraph/ShaderGraphNodes.h"

#include <algorithm>
#include <array>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Crucible::ShaderGraph {

	namespace {

		// When a problem keeps the shader from being made.
		enum class Stops { Always, IfUsed, Never };   // IfUsed: only if the Output uses its node

		// What the generator knows about one node, looked up once.
		struct NodeInfo {
			const Node*      node = nullptr;
			const NodeType*  type = nullptr;        // null: a type this Editor does not know
			std::vector<int> incoming;              // per input pin: the link into it (index in Graph::links), or -1
			bool             sorted = false;        // it has a place in the order: nothing it needs is in a loop
			bool             used = false;          // the Output uses it
			bool             valid = false;         // its inputs are fine: its outputs have types
			ValueType        working = ValueType::Float;   // the type an "any" node works in
			std::vector<ValueType> outTypes;        // per output pin
			std::vector<char>      outExists;       // per output pin: the working type is wide enough for it
			std::vector<char>      outUsed;         // per output pin: something that is written reads it
			const Parameter* parameter = nullptr;   // Parameter nodes: the parameter read
		};

		// Words that cannot name a member of the generated parameter struct.
		constexpr std::array<std::string_view, 44> kReservedWords{
			"float", "float2", "float3", "float4", "int", "uint", "bool", "half", "double", "void",
			"if", "else", "for", "while", "do", "return", "struct", "class", "enum", "interface",
			"in", "out", "inout", "const", "static", "true", "false", "break", "continue", "switch",
			"case", "default", "discard", "this", "var", "let", "typedef", "namespace", "import", "public",
			"private", "internal", "func", "extension"
		};

		// A constant of `type` as a Slang expression. A lone negative number is bracketed, so it
		// can follow an operator ("a - (-0.5)").
		std::string ConstantText(ValueType type, const std::array<float, 4>& values) {
			const uint32_t count = ComponentCount(type);
			if (count <= 1) {
				const std::string number = NumberText(values[0]);
				return values[0] < 0.0f ? "(" + number + ")" : number;
			}
			std::string text = std::string(ValueTypeName(type)) + "(";
			for (uint32_t i = 0; i < count; ++i) {
				if (i != 0) { text += ", "; }
				text += NumberText(values[i]);
			}
			return text + ")";
		}

		// A float expression repeated into a vector: float3(x).
		std::string Widened(const std::string& expression, ValueType to) {
			return std::string(ValueTypeName(to)) + "(" + expression + ")";
		}

		ValueType TypeOf(const Parameter& parameter) {
			switch (parameter.type) {
			case ParameterType::Vec2:    return ValueType::Float2;
			case ParameterType::Vec3:
			case ParameterType::Colour:  return ValueType::Float3;
			case ParameterType::Vec4:    return ValueType::Float4;
			case ParameterType::Texture: return ValueType::Texture;
			default:                     return ValueType::Float;
			}
		}

		class Builder {
		public:
			Builder(const Graph& graph, Generated& result, const Options& options) : m_graph(graph), m_result(result), m_options(options) {}

			void Run() {
				CheckParameters();
				IndexNodes();
				IndexLinks();
				FindOutput();
				Sort();
				MarkUsed();
				ResolveTypes();

				// Which problems are in the way of the Output.
				m_result.ok = true;
				for (size_t i = 0; i < m_result.problems.size(); ++i) {
					Problem& problem = m_result.problems[i];
					const auto node = m_byId.find(problem.node);
					switch (m_stops[i]) {
					case Stops::Always: problem.stops = true; break;
					case Stops::Never:  problem.stops = false; break;
					default:            problem.stops = node == m_byId.end() || m_nodes[node->second].used; break;
					}
					if (problem.stops) { m_result.ok = false; }
				}
				for (const NodeInfo& info : m_nodes) {
					if (info.type) { m_result.nodes.push_back({ info.node->id, info.valid, info.used, info.working, info.outTypes }); }
				}
				if (m_result.ok) {
					Write();
					WriteParameterList();
				}
			}

		private:
			void Report(Stops stops, uint32_t node, std::string pin, std::string message) {
				m_result.problems.push_back({ node, std::move(pin), std::move(message), true });
				m_stops.push_back(stops);
			}

			NodeInfo* Find(uint32_t id) {
				const auto found = m_byId.find(id);
				return found != m_byId.end() ? &m_nodes[found->second] : nullptr;
			}

			// "node 4 (Lerp)" - how messages name another node.
			std::string Name(uint32_t id) const {
				const Node* node = m_graph.FindNode(id);
				return "node " + std::to_string(id) + (node ? " (" + node->type + ")" : std::string());
			}

			// ---- 1. The parameters -----------------------------------------------------------------

			void CheckParameters() {
				std::set<std::string_view> names;
				size_t numbers = 0, textures = 0;
				for (const Parameter& parameter : m_graph.parameters) {
					const std::string quoted = "parameter \"" + parameter.name + "\"";
					if (!IsIdentifier(parameter.name)) {
						Report(Stops::Always, 0, {}, quoted + " needs another name: a letter or '_', then letters, digits and '_'");
					} else if (std::find(kReservedWords.begin(), kReservedWords.end(), parameter.name) != kReservedWords.end()) {
						Report(Stops::Always, 0, {}, quoted + " needs another name: that is a word of the shader language");
					} else if (!names.insert(parameter.name).second) {
						Report(Stops::Always, 0, {}, "two parameters are called \"" + parameter.name + "\"");
					}

					if (parameter.type == ParameterType::Texture) {
						++textures;
						const std::string& fallback = parameter.textureDefault;
						if (fallback != "white" && fallback != "black" && fallback != "normal") {
							Report(Stops::Always, 0, {}, quoted + ": its default is \"" + fallback + "\"; it can be \"white\", \"black\" or \"normal\"");
						}
					} else {
						++numbers;
					}
				}
				if (numbers > kMaxNumberParameters) {
					Report(Stops::Always, 0, {}, "the graph has " + std::to_string(numbers) + " number and colour parameters; a shader can have " + std::to_string(kMaxNumberParameters));
				}
				if (textures > kMaxTextureParameters) {
					Report(Stops::Always, 0, {}, "the graph has " + std::to_string(textures) + " texture parameters; a shader can have " + std::to_string(kMaxTextureParameters));
				}
			}

			// ---- 2. Nodes and links, looked up once ---------------------------------------------------

			void IndexNodes() {
				m_nodes.resize(m_graph.nodes.size());
				for (size_t i = 0; i < m_graph.nodes.size(); ++i) {
					const Node& node = m_graph.nodes[i];
					NodeInfo& info = m_nodes[i];
					info.node = &node;
					if (node.id == 0 || !m_byId.emplace(node.id, i).second) {
						Report(Stops::Always, node.id, {}, node.id == 0 ? "a node has no id" : "two nodes have the id " + std::to_string(node.id));
						continue;
					}
					info.type = FindNodeType(node.type);
					if (!info.type) {
						Report(Stops::IfUsed, node.id, {}, "\"" + node.type + "\" is not a kind of node this Editor has (was the graph made by a newer one?)");
						continue;
					}
					info.incoming.assign(info.type->inputs.size(), -1);
					info.outTypes.assign(info.type->outputs.size(), ValueType::Float);
					info.outExists.assign(info.type->outputs.size(), 0);
					info.outUsed.assign(info.type->outputs.size(), 0);
				}
			}

			void IndexLinks() {
				for (size_t i = 0; i < m_graph.links.size(); ++i) {
					const Link& link = m_graph.links[i];
					NodeInfo* to = Find(link.toNode);
					NodeInfo* from = Find(link.fromNode);
					if (!to) {
						// It goes nowhere, so it changes nothing; worth saying all the same.
						Report(Stops::Never, from ? link.fromNode : 0, from ? link.fromPin : std::string(),
						       "a link goes to node " + std::to_string(link.toNode) + ", which is not in the graph");
						continue;
					}
					if (!to->type) { continue; }   // already reported: an unknown kind of node

					const int toPin = to->type->FindInput(link.toPin);
					if (toPin < 0) {
						Report(Stops::IfUsed, link.toNode, link.toPin, "a link goes to \"" + link.toPin + "\", and this node has no such input");
						m_faulty.insert(link.toNode);
						continue;
					}
					if (!from) {
						Report(Stops::IfUsed, link.toNode, link.toPin, "its link comes from node " + std::to_string(link.fromNode) + ", which is not in the graph");
						m_faulty.insert(link.toNode);
						continue;
					}
					if (from == to) {
						Report(Stops::IfUsed, link.toNode, link.toPin, "it is linked to the node's own output");
						m_faulty.insert(link.toNode);
						continue;
					}
					if (from->type && from->type->FindOutput(link.fromPin) < 0) {
						Report(Stops::IfUsed, link.toNode, link.toPin, "its link comes from \"" + link.fromPin + "\" of " + Name(link.fromNode) + ", which has no such output");
						m_faulty.insert(link.toNode);
						continue;
					}
					if (to->incoming[static_cast<size_t>(toPin)] >= 0) {
						Report(Stops::IfUsed, link.toNode, link.toPin, "it has more than one link; an input takes one");
						m_faulty.insert(link.toNode);
						continue;
					}
					to->incoming[static_cast<size_t>(toPin)] = static_cast<int>(i);
				}
			}

			void FindOutput() {
				for (size_t i = 0; i < m_nodes.size(); ++i) {
					if (!m_nodes[i].type || m_nodes[i].node->type != kOutputNode) { continue; }
					if (m_output == kNone) {
						m_output = i;
					} else {
						Report(Stops::Always, m_nodes[i].node->id, {}, "a graph has one Output node, and this is a second one");
					}
				}
				if (m_output == kNone) {
					Report(Stops::Always, 0, {}, "the graph has no Output node, so there is nothing to draw");
				}
			}

			// The node a link comes from (as an index into m_nodes).
			size_t SourceOf(int link) const {
				return m_byId.find(m_graph.links[static_cast<size_t>(link)].fromNode)->second;
			}

			// ---- 3. An order where every node comes after the ones it needs; what is left is in a loop ----

			void Sort() {
				const size_t count = m_nodes.size();
				std::vector<size_t> waitingFor(count, 0);          // inputs whose source is not placed yet
				std::vector<std::vector<size_t>> readers(count);   // per node: the nodes its outputs go into (once per link)
				for (size_t i = 0; i < count; ++i) {
					for (const int link : m_nodes[i].incoming) {
						if (link < 0) { continue; }
						++waitingFor[i];
						readers[SourceOf(link)].push_back(i);
					}
				}

				m_order.reserve(count);
				for (size_t i = 0; i < count; ++i) {
					if (waitingFor[i] == 0) { m_order.push_back(i); }
				}
				for (size_t next = 0; next < m_order.size(); ++next) {
					const size_t placed = m_order[next];
					m_nodes[placed].sorted = true;
					for (const size_t reader : readers[placed]) {
						if (--waitingFor[reader] == 0) { m_order.push_back(reader); }
					}
				}
				if (m_order.size() == count) { return; }

				// The nodes left over are in a loop, or only read from one. Peel off the second
				// kind - left-over nodes nothing left over reads - until only loops remain.
				std::vector<char> left(count, 0);
				for (size_t i = 0; i < count; ++i) { left[i] = m_nodes[i].sorted ? 0 : 1; }
				std::vector<size_t> readBy(count, 0);   // left-over readers of each left-over node
				for (size_t i = 0; i < count; ++i) {
					if (!left[i]) { continue; }
					for (const size_t reader : readers[i]) { readBy[i] += left[reader] ? 1 : 0; }
				}
				std::vector<size_t> peel;
				for (size_t i = 0; i < count; ++i) {
					if (left[i] && readBy[i] == 0) { peel.push_back(i); }
				}
				while (!peel.empty()) {
					const size_t node = peel.back();
					peel.pop_back();
					left[node] = 0;
					for (const int link : m_nodes[node].incoming) {
						if (link < 0) { continue; }
						const size_t source = SourceOf(link);
						if (left[source] && --readBy[source] == 0) { peel.push_back(source); }
					}
				}
				for (size_t i = 0; i < count; ++i) {
					if (left[i]) {
						Report(Stops::IfUsed, m_nodes[i].node->id, {}, "it is part of a loop: its output comes back round into its own input");
					}
				}
			}

			// ---- 4. What the Output uses -------------------------------------------------------------

			void MarkUsed() {
				if (m_output == kNone) { return; }
				std::vector<size_t> open{ m_output };
				m_nodes[m_output].used = true;
				while (!open.empty()) {
					const size_t node = open.back();
					open.pop_back();
					for (const int link : m_nodes[node].incoming) {
						if (link < 0) { continue; }
						const size_t source = SourceOf(link);
						if (!m_nodes[source].used) {
							m_nodes[source].used = true;
							open.push_back(source);
						}
					}
				}
			}

			// ---- 5. Types, along the order --------------------------------------------------------------

			void ResolveTypes() {
				for (const size_t index : m_order) {
					NodeInfo& info = m_nodes[index];
					if (!info.type) { continue; }
					const uint32_t id = info.node->id;
					bool fine = m_faulty.count(id) == 0;

					if (info.node->type == kParameterNode) {
						const auto setting = info.node->settings.find(kParameterSetting);
						if (setting == info.node->settings.end() || setting->second.empty()) {
							Report(Stops::IfUsed, id, {}, "no parameter is chosen for it");
							fine = false;
						} else if (const Parameter* parameter = m_graph.FindParameter(setting->second); !parameter) {
							Report(Stops::IfUsed, id, {}, "its parameter \"" + setting->second + "\" is not in the graph's parameters");
							fine = false;
						} else {
							info.parameter = parameter;
							info.outTypes[0] = TypeOf(*parameter);
							info.outExists[0] = 1;
						}
						info.valid = fine;
						continue;
					}

					// Each input against what is plugged into it.
					std::vector<ValueType> given(info.type->inputs.size(), ValueType::Any);   // Any = nothing to go by
					uint32_t widest = 0;
					for (size_t p = 0; p < info.type->inputs.size(); ++p) {
						const InputPin& pin = info.type->inputs[p];
						const int linkIndex = info.incoming[p];
						if (linkIndex < 0) {
							if (pin.type == ValueType::Texture) {
								Report(Stops::IfUsed, id, pin.name, "it needs a texture: link a Parameter node with a texture parameter to it");
								fine = false;
							}
							continue;
						}
						const Link& link = m_graph.links[static_cast<size_t>(linkIndex)];
						const NodeInfo& source = m_nodes[SourceOf(linkIndex)];
						const int sourcePin = source.type ? source.type->FindOutput(link.fromPin) : -1;
						if (!source.valid || sourcePin < 0) {
							fine = false;   // the source has a problem of its own, already reported
							continue;
						}
						if (!source.outExists[static_cast<size_t>(sourcePin)]) {
							// e.g. the "z" of a Split that is given a float2. Said once, on the source.
							if (m_missingOutputs.emplace(link.fromNode, link.fromPin).second) {
								Report(Stops::IfUsed, link.fromNode, link.fromPin, std::string("the node's input is a ") + ValueTypeName(source.working) +
								       ", which has no " + link.fromPin);
							}
							fine = false;
							continue;
						}

						const ValueType from = source.outTypes[static_cast<size_t>(sourcePin)];
						const std::string fromName = ValueTypeName(from);
						if (pin.type == ValueType::Texture) {
							if (from != ValueType::Texture) {
								Report(Stops::IfUsed, id, pin.name, "it needs a texture, and it is given a " + fromName);
								fine = false;
							}
						} else if (from == ValueType::Texture) {
							Report(Stops::IfUsed, id, pin.name, "a texture cannot go into a number input: read it with a SampleTexture node first");
							fine = false;
						} else if (pin.type == ValueType::Any) {
							given[p] = from;
							widest = (std::max)(widest, ComponentCount(from));
						} else if (from != pin.type && from != ValueType::Float) {
							const std::string pinName = ValueTypeName(pin.type);
							Report(Stops::IfUsed, id, pin.name, ComponentCount(from) > ComponentCount(pin.type) ?
								"a " + fromName + " cannot go into a " + pinName + " input: take the part you want with a Split node" :
								"a " + fromName + " cannot go into a " + pinName + " input: build a " + pinName + " with a Vector node");
							fine = false;
						}
					}

					// The type an "any" node works in: the widest it is given; a float fits any.
					info.working = widest == 0 ? ValueType::Float : NumericType(widest);
					for (size_t p = 0; p < given.size(); ++p) {
						if (given[p] == ValueType::Any || given[p] == ValueType::Float || given[p] == info.working) { continue; }
						Report(Stops::IfUsed, id, info.type->inputs[p].name, std::string("it is given a ") + ValueTypeName(given[p]) +
						       ", and another input a " + ValueTypeName(info.working) + ": the node's inputs must be the same type (or a float)");
						fine = false;
					}
					if (fine && info.type->minComponents > ComponentCount(info.working)) {
						Report(Stops::IfUsed, id, {}, "it works on vectors: link a float2, float3 or float4 to it");
						fine = false;
					}

					for (size_t o = 0; o < info.type->outputs.size(); ++o) {
						const OutputPin& pin = info.type->outputs[o];
						info.outTypes[o] = pin.type == ValueType::Any ? info.working : pin.type;
						info.outExists[o] = pin.needs <= ComponentCount(info.working) ? 1 : 0;
					}
					info.valid = fine;
				}
			}

			// ---- 6. The shader ------------------------------------------------------------------------

			// Adds text to the source. Every line of it belongs to `node` (0 = none).
			void Emit(std::string_view text, uint32_t node = 0) {
				m_result.source += text;
				m_result.lineNodes.insert(m_result.lineNodes.end(), static_cast<size_t>(std::count(text.begin(), text.end(), '\n')), node);
			}

			// The Slang expression for an output pin: the local it is in, or the parameter itself.
			std::string OutputText(const NodeInfo& info, size_t pin) const {
				if (info.parameter) {
					return (info.parameter->type == ParameterType::Texture ? "gTex_" : "P.") + info.parameter->name;
				}
				return "n" + std::to_string(info.node->id) + "_" + info.type->outputs[pin].name;
			}

			// The Slang expression for what goes into an input pin: the link's value, the pin's
			// own expression ("s.uv"), or a constant.
			std::string InputText(const NodeInfo& info, size_t pinIndex) {
				const InputPin& pin = info.type->inputs[pinIndex];
				const ValueType wanted = pin.type == ValueType::Any ? info.working : pin.type;

				if (const int linkIndex = info.incoming[pinIndex]; linkIndex >= 0) {
					const Link& link = m_graph.links[static_cast<size_t>(linkIndex)];
					const NodeInfo& source = m_nodes[SourceOf(linkIndex)];
					const size_t sourcePin = static_cast<size_t>(source.type->FindOutput(link.fromPin));
					const std::string text = OutputText(source, sourcePin);
					return source.outTypes[sourcePin] == wanted ? text : Widened(text, wanted);   // only a float is ever widened
				}
				if (pin.defaultCode) { return pin.defaultCode; }

				const auto constant = info.node->inputs.find(pin.name);
				const std::array<float, 4>& values = constant != info.node->inputs.end() ? constant->second : pin.defaultValue;
				if (m_options.liveConstants) {
					// For a live preview: read from a hidden parameter, not written as a number.
					const ValueType own = pin.type == ValueType::Any ? ValueType::Float : pin.type;
					if (const std::string live = LiveConstantText(info, pin, own, values); !live.empty()) {
						return own == wanted ? live : Widened(live, wanted);
					}
				}
				if (pin.type == ValueType::Any) {
					// One number, whatever the node works in.
					const std::string number = ConstantText(ValueType::Float, values);
					return wanted == ValueType::Float ? number : Widened(NumberText(values[0]), wanted);
				}
				return ConstantText(wanted, values);
			}

			// The expression that reads a constant from its hidden parameter, which is made the
			// first time the constant is asked for (a row's code can name a pin twice). Empty if
			// the parameter list is full: that constant stays a number in the code.
			std::string LiveConstantText(const NodeInfo& info, const InputPin& pin, ValueType type, const std::array<float, 4>& values) {
				constexpr std::array<const char*, 5> kParts{ "", ".x", ".xy", ".xyz", "" };
				const auto read = [&](size_t slot) { return "MaterialParam(" + std::to_string(slot) + ")" + kParts[ComponentCount(type)]; };
				for (const Generated::LiveConstant& known : m_result.liveConstants) {
					if (known.node == info.node->id && known.pin == pin.name) { return read(known.slot); }
				}
				const size_t slot = m_numberCount + m_result.liveConstants.size();
				if (slot >= kMaxNumberParameters) { return {}; }
				m_result.liveConstants.push_back({ info.node->id, pin.name, LiveConstantName(info.node->id, pin.name), slot, type, values });
				return read(slot);
			}

			// An output's code with its {placeholders} filled in.
			std::string Expand(const NodeInfo& info, std::string_view code) {
				std::string text;
				size_t at = 0;
				while (at < code.size()) {
					const size_t open = code.find('{', at);
					const size_t close = open == std::string_view::npos ? open : code.find('}', open);
					if (close == std::string_view::npos) {
						text += code.substr(at);
						break;
					}
					text += code.substr(at, open - at);
					const std::string_view word = code.substr(open + 1, close - open - 1);
					if (word == "T") {
						text += ValueTypeName(info.working);
					} else if (const int input = info.type->FindInput(word); input >= 0) {
						text += InputText(info, static_cast<size_t>(input));
					} else if (const int output = info.type->FindOutput(word); output >= 0) {
						text += OutputText(info, static_cast<size_t>(output));
					}
					at = close + 1;
				}
				return text;
			}

			void Write() {
				// Which output pins are read: by a link into a node that is written, or by a later
				// output of the same node (SampleTexture's "rgb" reads its "rgba").
				for (const NodeInfo& info : m_nodes) {
					if (!info.used) { continue; }
					for (const int linkIndex : info.incoming) {
						if (linkIndex < 0) { continue; }
						NodeInfo& source = m_nodes[SourceOf(linkIndex)];
						source.outUsed[static_cast<size_t>(source.type->FindOutput(m_graph.links[static_cast<size_t>(linkIndex)].fromPin))] = 1;
					}
				}
				for (NodeInfo& info : m_nodes) {
					if (!info.used || info.parameter) { continue; }
					for (size_t o = info.type->outputs.size(); o-- > 0;) {
						if (!info.outUsed[o]) { continue; }
						const std::string_view code = info.type->outputs[o].code;
						for (size_t earlier = 0; earlier < o; ++earlier) {
							if (code.find("{" + std::string(info.type->outputs[earlier].name) + "}") != std::string_view::npos) { info.outUsed[earlier] = 1; }
						}
					}
				}

				Emit("// Written by the Crucible Editor from a shader graph. Do not edit: it is written again\n"
				     "// whenever the graph changes. The graph makes the surface function; the engine's mesh\n"
				     "// shader (SurfaceTemplate.slang) does the rest.\n"
				     "#include \"Surface.slang\"\n"
				     "#include \"GraphNodes.slang\"\n\n");

				// The parameters: textures by their slot, the rest as one struct read from the
				// material's values.
				size_t textureSlot = 0, numberSlot = 0;
				std::string members, loads;
				for (const Parameter& parameter : m_graph.parameters) {
					if (parameter.type == ParameterType::Texture) {
						Emit("MATERIAL_TEXTURE(" + std::to_string(textureSlot++) + ", gTex_" + parameter.name + ");\n");
						continue;
					}
					const ValueType type = TypeOf(parameter);
					constexpr std::array<const char*, 5> kParts{ "", ".x", ".xy", ".xyz", "" };
					members += std::string("    ") + ValueTypeName(type) + " " + parameter.name + ";\n";
					loads += "    P." + parameter.name + " = MaterialParam(" + std::to_string(numberSlot++) + ")" + kParts[ComponentCount(type)] + ";\n";
				}
				m_numberCount = numberSlot;   // hidden parameters (Options::liveConstants) follow the graph's own
				if (textureSlot != 0) { Emit("\n"); }
				if (numberSlot != 0) {
					Emit("// The graph's parameters: this material's own values for them.\n"
					     "struct GraphParams {\n" + members + "};\n\n"
					     "GraphParams LoadGraphParams() {\n    GraphParams P;\n" + loads + "    return P;\n}\n\n");
				}

				Emit("SurfaceData EvaluateSurface(SurfaceInput s) {\n");
				if (numberSlot != 0) { Emit("    GraphParams P = LoadGraphParams();\n"); }
				Emit("    SurfaceData o;\n");

				for (const size_t index : m_order) {
					const NodeInfo& info = m_nodes[index];
					if (!info.used || info.parameter) { continue; }
					const uint32_t id = info.node->id;
					Emit("\n    // node " + std::to_string(id) + ": " + info.type->name + "\n", id);

					if (index == m_output) {
						for (size_t p = 0; p < info.type->inputs.size(); ++p) {
							const std::string_view pin = info.type->inputs[p].name;
							Emit("    o." + std::string(pin == "normal" ? "normalTS" : pin) + " = " + InputText(info, p) + ";\n", id);
						}
						continue;
					}
					for (size_t o = 0; o < info.type->outputs.size(); ++o) {
						if (!info.outUsed[o]) { continue; }
						Emit(std::string("    ") + ValueTypeName(info.outTypes[o]) + " " + OutputText(info, o) + " = " +
						     Expand(info, info.type->outputs[o].code) + ";\n", id);
					}
				}

				Emit("\n    return o;\n}\n\n#include \"SurfaceTemplate.slang\"\n");
			}

			void WriteParameterList() {
				std::string numbers, textures;
				for (const Parameter& parameter : m_graph.parameters) {
					if (parameter.type == ParameterType::Texture) {
						textures += std::string(textures.empty() ? "" : ",\n") + "    { \"name\": \"" + parameter.name + "\", \"colour\": " +
						            (parameter.textureIsColour ? "true" : "false") + ", \"default\": \"" + parameter.textureDefault + "\" }";
						continue;
					}
					std::string value;
					if (parameter.type == ParameterType::Float) {
						value = NumberText(parameter.defaultValue[0]);
					} else {
						const uint32_t count = ComponentCount(TypeOf(parameter));
						for (uint32_t i = 0; i < count; ++i) { value += (i == 0 ? "[" : ", ") + NumberText(parameter.defaultValue[i]); }
						value += "]";
					}
					numbers += std::string(numbers.empty() ? "" : ",\n") + "    { \"name\": \"" + parameter.name + "\", \"type\": \"" +
					           ParameterTypeName(parameter.type) + "\", \"default\": " + value;
					if (parameter.type != ParameterType::Colour) {
						numbers += ", \"min\": " + NumberText(parameter.minValue) + ", \"max\": " + NumberText(parameter.maxValue);
					}
					numbers += " }";
				}
				// The hidden parameters of a live preview, after the graph's own: each a constant's
				// value, as the default.
				for (const Generated::LiveConstant& live : m_result.liveConstants) {
					constexpr std::array<const char*, 5> kTypes{ "float", "float", "vec2", "vec3", "vec4" };
					const uint32_t count = ComponentCount(live.type);
					std::string value = count <= 1 ? NumberText(live.value[0]) : std::string();
					for (uint32_t i = 0; count > 1 && i < count; ++i) { value += (i == 0 ? "[" : ", ") + NumberText(live.value[i]) + (i + 1 == count ? "]" : ""); }
					numbers += std::string(numbers.empty() ? "" : ",\n") + "    { \"name\": \"" + live.name + "\", \"type\": \"" + kTypes[count] +
					           "\", \"default\": " + value + " }";
				}
				m_result.paramsJson = "{\n  \"version\": 1,\n  \"params\": [" + (numbers.empty() ? std::string() : "\n" + numbers + "\n  ") +
				                      "],\n  \"textures\": [" + (textures.empty() ? std::string() : "\n" + textures + "\n  ") + "]\n}\n";
			}

			static constexpr size_t kNone = static_cast<size_t>(-1);

			const Graph&                         m_graph;
			Generated&                           m_result;
			const Options                        m_options;
			size_t                               m_numberCount = 0;   // the graph's own number and colour parameters
			std::vector<Stops>                   m_stops;     // one per problem
			std::vector<NodeInfo>                m_nodes;     // one per Graph::nodes entry, same order
			std::unordered_map<uint32_t, size_t> m_byId;      // node id -> index in m_nodes
			std::set<uint32_t>                   m_faulty;    // nodes with a broken link into them
			std::set<std::pair<uint32_t, std::string>> m_missingOutputs;   // (node, output pin) already reported as not there
			std::vector<size_t>                  m_order;     // every node that is not in (or after) a loop, sources first
			size_t                               m_output = kNone;
		};

	}   // anonymous namespace

	Generated Generate(const Graph& graph, const Options& options) {
		Generated result;
		Builder(graph, result, options).Run();
		if (!result.ok) {
			result.source.clear();
			result.paramsJson.clear();
			result.lineNodes.clear();
			result.liveConstants.clear();
		}
		return result;
	}

	std::string LiveConstantName(uint32_t node, std::string_view pin) {
		return "_n" + std::to_string(node) + "_" + std::string(pin);
	}

	bool ParseLiveConstantName(std::string_view name, uint32_t& node, std::string& pin) {
		if (name.size() < 5 || name.substr(0, 2) != "_n") { return false; }
		size_t at = 2;
		uint64_t id = 0;
		while (at < name.size() && name[at] >= '0' && name[at] <= '9' && id <= 0xFFFFFFFFull) { id = id * 10 + static_cast<uint64_t>(name[at++] - '0'); }
		if (at == 2 || id == 0 || id > 0xFFFFFFFFull || at + 1 >= name.size() || name[at] != '_') { return false; }
		node = static_cast<uint32_t>(id);
		pin = std::string(name.substr(at + 1));
		return true;
	}

	std::string Describe(const Graph& graph, const Problem& problem) {
		if (problem.node == 0) { return problem.message; }
		const Node* node = graph.FindNode(problem.node);
		std::string text = "node " + std::to_string(problem.node);
		if (node) { text += " (" + node->type + ")"; }
		if (!problem.pin.empty()) { text += ", " + problem.pin; }
		return text + ": " + problem.message;
	}

}
